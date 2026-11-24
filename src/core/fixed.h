#pragma once
// Fixed-point math for the simulation. Everything that feeds the per-frame
// state hash is integer so replays are bit-identical across runs and builds.
#include <cstdint>

namespace sp {

using fx = int32_t;                 // 24.8 fixed point, 1 unit = 1/256 px
constexpr int kFxShift = 8;
constexpr fx kFxOne = 1 << kFxShift;

constexpr fx toFx(int v) { return v * kFxOne; }
inline fx fxFromDouble(double d) { return static_cast<fx>(d >= 0 ? d * kFxOne + 0.5 : d * kFxOne - 0.5); }
constexpr float fxToFloat(fx v) { return static_cast<float>(v) / kFxOne; }
constexpr fx fxScale(fx v, int num, int den) { return static_cast<fx>(static_cast<int64_t>(v) * num / den); }

constexpr int sign(fx v) { return (v > 0) - (v < 0); }
constexpr fx fxAbs(fx v) { return v < 0 ? -v : v; }
constexpr fx fxMin(fx a, fx b) { return a < b ? a : b; }
constexpr fx fxMax(fx a, fx b) { return a > b ? a : b; }
constexpr fx fxClamp(fx v, fx lo, fx hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct Vec2 {
    fx x = 0;
    fx y = 0;
};

// Axis-aligned box. y-up world, (x, y) is the bottom-left corner.
struct Box {
    fx x = 0;
    fx y = 0;
    fx w = 0;
    fx h = 0;
};

}  // namespace sp
