// Headless simulation benchmark. Runs CPU-vs-CPU matches with no renderer and
// reports throughput plus heap allocations inside the timed loops.
//
//   southpaw_bench [frames]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "../tests/alloc_counter.h"
#include "core/ai.h"
#include "core/move_data.h"
#include "core/replay.h"
#include "core/sim.h"

using namespace sp;
using Clock = std::chrono::steady_clock;

namespace {

struct Result {
    double seconds;
    uint64_t allocs;
    uint64_t checksum;
};

void report(const char* label, uint64_t frames, const Result& r) {
    std::printf("  %-44s %9.0f frames/s  %7.1f ns/frame  allocs: %llu\n", label, frames / r.seconds,
                1e9 * r.seconds / frames, static_cast<unsigned long long>(r.allocs));
}

}  // namespace

int main(int argc, char** argv) {
    const uint64_t frames = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 2000000;
    MoveSet moves;
    std::string err;
    if (!loadMoveSet(std::string(SOUTHPAW_DATA_DIR) + "/moves.json", moves, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    MatchConfig cfg;
    std::vector<InputBits> inputs;
    std::vector<uint64_t> hashes;
    inputs.reserve(frames * 2);
    hashes.reserve(frames);

    std::printf("Southpaw headless benchmark: %llu frames, CPU vs CPU, 60 Hz fixed step\n",
                static_cast<unsigned long long>(frames));

    // 1. Full headless frame: both AIs think, the sim steps, state is hashed and recorded.
    Result full{};
    {
        Match m;
        m.reset(moves, cfg);
        CpuBrain a, b;
        a.reset(1234, 3);
        b.reset(5678, 2);
        const uint64_t allocBefore = sp_test::allocationCount();
        const auto t0 = Clock::now();
        for (uint64_t i = 0; i < frames; ++i) {
            const InputBits p1 = a.think(m, 0), p2 = b.think(m, 1);
            m.step(p1, p2);
            const uint64_t h = m.hash();
            inputs.push_back(p1);
            inputs.push_back(p2);
            hashes.push_back(h);
            if (m.state().phase == Phase::MatchEnd) m.reset(moves, cfg);
        }
        full.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        full.allocs = sp_test::allocationCount() - allocBefore;
        full.checksum = m.hash();
    }

    // 2. Replay: rebuild every match from recorded inputs alone.
    Result replay{};
    {
        Match m;
        m.reset(moves, cfg);
        const uint64_t allocBefore = sp_test::allocationCount();
        const auto t0 = Clock::now();
        for (uint64_t i = 0; i < frames; ++i) {
            m.step(inputs[i * 2], inputs[i * 2 + 1]);
            if (m.state().phase == Phase::MatchEnd) m.reset(moves, cfg);
        }
        replay.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        replay.allocs = sp_test::allocationCount() - allocBefore;
        replay.checksum = m.hash();
    }

    // 3. Replay with per-frame validation against the recorded state hashes.
    Result verify{};
    uint64_t mismatches = 0;
    {
        Match m;
        m.reset(moves, cfg);
        const uint64_t allocBefore = sp_test::allocationCount();
        const auto t0 = Clock::now();
        for (uint64_t i = 0; i < frames; ++i) {
            m.step(inputs[i * 2], inputs[i * 2 + 1]);
            mismatches += m.hash() != hashes[i];
            if (m.state().phase == Phase::MatchEnd) m.reset(moves, cfg);
        }
        verify.seconds = std::chrono::duration<double>(Clock::now() - t0).count();
        verify.allocs = sp_test::allocationCount() - allocBefore;
        verify.checksum = m.hash();
    }

    report("AI x2 + step + hash + record", frames, full);
    report("replay from inputs (step only)", frames, replay);
    report("replay + per-frame hash validation", frames, verify);
    std::printf("  final state hash: %016llx (live) / %016llx (replay)  mismatched frames: %llu\n",
                static_cast<unsigned long long>(full.checksum), static_cast<unsigned long long>(verify.checksum),
                static_cast<unsigned long long>(mismatches));
    std::printf("  real-time equivalent: %.0fx faster than 60 fps\n", frames / full.seconds / 60.0);
    return mismatches == 0 && full.allocs == 0 && replay.allocs == 0 && verify.allocs == 0 ? 0 : 1;
}
