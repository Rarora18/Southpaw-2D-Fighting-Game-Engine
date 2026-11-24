#pragma once
// Collision primitives for hit detection. Pure integer math: no floats touch
// anything that affects the simulation.
#include <cstdint>

#include "core/fixed.h"

namespace sp {

constexpr int kDirScale = 4096;  // unit vectors from dirFromDegrees are scaled by this

// Integer unit vector for an angle in degrees (CCW from +x).
void dirFromDegrees(int degrees, int32_t& dx, int32_t& dy);

bool boxesOverlap(const Box& a, const Box& b);

// Annular sector centred at (cx, cy) with radii [r0, r1], covering angles
// [lo, hi] (degrees, lo <= hi, hi - lo <= 360).
struct ArcRegion {
    fx cx, cy, r0, r1;
    int lo, hi;
};

bool pointInArc(const ArcRegion& arc, int64_t px, int64_t py);
bool arcOverlapsBox(const ArcRegion& arc, const Box& box);

// Mirror a world-space box into an attacker's local frame (facing right,
// origin at the attacker's feet).
Box worldToLocal(const Box& world, Vec2 origin, int facing);
// Inverse: local-space box to world space.
Box localToWorld(const Box& local, Vec2 origin, int facing);

}  // namespace sp
