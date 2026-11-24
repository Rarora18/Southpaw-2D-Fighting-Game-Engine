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
