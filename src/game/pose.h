#pragma once
// Procedural skeletal animation. Poses are derived purely from simulation
// state (state, move, frame counters), so replays animate identically.
#include "core/sim.h"
#include "raylib.h"

namespace sp {

// Joint positions in fighter-local pixels: facing right, y up, origin at the feet.
struct Skeleton {
    Vector2 hip, neck, shoulder, head;
    Vector2 frontElbow, frontHand, backElbow, backHand;
    Vector2 frontKnee, frontFoot, backKnee, backFoot;
    float headRadius;
    float lean;  // degrees, for the headband direction
};

Skeleton buildSkeleton(const Match& match, int who, float time);

}  // namespace sp
