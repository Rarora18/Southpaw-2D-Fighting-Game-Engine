#pragma once
// Move and fighter data, loaded from data/moves.json at startup and hot-reloaded
// at runtime. All values are converted to fixed point at load time.
#include <cstdint>
#include <string>

#include "core/fixed.h"

namespace sp {

constexpr int kMaxMoves = 32;
constexpr int kMaxCancels = 8;
constexpr int kMaxNameLen = 24;

enum class Guard : uint8_t { Mid, High, Low };
enum class Stance : uint8_t { Stand, Crouch, Air };
enum class Shape : uint8_t { Box, Arc };
enum class DirReq : uint8_t { None, Forward, Back };

enum class Anim : uint8_t {
    Jab, Cross, Uppercut, ShortKick, Roundhouse, AxeKick,
    CrouchJab, LowKick, Sweep, JumpPunch, JumpKick, Haymaker, Count
};
const char* animName(Anim a);

// Hit shapes are authored in attacker-local space: facing right, y-up, origin
// at the feet. They are mirrored for a left-facing attacker.
struct HitShape {
    Shape shape = Shape::Box;
    Box box{};
    // Arc: annular sector swept from angle a0 to a1 (degrees, CCW from +x)
    // across the active frames. Each active frame covers only the slice the
    // limb travels through that frame.
    fx cx = 0, cy = 0, r0 = 0, r1 = 0;
    int16_t a0 = 0, a1 = 0;
};

struct MoveData {
    char name[kMaxNameLen] = {};
    Anim anim = Anim::Jab;
    Stance stance = Stance::Stand;
    uint8_t button = 0;     // 0 LP, 1 HP, 2 LK, 3 HK
    DirReq dir = DirReq::None;
    bool motion = false;    // requires quarter-circle forward
    uint8_t startup = 1;    // frames before the first active frame
    uint8_t active = 1;
    uint8_t recovery = 1;
    int16_t damage = 0;
    int16_t chip = 0;
    uint8_t hitstun = 0;
    uint8_t blockstun = 0;
    uint8_t hitstop = 0;
    fx knockbackX = 0;
    fx knockbackY = 0;
    fx blockPush = 0;
    Guard guard = Guard::Mid;
    bool knockdown = false;
    bool frontOnly = true;  // only connects with targets in front of the attacker
    fx lunge = 0;
    HitShape hit{};
    uint8_t cancelWindow = 0;  // frames after connecting (post-hitstop) that cancels are open
    uint8_t cancelCount = 0;
    int8_t cancels[kMaxCancels] = {};

    int total() const { return startup + active + recovery; }
};

struct FighterData {
    int16_t health = 1000;
    fx walkForward = 0, walkBackward = 0;
    fx jumpVelocity = 0, jumpForward = 0, gravity = 0;
    uint8_t jumpSquat = 4, landing = 3, knockdownFrames = 40, wakeupInvuln = 8, maxJuggle = 3;
    fx pushWidth = 0;  // half-width of the push box
    Box hurtStand{}, hurtCrouch{}, hurtAir{};
};

struct MoveSet {
    FighterData fighter{};
    MoveData moves[kMaxMoves];
    int count = 0;
    uint64_t hash = 0;  // hash of the parsed data; stored in replays

    int find(const char* name) const;
};

bool parseMoveSet(const std::string& text, MoveSet& out, std::string& error);
bool loadMoveSet(const std::string& path, MoveSet& out, std::string& error);

}  // namespace sp
