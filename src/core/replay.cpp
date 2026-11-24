#include "core/replay.h"

#include <fstream>
#include <iterator>

namespace sp {
namespace {

void putU(std::string& out, uint64_t v, int bytes) {
    for (int i = 0; i < bytes; ++i) out += static_cast<char>((v >> (8 * i)) & 0xFF);
}

struct Reader {
    const std::string& data;
    size_t pos = 0;
    bool ok = true;

    uint64_t get(int bytes) {
        if (pos + static_cast<size_t>(bytes) > data.size()) {
            ok = false;
            return 0;
        }
        uint64_t v = 0;
        for (int i = 0; i < bytes; ++i) v |= static_cast<uint64_t>(static_cast<uint8_t>(data[pos + i])) << (8 * i);
        pos += static_cast<size_t>(bytes);
        return v;
    }
};

}  // namespace

void Replay::begin(const MatchConfig& cfg, uint64_t hash, size_t reserveFrames) {
    config = cfg;
    movesHash = hash;
    inputs.clear();
    checksums.clear();
    inputs.reserve(reserveFrames * 2);
    checksums.reserve(reserveFrames);
}

bool Replay::save(const std::string& path, std::string& error) const {
    std::string out;
    out.reserve(32 + inputs.size() * 2 + checksums.size() * 8);
    putU(out, kMagic, 4);
    putU(out, kVersion, 4);
    putU(out, movesHash, 8);
    putU(out, config.bufferFrames, 1);
    putU(out, config.roundFrames, 2);
    putU(out, config.roundsToWin, 1);
    putU(out, config.training, 1);
    putU(out, frames(), 4);
    for (InputBits b : inputs) putU(out, b, 2);
    for (uint64_t c : checksums) putU(out, c, 8);

    std::ofstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot write " + path;
        return false;
    }
    f.write(out.data(), static_cast<std::streamsize>(out.size()));
    return static_cast<bool>(f);
}

bool Replay::load(const std::string& path, std::string& error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        error = "cannot open " + path;
        return false;
    }
    const std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    Reader r{data};
    if (r.get(4) != kMagic) {
        error = path + " is not a Southpaw replay";
        return false;
    }
    if (r.get(4) != kVersion) {
        error = path + " has an unsupported replay version";
        return false;
    }
    movesHash = r.get(8);
    config.bufferFrames = static_cast<uint8_t>(r.get(1));
    config.roundFrames = static_cast<uint16_t>(r.get(2));
    config.roundsToWin = static_cast<uint8_t>(r.get(1));
    config.training = static_cast<uint8_t>(r.get(1));
    const uint64_t n = r.get(4);
    if (!r.ok || data.size() - r.pos != n * (2 * 2 + 8)) {
        error = path + " is truncated or corrupt";
        return false;
    }
    inputs.resize(n * 2);
    checksums.resize(n);
    for (auto& b : inputs) b = static_cast<InputBits>(r.get(2));
    for (auto& c : checksums) c = r.get(8);
    return true;
}

VerifyResult verifyReplay(const Replay& replay, const MoveSet& moves) {
    VerifyResult res;
    Match match;
    match.reset(moves, replay.config);
    const size_t n = replay.frames();
    for (size_t i = 0; i < n; ++i) {
        match.step(replay.p1(i), replay.p2(i));
        const uint64_t h = match.hash();
        if (h != replay.checksums[i]) {
            res.firstMismatch = static_cast<int64_t>(i);
            res.frames = i + 1;
            res.finalHash = h;
            return res;
        }
    }
    res.ok = true;
    res.frames = n;
    res.finalHash = match.hash();
    return res;
}

}  // namespace sp
