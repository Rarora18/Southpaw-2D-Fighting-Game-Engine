#include "game/pose.h"

#include <algorithm>
#include <cmath>

namespace sp {
namespace {

constexpr float kTorso = 60, kUpperArm = 32, kForearm = 31, kThigh = 44, kShin = 44, kHead = 15;
constexpr float kAirHipHeight = 80;
constexpr float kDeg = 3.14159265f / 180.0f;

// Limb angles are degrees from straight down, positive rotating toward the
// fighter's front. Lean is the torso's tilt from vertical (positive = forward).
struct Pose {
    float lean;
    float frontSh, frontEl, backSh, backEl;
    float frontHip, frontKnee, backHip, backKnee;
    float bodyRot;  // whole-body rotation about the hip (positive = falling backwards)
};

Pose lerp(const Pose& a, const Pose& b, float t) {
    auto l = [t](float x, float y) { return x + (y - x) * t; };
    return {l(a.lean, b.lean),         l(a.frontSh, b.frontSh),   l(a.frontEl, b.frontEl),
            l(a.backSh, b.backSh),     l(a.backEl, b.backEl),     l(a.frontHip, b.frontHip),
            l(a.frontKnee, b.frontKnee), l(a.backHip, b.backHip), l(a.backKnee, b.backKnee),
            l(a.bodyRot, b.bodyRot)};
}

float smooth(float t) {
    t = t < 0 ? 0 : (t > 1 ? 1 : t);
    return t * t * (3 - 2 * t);
}

const Pose kStance{6, 40, 100, 25, 120, 16, -16, -16, -6, 0};
const Pose kCrouch{38, 55, 95, 40, 110, 75, -100, 20, -95, 0};
const Pose kAir{5, 60, 90, 30, 110, 80, -110, 30, -100, 0};
const Pose kSquat{14, 45, 95, 30, 115, 45, -75, -5, -60, 0};
const Pose kBlock{-4, 72, 112, 58, 122, 20, -14, -24, -6, 0};
const Pose kHit{-24, 18, 40, -12, 50, 10, -22, -24, -8, 0};
const Pose kLying{0, 160, 20, 140, 30, 10, -10, -5, -20, 92};

Pose withArms(Pose legs, const Pose& arms) {
    legs.frontSh = arms.frontSh;
    legs.frontEl = arms.frontEl;
    legs.backSh = arms.backSh;
    legs.backEl = arms.backEl;
    return legs;
}

// Windup, strike (first active frame) and follow-through (last active frame).
void attackKeys(Anim anim, const Pose& base, Pose& wind, Pose& strike, Pose& end) {
    wind = strike = base;
    switch (anim) {
        case Anim::Jab:
            wind.frontSh = 55; wind.frontEl = 75;
            strike.lean = 10; strike.frontSh = 90; strike.frontEl = 0;
            end = strike;
            break;
        case Anim::Cross:
            wind.backSh = 45; wind.backEl = 100; wind.lean = 0;
            strike.lean = 22; strike.backSh = 90; strike.backEl = 0; strike.frontSh = 35; strike.frontEl = 120;
            strike.frontHip = 32; strike.backHip = -26; strike.backKnee = 0;
            end = strike;
            break;
        case Anim::Uppercut:
            wind = kCrouch; wind.backSh = 10; wind.backEl = 60; wind.lean = 30;
            strike = kStance; strike.lean = 4; strike.backSh = 105; strike.backEl = 35; strike.frontSh = 30;
            strike.frontEl = 110; strike.frontHip = 25; strike.frontKnee = -30;
            end = strike; end.lean = -10; end.backSh = 178; end.backEl = 8; end.backHip = -8; end.backKnee = 0;
            break;
        case Anim::ShortKick:
            wind.frontHip = 55; wind.frontKnee = -95; wind.lean = -4;
            strike.frontHip = 92; strike.frontKnee = -4; strike.lean = -12; strike.backHip = -8; strike.backKnee = -2;
            end = strike;
            break;
        case Anim::Roundhouse:
            wind.frontHip = 40; wind.frontKnee = -100; wind.lean = -10; wind.backHip = -6; wind.backKnee = 0;
            strike.frontHip = 70; strike.frontKnee = 0; strike.lean = -18; strike.backHip = -10; strike.backKnee = 0;
            strike.frontSh = 20; strike.frontEl = 90;
            end = strike; end.frontHip = 145; end.frontKnee = -10; end.lean = -28;
            break;
        case Anim::AxeKick:
            wind.frontHip = 120; wind.frontKnee = -70; wind.lean = -14; wind.backHip = -6; wind.backKnee = 0;
            strike.frontHip = 205; strike.frontKnee = 0; strike.lean = -20; strike.backHip = -8; strike.backKnee = 0;
            end = strike; end.frontHip = 95; end.lean = 12;
            break;
        case Anim::CrouchJab:
            wind.frontSh = 60; wind.frontEl = 70;
            strike.frontSh = 90; strike.frontEl = 0; strike.lean = 30;
            end = strike;
            break;
        case Anim::LowKick:
            wind.frontHip = 50; wind.frontKnee = -110;
            strike.frontHip = 75; strike.frontKnee = 0; strike.lean = 30; strike.backHip = 30; strike.backKnee = -115;
            end = strike;
            break;
        case Anim::Sweep:
            wind.lean = 50; wind.frontHip = 40; wind.frontKnee = -120;
            strike.lean = 48; strike.frontHip = 82; strike.frontKnee = 0; strike.backHip = 40; strike.backKnee = -140;
            strike.frontSh = 70; strike.frontEl = 60;
            end = strike; end.frontHip = 92;
            break;
        case Anim::JumpPunch:
            wind.frontSh = 110; wind.frontEl = 80;
            strike.frontSh = 48; strike.frontEl = 0; strike.lean = 18;
            end = strike;
            break;
        case Anim::JumpKick:
            wind.frontHip = 70; wind.frontKnee = -120;
            strike.frontHip = 55; strike.frontKnee = 0; strike.backHip = -10; strike.backKnee = -100; strike.lean = -12;
            end = strike;
            break;
        case Anim::Haymaker:
            wind.backSh = -50; wind.backEl = 60; wind.lean = -16; wind.frontSh = 60; wind.frontEl = 90;
            strike.lean = 32; strike.backSh = 95; strike.backEl = 0; strike.frontSh = 20; strike.frontEl = 100;
            strike.frontHip = 48; strike.frontKnee = -55; strike.backHip = -36; strike.backKnee = 0;
            end = strike;
            break;
        case Anim::Count:
            end = base;
            break;
    }
}

Pose attackPose(const MoveData& m, const Pose& base, int frame) {
    Pose wind, strike, end;
    attackKeys(m.anim, base, wind, strike, end);
    if (frame < m.startup) {
        const float t = static_cast<float>(frame + 1) / m.startup;
        return t < 0.6f ? lerp(base, wind, smooth(t / 0.6f)) : lerp(wind, strike, smooth((t - 0.6f) / 0.4f));
    }
    if (frame < m.startup + m.active) {
        const float t = static_cast<float>(frame - m.startup + 1) / m.active;
        return lerp(strike, end, t);
    }
    const float t = static_cast<float>(frame - m.startup - m.active + 1) / m.recovery;
    return lerp(end, base, smooth(t));
}

Vector2 limbDir(float deg) { return {std::sin(deg * kDeg), -std::cos(deg * kDeg)}; }
Vector2 add(Vector2 a, Vector2 b, float s) { return {a.x + b.x * s, a.y + b.y * s}; }

Vector2 rotateAbout(Vector2 p, Vector2 c, float deg) {
    const float s = std::sin(deg * kDeg), co = std::cos(deg * kDeg);
    const float x = p.x - c.x, y = p.y - c.y;
    return {c.x + x * co - y * s, c.y + x * s + y * co};
}

}  // namespace

Skeleton buildSkeleton(const Match& match, int who, float time) {
    const Fighter& f = match.state().f[who];
    const MoveSet& ms = match.moves();
    const float sf = static_cast<float>(f.stateFrame);
    const bool crouchHeld = (f.input.current() & IN_DOWN) != 0;
    Pose p = kStance;

    switch (f.state) {
        case FState::Idle: {
            const float bob = std::sin(time * 4.2f + who * 1.7f);
            p.lean += bob * 1.5f;
            p.frontKnee -= 4 + bob * 4;
            p.backKnee -= 4 + bob * 4;
            p.frontSh += bob * 3;
            break;
        }
        case FState::WalkForward:
        case FState::WalkBackward: {
            const float ph = sf * (f.state == FState::WalkForward ? 0.22f : -0.2f);
            p.frontHip = 16 + 16 * std::sin(ph);
            p.backHip = -16 - 16 * std::sin(ph);
            p.frontKnee = -16 - 14 * std::max(0.0f, std::cos(ph));
            p.backKnee = -6 - 14 * std::max(0.0f, -std::cos(ph));
            p.lean = f.state == FState::WalkForward ? 10 : 2;
            break;
        }
        case FState::Crouch:
            p = kCrouch;
            p.lean += std::sin(time * 4.0f) * 1.0f;
            break;
        case FState::JumpSquat:
        case FState::Landing:
            p = kSquat;
            break;
        case FState::Airborne: {
            const float rise = fxToFloat(f.vel.y);
            p = lerp(kAir, kStance, rise > 6 ? 0.35f : 0.0f);
            if (f.jumpDir != 0) p.bodyRot = -f.jumpDir * std::sin(std::min(sf / 38.0f, 1.0f) * 3.14159f) * 40.0f;
            break;
        }
        case FState::Attack: {
            if (f.move >= 0 && f.move < ms.count) {
                const MoveData& m = ms.moves[f.move];
                const Pose& base = m.stance == Stance::Crouch ? kCrouch : (m.stance == Stance::Air ? kAir : kStance);
                p = attackPose(m, base, f.moveFrame);
            }
            break;
        }
        case FState::Blockstun:
            p = crouchHeld ? withArms(kCrouch, kBlock) : kBlock;
            p.lean -= 4;
            break;
        case FState::Hitstun:
            if (f.air) {
                p = kHit;
                p.frontHip = 40; p.frontKnee = -60; p.backHip = 10; p.backKnee = -50;
                p.bodyRot = std::min(70.0f, sf * 6.0f);
            } else {
                const float t = std::min(1.0f, sf / 4.0f);
                p = lerp(kHit, kStance, f.stun < 6 ? 1 - f.stun / 6.0f : 0) ;
                p.lean = kHit.lean * (1 - 0.3f * t) + (f.counterHit ? -8 : 0);
            }
            break;
        case FState::Knockdown: {
            const float total = ms.fighter.knockdownFrames;
            const float rise = sf > total - 14 ? (sf - (total - 14)) / 14.0f : 0;
            p = lerp(kLying, kCrouch, smooth(rise));
            break;
        }
        case FState::KO:
            p = f.air ? kHit : kLying;
            if (f.air) p.bodyRot = std::min(80.0f, sf * 5.0f);
            break;
        case FState::Count:
            break;
    }

    Skeleton s{};
    s.lean = p.lean;
    s.headRadius = kHead;
    const Vector2 up{std::sin(p.lean * kDeg), std::cos(p.lean * kDeg)};
    s.hip = {0, 0};
    s.neck = add(s.hip, up, kTorso);
    s.shoulder = add(s.hip, up, kTorso - 7);
    s.head = add(s.neck, up, kHead + 3);
    s.frontElbow = add(s.shoulder, limbDir(p.frontSh), kUpperArm);
    s.frontHand = add(s.frontElbow, limbDir(p.frontSh + p.frontEl), kForearm);
    s.backElbow = add(s.shoulder, limbDir(p.backSh), kUpperArm);
    s.backHand = add(s.backElbow, limbDir(p.backSh + p.backEl), kForearm);
    s.frontKnee = add(s.hip, limbDir(p.frontHip), kThigh);
    s.frontFoot = add(s.frontKnee, limbDir(p.frontHip + p.frontKnee), kShin);
    s.backKnee = add(s.hip, limbDir(p.backHip), kThigh);
    s.backFoot = add(s.backKnee, limbDir(p.backHip + p.backKnee), kShin);

    Vector2* pts[] = {&s.neck, &s.shoulder, &s.head, &s.frontElbow, &s.frontHand, &s.backElbow,
                      &s.backHand, &s.frontKnee, &s.frontFoot, &s.backKnee, &s.backFoot};
    if (p.bodyRot != 0)
        for (Vector2* v : pts) *v = rotateAbout(*v, s.hip, p.bodyRot);

    // Plant the lowest point on the floor when grounded; hang from the hip in the air.
    float offset;
    if (f.air) {
        offset = kAirHipHeight;
    } else {
        float lowest = std::min(s.frontFoot.y, s.backFoot.y);
        if (p.bodyRot > 30) {
            lowest = std::min(lowest, s.head.y - kHead);
            for (Vector2* v : pts) lowest = std::min(lowest, v->y - 6);
        }
        offset = -lowest;
    }
    s.hip.y += offset;
    for (Vector2* v : pts) v->y += offset;
    return s;
}

}  // namespace sp
