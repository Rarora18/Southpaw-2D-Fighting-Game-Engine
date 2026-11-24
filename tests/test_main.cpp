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
