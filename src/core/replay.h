#pragma once
// Deterministic replays: a match is fully described by its config, the move
// data hash and both players' inputs. Per-frame state hashes are stored
// alongside so playback can prove it reproduced the exact same match.
#include <cstdint>
#include <string>
#include <vector>

#include "core/input.h"
#include "core/move_data.h"
#include "core/sim.h"

namespace sp {

struct Replay {
    static constexpr uint32_t kMagic = 0x50525053;  // "SPRP"
    static constexpr uint32_t kVersion = 1;

    MatchConfig config{};
    uint64_t movesHash = 0;
    std::vector<InputBits> inputs;    // two entries per frame (p1, p2)
    std::vector<uint64_t> checksums;  // state hash after each frame

    // Reserves storage up front so recording never allocates inside the game loop.
    void begin(const MatchConfig& cfg, uint64_t hash, size_t reserveFrames);
    void record(InputBits p1, InputBits p2, uint64_t stateHash) {
        inputs.push_back(p1);
        inputs.push_back(p2);
        checksums.push_back(stateHash);
    }
    size_t frames() const { return checksums.size(); }
    InputBits p1(size_t frame) const { return inputs[frame * 2]; }
    InputBits p2(size_t frame) const { return inputs[frame * 2 + 1]; }

    bool save(const std::string& path, std::string& error) const;
    bool load(const std::string& path, std::string& error);
};

struct VerifyResult {
    bool ok = false;
    uint64_t frames = 0;
    int64_t firstMismatch = -1;  // frame index of first divergence, -1 if none
    uint64_t finalHash = 0;
};

// Rebuilds the match from inputs alone and compares every frame's state hash.
VerifyResult verifyReplay(const Replay& replay, const MoveSet& moves);

}  // namespace sp
