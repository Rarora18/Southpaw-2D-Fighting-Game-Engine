// Southpaw test suite. No framework dependency: each TEST registers itself and
// CHECK records failures without aborting the run.
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "alloc_counter.h"
#include "core/ai.h"
#include "core/combat.h"
#include "core/json.h"
#include "core/move_data.h"
#include "core/replay.h"
#include "core/sim.h"

using namespace sp;

namespace {

int g_checks = 0;
int g_failures = 0;

struct TestCase {
    const char* name;
    void (*fn)();
};
std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); }
};

#define TEST(name)                                     \
    static void name();                                \
    static Registrar registrar_##name(#name, name);    \
    static void name()

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                           \
    } while (0)

std::string dataPath() { return std::string(SOUTHPAW_DATA_DIR) + "/moves.json"; }

const MoveSet& moves() {
    static MoveSet ms;
    static bool loaded = false;
    if (!loaded) {
        std::string err;
        if (!loadMoveSet(dataPath(), ms, err)) {
            std::printf("cannot load move data: %s\n", err.c_str());
            std::exit(1);
        }
        loaded = true;
    }
    return ms;
}

void toFight(Match& m) {
    while (m.state().phase != Phase::Fight) m.step(0, 0);
}

// Fresh match in the Fight phase with the fighters `gap` pixels apart.
void setup(Match& m, int gap, uint8_t buffer = 6, uint8_t training = 1) {
    MatchConfig cfg;
    cfg.bufferFrames = buffer;
    cfg.training = training;
    m.reset(moves(), cfg);
    toFight(m);
    m.mutableState().f[0].pos.x = toFx(-gap / 2);
    m.mutableState().f[1].pos.x = toFx(gap - gap / 2);
    m.step(0, 0);
}

bool sawEvent(const Match& m, EventType t, int who = -1) {
    for (int i = 0; i < m.events().count; ++i)
        if (m.events().list[i].type == t && (who < 0 || m.events().list[i].who == who)) return true;
    return false;
}

// Steps until `pred` holds or `limit` frames pass. Returns frames stepped, -1 on timeout.
template <class Pred>
int stepUntil(Match& m, InputBits p1, InputBits p2, int limit, Pred pred) {
    for (int i = 1; i <= limit; ++i) {
        m.step(p1, p2);
        if (pred()) return i;
    }
    return -1;
}

// ---------------------------------------------------------------- JSON / data

TEST(json_parses_values_and_comments) {
    Json j;
    std::string err;
    CHECK(Json::parse("// comment\n{\"a\": [1, 2.5, -3e2], \"b\": {\"c\": true}, \"d\": \"x\\ny\", \"e\": null}", j, err));
    CHECK(j.isObject());
    CHECK(j.get("a") && j.get("a")->array.size() == 3);
    CHECK(j.get("a")->array[2].number == -300.0);
    CHECK(j.get("b")->flag("c", false));
    CHECK(j.str("d", "") == "x\ny");
    CHECK(j.get("e") && j.get("e")->type == Json::Type::Null);
}

TEST(json_reports_errors_with_line_numbers) {
    Json j;
    std::string err;
    CHECK(!Json::parse("{\n  \"a\": 1,\n  \"b\" 2\n}", j, err));
    CHECK(err.find("line 3") != std::string::npos);
    CHECK(!Json::parse("[1, 2", j, err));
    CHECK(!Json::parse("{} trailing", j, err));
}

TEST(move_data_loads) {
    const MoveSet& ms = moves();
    CHECK(ms.count == 14);
    const int jab = ms.find("jab");
    CHECK(jab >= 0);
    CHECK(ms.moves[jab].startup == 4 && ms.moves[jab].active == 2 && ms.moves[jab].recovery == 7);
    CHECK(ms.moves[jab].cancelCount == 3);
    CHECK(ms.moves[ms.find("uppercut")].hit.shape == Shape::Arc);
    CHECK(ms.moves[ms.find("haymaker")].motion);
    CHECK(ms.hash != 0);
}

TEST(move_data_rejects_bad_files) {
    MoveSet ms;
    std::string err;
    const std::string fighter =
        "\"fighter\": {\"health\": 100, \"hurtbox_stand\": [0,0,1,1], \"hurtbox_crouch\": [0,0,1,1], "
        "\"hurtbox_air\": [0,0,1,1]}";
    const std::string good = "{" + fighter +
                             ", \"moves\": [{\"name\": \"a\", \"anim\": \"jab\", \"button\": \"LP\", \"startup\": 3, "
                             "\"active\": 2, \"recovery\": 5, \"damage\": 10, \"hitstun\": 10, \"blockstun\": 5, "
                             "\"hitbox\": {\"x\": 0, \"y\": 0, \"w\": 10, \"h\": 10}";
    CHECK(parseMoveSet(good + "}]}", ms, err));
    CHECK(!parseMoveSet(good + ", \"cancels\": [\"nope\"]}]}", ms, err));
    CHECK(err.find("unknown move \"nope\"") != std::string::npos);
    const std::string badArc = "{" + fighter +
                               ", \"moves\": [{\"name\": \"a\", \"anim\": \"jab\", \"button\": \"LP\", \"startup\": 3, "
                               "\"active\": 2, \"recovery\": 5, \"damage\": 10, \"hitstun\": 10, \"blockstun\": 5, "
                               "\"hitbox\": {\"type\": \"arc\", \"inner\": 50, \"outer\": 20, \"from\": 0, \"to\": 90}}]}";
    CHECK(!parseMoveSet(badArc, ms, err));
    CHECK(err.find("inner < outer") != std::string::npos);
}

// ---------------------------------------------------------------- input

TEST(input_buffer_window_and_consume) {
    InputBuffer b;
    b.clear();
    b.push(0);
    b.push(IN_LP);  // press
    for (int i = 0; i < 4; ++i) b.push(0);
    CHECK(b.findPress(0, 0) == -1);  // no buffer: only this frame counts
    CHECK(b.findPress(0, 6) == 4);
    CHECK(b.findPress(0, 3) == -1);
    b.consume(0, 4);
    CHECK(b.findPress(0, 6) == -1);  // a press can only start one move
    b.push(IN_LP);
    CHECK(b.findPress(0, 0) == 0);
    CHECK(b.findPress(1, 6) == -1);
}

TEST(input_held_button_is_not_a_new_press) {
    InputBuffer b;
    b.clear();
    for (int i = 0; i < 10; ++i) b.push(IN_HP);
    CHECK(b.findPress(1, 6) == -1);  // first press is 9 frames back
    b.push(0);
    b.push(IN_HP);
    CHECK(b.findPress(1, 0) == 0);
}

TEST(quarter_circle_forward_detection) {
    InputBuffer b;
    b.clear();
    b.push(IN_DOWN);
    b.push(IN_DOWN | IN_RIGHT);
    b.push(IN_RIGHT | IN_HP);
    CHECK(b.quarterCircleForward(+1, 0, 15));
    CHECK(!b.quarterCircleForward(-1, 0, 15));  // facing left the motion is backwards
    InputBuffer c;
    c.clear();
    c.push(IN_DOWN);
    c.push(IN_RIGHT | IN_HP);  // skipped the diagonal
    CHECK(!c.quarterCircleForward(+1, 0, 15));
}

// ---------------------------------------------------------------- geometry

TEST(box_overlap_and_mirroring) {
    const Box a{0, 0, toFx(10), toFx(10)};
    CHECK(boxesOverlap(a, {toFx(5), toFx(5), toFx(10), toFx(10)}));
    CHECK(!boxesOverlap(a, {toFx(10), 0, toFx(5), toFx(5)}));  // touching edges do not overlap

    const Vec2 origin{toFx(100), 0};
    const Box local{toFx(20), toFx(50), toFx(30), toFx(10)};
    const Box worldR = localToWorld(local, origin, +1);
    const Box worldL = localToWorld(local, origin, -1);
    CHECK(worldR.x == toFx(120));
    CHECK(worldL.x == toFx(50));  // mirrored: spans 50..80, in front of a left-facing fighter
    const Box back = worldToLocal(worldL, origin, -1);
    CHECK(back.x == local.x && back.y == local.y && back.w == local.w && back.h == local.h);
}

TEST(arc_contains_points) {
    const ArcRegion arc{0, 0, toFx(20), toFx(100), 0, 90};
    CHECK(pointInArc(arc, toFx(50), toFx(50)));
    CHECK(!pointInArc(arc, toFx(10), toFx(5)));    // inside the inner radius
    CHECK(!pointInArc(arc, toFx(90), toFx(90)));   // past the outer radius
    CHECK(!pointInArc(arc, toFx(-50), toFx(10)));  // outside the angle range
    CHECK(!pointInArc(arc, toFx(50), toFx(-10)));
    const ArcRegion wide{0, 0, 0, toFx(100), -135, 135};  // > 180 degree sweep
    CHECK(pointInArc(wide, toFx(-50), toFx(-10)) == false);
    CHECK(pointInArc(wide, toFx(-10), toFx(50)));
}

TEST(arc_overlaps_box_all_cases) {
    const ArcRegion arc{0, 0, toFx(40), toFx(100), -30, 60};
    CHECK(arcOverlapsBox(arc, {toFx(60), toFx(-5), toFx(10), toFx(10)}));     // box inside arc
    CHECK(arcOverlapsBox(arc, {toFx(-200), toFx(-200), toFx(400), toFx(400)}));  // arc inside box
    CHECK(arcOverlapsBox(arc, {toFx(95), toFx(-5), toFx(40), toFx(10)}));     // straddles outer edge
    CHECK(arcOverlapsBox(arc, {toFx(30), toFx(30), toFx(4), toFx(200)}));     // thin box crossing both arcs
    CHECK(!arcOverlapsBox(arc, {toFx(-10), toFx(-10), toFx(20), toFx(20)}));  // inside the hole
    CHECK(!arcOverlapsBox(arc, {toFx(-100), toFx(0), toFx(30), toFx(30)}));   // behind
    CHECK(!arcOverlapsBox(arc, {toFx(10), toFx(80), toFx(10), toFx(10)}));    // above the sweep
    CHECK(!arcOverlapsBox(arc, {toFx(120), toFx(0), toFx(10), toFx(10)}));    // out of reach
}

// ---------------------------------------------------------------- state machine & combat

TEST(state_machine_movement_states) {
    Match m;
    setup(m, 400);
    const auto& f = m.state().f[0];
    m.step(IN_RIGHT, 0);
    CHECK(f.state == FState::WalkForward);
    m.step(IN_LEFT, 0);
    CHECK(f.state == FState::WalkBackward);
    m.step(IN_DOWN, 0);
    CHECK(f.state == FState::Crouch);
    m.step(IN_UP, 0);
    CHECK(f.state == FState::JumpSquat);
    CHECK(stepUntil(m, 0, 0, 10, [&] { return f.state == FState::Airborne; }) == moves().fighter.jumpSquat);
    CHECK(stepUntil(m, 0, 0, 120, [&] { return f.state == FState::Landing; }) > 0);
    CHECK(stepUntil(m, 0, 0, 10, [&] { return f.state == FState::Idle; }) > 0);
    CHECK(Match::illegalTransitions == 0);
}

TEST(jab_hits_on_first_active_frame) {
    Match m;
    setup(m, 90);
    const MoveData& jab = moves().moves[moves().find("jab")];
    m.step(IN_LP, 0);
    CHECK(m.state().f[0].state == FState::Attack);
    int frames = stepUntil(m, 0, 0, 30, [&] { return sawEvent(m, EventType::Hit, 0); });
    CHECK(frames == jab.startup);
    CHECK(m.state().f[1].state == FState::Hitstun);
    CHECK(m.state().f[0].hitstop == jab.hitstop && m.state().f[1].hitstop == jab.hitstop);
    CHECK(m.state().f[1].health == moves().fighter.health - jab.damage);
}

TEST(blocking_requires_holding_away_and_correct_height) {
    {
        Match m;  // standing block stops a mid
        setup(m, 90, 6, 0);
        m.step(IN_LP, IN_RIGHT);  // p2 is on the right, so right is "away"
        stepUntil(m, 0, IN_RIGHT, 30, [&] { return m.events().count > 0; });
        CHECK(sawEvent(m, EventType::Block));
        CHECK(m.state().f[1].state == FState::Blockstun);
    }
    {
        Match m;  // a low must be blocked crouching
        setup(m, 90, 6, 0);
        m.step(IN_DOWN | IN_LK, IN_RIGHT);
        stepUntil(m, IN_DOWN, IN_RIGHT, 30, [&] { return m.events().count > 0; });
        CHECK(sawEvent(m, EventType::Hit));
    }
    {
        Match m;  // an overhead beats a crouching block
        setup(m, 90, 6, 0);
        m.step(IN_RIGHT, 0);
        m.step(IN_RIGHT | IN_HK, IN_RIGHT | IN_DOWN);
        stepUntil(m, 0, IN_RIGHT | IN_DOWN, 40, [&] { return m.events().count > 0 && (sawEvent(m, EventType::Hit) || sawEvent(m, EventType::Block)); });
        CHECK(sawEvent(m, EventType::Hit));
    }
}

TEST(cross_up_guard_direction_and_facing_check) {
    // p1 has jumped just past p2 and is still facing right, so p2 is behind it.
    enum { None, Hit, Blocked };
    auto run = [&](InputBits attack, InputBits defend, const MoveSet* override) {
        Match m;
        setup(m, 200, 6, 0);
        if (override) m.setMoves(*override);
        auto& s = m.mutableState();
        s.f[0].pos = {s.f[1].pos.x + toFx(30), toFx(120)};
        s.f[0].air = 1;
        s.f[0].state = FState::Airborne;
        s.f[0].facing = 1;
        s.f[0].vel = {0, -toFx(2)};
        m.step(attack, defend);
        for (int i = 0; i < 12; ++i) {
            m.step(0, defend);
            if (sawEvent(m, EventType::Hit)) return Hit;
            if (sawEvent(m, EventType::Block)) return Blocked;
        }
        return None;
    };
    // The attacker is now on p2's right: holding the old "back" (right) gets hit.
    CHECK(run(IN_LK, IN_RIGHT, nullptr) == Hit);
    CHECK(run(IN_LK, IN_LEFT, nullptr) == Blocked);
    // Same kick flagged front-only can't connect with someone behind the attacker.
    MoveSet frontOnly = moves();
    frontOnly.moves[frontOnly.find("jump_kick")].frontOnly = true;
    CHECK(run(IN_LK, IN_RIGHT, &frontOnly) == None);
}

TEST(knockback_pushes_away_from_attacker) {
    for (int side : {+1, -1}) {
        Match m;
        setup(m, 90);
        auto& s = m.mutableState();
        if (side < 0) {  // swap sides
            std::swap(s.f[0].pos.x, s.f[1].pos.x);
            m.step(0, 0);
        }
        const fx before = m.state().f[1].pos.x;
        m.step(IN_HP, 0);
        stepUntil(m, 0, 0, 40, [&] { return m.state().f[1].state == FState::Hitstun; });
        stepUntil(m, 0, 0, 30, [&] { return m.state().f[1].slide == 0; });
        const fx moved = m.state().f[1].pos.x - before;
        CHECK(sign(moved) == side);
        CHECK(fxAbs(moved) > toFx(15));
    }
}

TEST(corner_pushback_moves_attacker) {
    Match m;
    setup(m, 90);
    auto& s = m.mutableState();
    const fx wall = Match::kStageHalfWidth - moves().fighter.pushWidth;
    s.f[1].pos.x = wall;
    s.f[0].pos.x = wall - toFx(90);
    const fx before = s.f[0].pos.x;
    m.step(IN_HP, 0);
    stepUntil(m, 0, 0, 40, [&] { return m.state().f[0].slide != 0; });
    stepUntil(m, 0, 0, 40, [&] { return m.state().f[0].slide == 0; });
    CHECK(m.state().f[0].pos.x < before - toFx(10));
}

TEST(uppercut_arc_anti_airs_a_jump_in) {
    Match m;
    setup(m, 90);
    auto& s = m.mutableState();
    s.f[1].pos = {s.f[0].pos.x + toFx(60), toFx(110)};
    s.f[1].air = 1;
    s.f[1].state = FState::Airborne;
    s.f[1].airAttackUsed = 1;
    s.f[1].vel = {0, 0};
    m.step(IN_DOWN | IN_HP, 0);
    stepUntil(m, IN_DOWN, 0, 12, [&] { return sawEvent(m, EventType::Hit, 0); });
    CHECK(sawEvent(m, EventType::Hit, 0));
    CHECK(m.state().f[1].launched == 1);
}

TEST(special_move_from_quarter_circle) {
    Match m;
    setup(m, 120);
    m.step(IN_DOWN, 0);
    m.step(IN_DOWN | IN_RIGHT, 0);
    m.step(IN_RIGHT | IN_HP, 0);
    CHECK(m.state().f[0].state == FState::Attack);
    CHECK(m.state().f[0].move == moves().find("haymaker"));
}

// The headline input-buffer experiment. A two-hit chain is attempted with the
// follow-up pressed 0..5 frames after the starter's cancel window opens.
// Inside the 3-frame window it cancels; after that it only combos if the press
// survives until recovery ends (the buffer turns it into a link).
double comboSuccessRate(int buffer, bool print) {
    struct Chain {
        const char* name;
        InputBits hold, starter, follow;
    };
    const Chain chains[] = {
        {"jab > cross", 0, IN_LP, IN_HP},
        {"crouch jab > low kick", IN_DOWN, IN_LP, IN_LK},
        {"short kick > cross", 0, IN_LK, IN_HP},
    };
    int ok = 0, total = 0;
    for (const Chain& c : chains) {
        if (print) std::printf("      %-22s", c.name);
        for (int late = 0; late <= 5; ++late) {
            Match m;
            setup(m, 90, static_cast<uint8_t>(buffer));
            m.step(c.hold | c.starter, 0);
            stepUntil(m, c.hold, 0, 40, [&] { return sawEvent(m, EventType::Hit, 0); });
            while (m.state().f[0].hitstop > 0) m.step(c.hold, 0);  // window opens on the next frame
            for (int i = 0; i < late; ++i) m.step(c.hold, 0);
            uint16_t best = m.state().f[1].comboHits;
            m.step(c.hold | c.follow, 0);
            for (int i = 0; i < 60; ++i) {
                if (m.state().f[1].comboHits > best) best = m.state().f[1].comboHits;
                m.step(c.hold, 0);
            }
            const bool combo = best >= 2;
            ok += combo;
            ++total;
            if (print) std::printf(" %s", combo ? "Y" : ".");
        }
        if (print) std::printf("\n");
    }
    return 100.0 * ok / total;
}

TEST(input_buffer_improves_late_combo_inputs) {
    std::printf("      follow-up lateness (frames) 0 1 2 3 4 5\n");
    std::printf("    no buffer:\n");
    const double without = comboSuccessRate(0, true);
    std::printf("    6-frame buffer:\n");
    const double with = comboSuccessRate(6, true);
    std::printf("    combo success: %.0f%% without buffer -> %.0f%% with 6-frame buffer\n", without, with);
    CHECK(without == 50.0);
    CHECK(with == 100.0);
}

TEST(hot_reloaded_data_changes_behaviour) {
    std::string text;
    {
        FILE* f = std::fopen(dataPath().c_str(), "rb");
        CHECK(f != nullptr);
        if (!f) return;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        std::fclose(f);
    }
    const std::string from = "\"startup\": 4, \"active\": 2, \"recovery\": 7,\n      \"damage\": 40";
    const size_t at = text.find(from);
    CHECK(at != std::string::npos);
    if (at == std::string::npos) return;
    text.replace(at, from.size(), "\"startup\": 2, \"active\": 2, \"recovery\": 7,\n      \"damage\": 70");
    MoveSet tuned;
    std::string err;
    CHECK(parseMoveSet(text, tuned, err));
    CHECK(tuned.hash != moves().hash);

    Match m;
    setup(m, 90);
    m.setMoves(tuned);
    m.step(IN_LP, 0);
    CHECK(stepUntil(m, 0, 0, 30, [&] { return sawEvent(m, EventType::Hit, 0); }) == 2);
    CHECK(m.state().f[1].health == tuned.fighter.health - 70);
}

// ---------------------------------------------------------------- determinism

struct RandomPad {
    uint32_t rng;
    InputBits cur = 0;
    int hold = 0;
    InputBits next() {
        if (--hold <= 0) {
            rng ^= rng << 13;
            rng ^= rng >> 17;
            rng ^= rng << 5;
            cur = static_cast<InputBits>(rng & 0xFF);
            hold = 1 + static_cast<int>((rng >> 8) % 14);
        }
        return cur;
    }
};

TEST(replay_rebuilds_1M_frames_bit_exact) {
    const uint64_t kTarget = 1u << 20;  // 1,048,576 frames
    uint64_t frames = 0, sessions = 0, mismatches = 0, rounds = 0;
    uint16_t statesSeen[2] = {0, 0};
    const uint64_t illegalBefore = Match::illegalTransitions;
    const auto t0 = std::chrono::steady_clock::now();

    while (frames < kTarget) {
        MatchConfig cfg;
        cfg.bufferFrames = static_cast<uint8_t>((sessions % 3) * 3);  // 0, 3, 6
        cfg.training = sessions % 7 == 0;
        cfg.roundFrames = 40 * 60;
        Match m;
        m.reset(moves(), cfg);
        CpuBrain cpu[2];
        cpu[0].reset(static_cast<uint32_t>(sessions * 2 + 1), static_cast<int>(sessions % 4));
        cpu[1].reset(static_cast<uint32_t>(sessions * 2 + 2), static_cast<int>((sessions + 1) % 4));
        RandomPad pad{static_cast<uint32_t>(sessions * 7919 + 13)};
        Replay rec;
        rec.begin(cfg, moves().hash, 20000);
        int tail = 0;
        for (int i = 0; i < 20000 && tail < 30; ++i) {
            const InputBits p1 = (sessions & 1) ? pad.next() : cpu[0].think(m, 0);
            const InputBits p2 = cpu[1].think(m, 1);
            m.step(p1, p2);
            rec.record(p1, p2, m.hash());
            for (int k = 0; k < 2; ++k) statesSeen[k] |= static_cast<uint16_t>(1u << static_cast<int>(m.state().f[k].state));
            if (m.state().phase == Phase::MatchEnd) ++tail;
        }
        rounds += m.state().round;
        const VerifyResult v = verifyReplay(rec, moves());
        if (!v.ok) {
            ++mismatches;
            std::printf("    session %llu diverged at frame %lld\n", static_cast<unsigned long long>(sessions),
                        static_cast<long long>(v.firstMismatch));
        }
        frames += rec.frames();
        ++sessions;
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    int covered = 0;
    for (int s = 0; s < kStateCount; ++s) covered += ((statesSeen[0] | statesSeen[1]) >> s) & 1;
    std::printf("    %llu frames, %llu matches, %llu rounds, hashed and re-simulated in %.2fs\n",
                static_cast<unsigned long long>(frames), static_cast<unsigned long long>(sessions),
                static_cast<unsigned long long>(rounds), secs);
    std::printf("    per-frame hash mismatches: %llu, states visited: %d/%d, illegal transitions: %llu\n",
                static_cast<unsigned long long>(mismatches), covered, kStateCount,
                static_cast<unsigned long long>(Match::illegalTransitions - illegalBefore));
    CHECK(frames >= 1000000);
    CHECK(mismatches == 0);
    CHECK(covered == kStateCount);
    CHECK(Match::illegalTransitions == illegalBefore);
}

}  // namespace

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int ran = 0, failedTests = 0;
    for (const TestCase& t : registry()) {
        if (filter && !std::strstr(t.name, filter)) continue;
        const int before = g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        const bool ok = g_failures == before;
        failedTests += !ok;
        ++ran;
        std::printf("[ %s ] %s\n", ok ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d tests, %d checks, %d failed tests\n", ran, g_checks, failedTests);
    return failedTests == 0 ? 0 : 1;
}
