#pragma once
// Deterministic match simulation. MatchState is plain data with fixed-size
// storage: stepping a frame never touches the heap, and the full state can be
// hashed every frame to validate replays.
#include <cstdint>

#include "core/combat.h"
#include "core/fixed.h"
#include "core/input.h"
#include "core/move_data.h"

namespace sp {

enum class FState : uint8_t {
    Idle,
    WalkForward,
    WalkBackward,
    Crouch,
    JumpSquat,
    Airborne,
    Landing,
    Attack,
    Blockstun,
    Hitstun,
    Knockdown,
    KO,
    Count
};
constexpr int kStateCount = static_cast<int>(FState::Count);
static_assert(kStateCount == 12, "fighter state machine has 12 states");

const char* stateName(FState s);
bool transitionAllowed(FState from, FState to);

struct Fighter {
    Vec2 pos;
    Vec2 vel;
    fx slide;  // decaying ground push from knockback, block push and lunges
    int8_t facing;  // +1 right, -1 left
    FState state;
    uint16_t stateFrame;
    int8_t move;  // index into MoveSet, -1 when not attacking
    uint8_t moveFrame;
    uint8_t connected;  // 0 whiff, 1 hit, 2 blocked
    uint8_t cancelTimer;
    uint8_t counterHit;
    uint8_t hitstop;
    uint16_t stun;
    int16_t health;
    uint16_t comboHits;  // hits taken in the current combo
    int16_t comboDamage;
    uint8_t juggle;
    uint8_t air;
    uint8_t launched;
    uint8_t invuln;
    int8_t jumpDir;
    uint8_t airAttackUsed;
    InputBuffer input;
};

struct MatchConfig {
    uint8_t bufferFrames = 6;
    uint16_t roundFrames = 99 * 60;
    uint8_t roundsToWin = 2;
    uint8_t training = 0;  // no timer, no KOs, health refills
};

enum class Phase : uint8_t { Intro, Fight, RoundEnd, MatchEnd };

enum class EventType : uint8_t { Hit, Block, Whiff, Jump, Land, Knockdown, KO, RoundStart, Fight, TimeUp, MatchOver };

struct Event {
    EventType type;
    uint8_t who;  // fighter that caused the event (attacker for hits)
    int8_t move;
    Vec2 at;
    int16_t damage;
    uint16_t combo;
    uint8_t counter;
};

struct FrameEvents {
    static constexpr int kMax = 16;
    Event list[kMax];
    int count = 0;
    void clear() { count = 0; }
    void push(const Event& e) {
        if (count < kMax) list[count++] = e;
    }
};

struct MatchState {
    uint32_t frame;
    Phase phase;
    uint16_t phaseTimer;
    uint16_t timer;  // frames left in the round
    uint8_t round;
    uint8_t wins[2];
    int8_t roundWinner;  // -1 none / draw
    int8_t matchWinner;
    Fighter f[2];
};

class Match {
public:
    static constexpr int kIntroFrames = 90;
    static constexpr int kRoundEndFrames = 150;
    static constexpr fx kStageHalfWidth = 900 * kFxOne;
    static constexpr fx kMaxSeparation = 760 * kFxOne;

    void reset(const MoveSet& moves, const MatchConfig& config);
    void step(InputBits p1, InputBits p2);

    // Swap in freshly loaded data (hot reload). Fighters mid-move fall back to idle
    // if their move no longer exists.
    void setMoves(const MoveSet& moves);

    const MatchState& state() const { return s_; }
    MatchState& mutableState() { return s_; }
    const FrameEvents& events() const { return events_; }
    const MoveSet& moves() const { return *moves_; }
    const MatchConfig& config() const { return config_; }
    void setBufferFrames(uint8_t frames) { config_.bufferFrames = frames; }
    uint64_t hash() const;

    // Geometry queries, used by hit detection, tests and the debug overlay.
    Box hurtbox(int who) const;
    bool activeArc(int who, ArcRegion& localArc) const;  // only for Arc hit shapes
    bool activeBox(int who, Box& localBox) const;        // only for Box hit shapes
    bool isActionable(int who) const;

    static uint64_t illegalTransitions;  // debug counter, checked by the tests

private:
    void startRound();
    void updateFighter(int i);
    void groundControl(int i);
    bool tryStartMove(int i, const int8_t* allowed, int allowedCount);
    void startMove(int i, int move);
    void setState(Fighter& f, FState to);
    void physics(int i, fx& movedX);
    void onLand(int i);
    void separate(const fx movedX[2]);
    void updateFacing();
    void resolveHits();
    bool canBlock(int defender, int attacker, const MoveData& m) const;
    void applyHit(int attacker, int defender, bool blocked, int moveIdx, Vec2 at);
    void endRound(int winner);
    bool atWall(const Fighter& f) const;

    const MoveSet* moves_ = nullptr;
    MatchConfig config_{};
    MatchState s_{};
    FrameEvents events_{};
};

uint64_t hashMatchState(const MatchState& s);

}  // namespace sp
