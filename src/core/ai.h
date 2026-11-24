#pragma once
// CPU opponent. Runs outside the simulation and only produces inputs, so
// matches against it replay exactly like human matches.
#include <cstdint>

#include "core/input.h"
#include "core/sim.h"

namespace sp {

class CpuBrain {
public:
    void reset(uint32_t seed, int level);  // level 0 (easy) .. 3 (hard)
    InputBits think(const Match& match, int self);

private:
    uint32_t next();
    int roll(int n) { return static_cast<int>(next() % static_cast<uint32_t>(n)); }
    bool chance(int percent) { return roll(100) < percent; }
    InputBits pressFor(const MoveSet& ms, int move, InputBits toward, InputBits away);
    int pickMove(const MoveSet& ms, int distPx, bool air);
    InputBits finish(InputBits out);

    uint32_t rng_ = 1;
    int level_ = 1;
    InputBits macro_[8] = {};
    int macroLen_ = 0;
    int macroPos_ = 0;
    InputBits lastOut_ = 0;
    InputBits holdDir_ = 0;
    int holdFrames_ = 0;
    int blockFrames_ = 0;
};

}  // namespace sp
