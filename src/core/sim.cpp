#include "core/sim.h"

#include "core/hash.h"

namespace sp {

uint64_t Match::illegalTransitions = 0;

namespace {

constexpr uint16_t bit(FState s) { return static_cast<uint16_t>(1u << static_cast<int>(s)); }

// States a fighter can leave into once it is back in control on the ground.
constexpr uint16_t kControl = bit(FState::Idle) | bit(FState::WalkForward) | bit(FState::WalkBackward) |
                              bit(FState::Crouch) | bit(FState::JumpSquat) | bit(FState::Attack);
constexpr uint16_t kDamaged = bit(FState::Hitstun) | bit(FState::KO);

// Transition table for the 12-state machine. Every setState() is checked
// against it; the test suite asserts zero violations over 1M+ random frames.
constexpr uint16_t kAllowed[kStateCount] = {
    /* Idle         */ kControl | bit(FState::Blockstun) | kDamaged,
    /* WalkForward  */ kControl | bit(FState::Blockstun) | kDamaged,
    /* WalkBackward */ kControl | bit(FState::Blockstun) | kDamaged,
    /* Crouch       */ kControl | bit(FState::Blockstun) | kDamaged,
    /* JumpSquat    */ bit(FState::Airborne) | kDamaged,
    /* Airborne     */ bit(FState::Attack) | bit(FState::Landing) | kDamaged,
    /* Landing      */ kControl | kDamaged,
    /* Attack       */ kControl | bit(FState::Airborne) | bit(FState::Landing) | kDamaged,
    /* Blockstun    */ kControl | bit(FState::Blockstun) | kDamaged,
    /* Hitstun      */ kControl | bit(FState::Knockdown) | kDamaged,
    /* Knockdown    */ kControl,
    /* KO           */ 0,
};

const char* const kStateNames[kStateCount] = {
    "Idle", "WalkForward", "WalkBackward", "Crouch", "JumpSquat", "Airborne",
    "Landing", "Attack", "Blockstun", "Hitstun", "Knockdown", "KO",
};

bool reentrant(FState s) { return s == FState::Attack || s == FState::Blockstun || s == FState::Hitstun; }

constexpr fx kStartOffset = 170 * kFxOne;
constexpr fx kPushHeight = 90 * kFxOne;  // jumps that clear this can cross over
constexpr int kFrictionNum = 205;  // ground slide keeps 205/256 of its speed per frame
constexpr int kMotionWindow = 15;
constexpr int kMaxRounds = 9;

void hashFighter(Hasher& h, const Fighter& f) {
    h.add(f.pos.x); h.add(f.pos.y); h.add(f.vel.x); h.add(f.vel.y); h.add(f.slide);
    h.add(f.facing); h.add(f.state); h.add(f.stateFrame);
    h.add(f.move); h.add(f.moveFrame); h.add(f.connected); h.add(f.cancelTimer); h.add(f.counterHit);
    h.add(f.hitstop); h.add(f.stun); h.add(f.health); h.add(f.comboHits); h.add(f.comboDamage);
    h.add(f.juggle); h.add(f.air); h.add(f.launched); h.add(f.invuln); h.add(f.jumpDir); h.add(f.airAttackUsed);
    for (InputBits b : f.input.held) h.add(b);
    h.add(f.input.count);
    for (int32_t c : f.input.consumed) h.add(c);
}

}  // namespace

const char* stateName(FState s) {
    int i = static_cast<int>(s);
    return i >= 0 && i < kStateCount ? kStateNames[i] : "?";
}

bool transitionAllowed(FState from, FState to) {
    return (kAllowed[static_cast<int>(from)] & bit(to)) != 0;
}

uint64_t hashMatchState(const MatchState& s) {
    Hasher h;
    h.add(s.frame); h.add(s.phase); h.add(s.phaseTimer); h.add(s.timer); h.add(s.round);
    h.add(s.wins[0]); h.add(s.wins[1]); h.add(s.roundWinner); h.add(s.matchWinner);
    hashFighter(h, s.f[0]);
    hashFighter(h, s.f[1]);
    return h.h;
}

uint64_t Match::hash() const { return hashMatchState(s_); }

void Match::reset(const MoveSet& moves, const MatchConfig& config) {
    moves_ = &moves;
    config_ = config;
    s_ = MatchState{};
    s_.matchWinner = -1;
    s_.f[0].input.clear();
    s_.f[1].input.clear();
    events_.clear();
    startRound();
}

void Match::setMoves(const MoveSet& moves) {
    moves_ = &moves;
    for (Fighter& f : s_.f) {
        if (f.move >= moves.count) {
            f.move = -1;
            f.state = f.air ? FState::Airborne : FState::Idle;
            f.stateFrame = 0;
        }
        if (f.health > moves.fighter.health) f.health = moves.fighter.health;
    }
}

void Match::startRound() {
    ++s_.round;
    s_.phase = Phase::Intro;
    s_.phaseTimer = kIntroFrames;
    s_.timer = config_.roundFrames;
    s_.roundWinner = -1;
    for (int i = 0; i < 2; ++i) {
        Fighter& f = s_.f[i];
        const InputBuffer keep = f.input;
        f = Fighter{};
        f.input = keep;
        f.pos.x = i == 0 ? -kStartOffset : kStartOffset;
        f.facing = i == 0 ? 1 : -1;
        f.state = FState::Idle;
        f.move = -1;
        f.health = moves_->fighter.health;
    }
    events_.push({EventType::RoundStart, 0, -1, {}, 0, s_.round, 0});
}

void Match::step(InputBits p1, InputBits p2) {
    events_.clear();
    ++s_.frame;
    const bool control = s_.phase == Phase::Fight;
    s_.f[0].input.push(control ? p1 : 0);
    s_.f[1].input.push(control ? p2 : 0);

    if (s_.phase == Phase::Intro) {
        if (--s_.phaseTimer == 0) {
            s_.phase = Phase::Fight;
            events_.push({EventType::Fight, 0, -1, {}, 0, 0, 0});
        }
        return;
    }

    bool frozen[2];
    for (int i = 0; i < 2; ++i) {
        Fighter& f = s_.f[i];
        frozen[i] = f.hitstop > 0;
        if (frozen[i]) --f.hitstop;
        else updateFighter(i);
    }
    fx moved[2] = {0, 0};
    for (int i = 0; i < 2; ++i)
        if (!frozen[i]) physics(i, moved[i]);
    separate(moved);
    updateFacing();
    resolveHits();

    if (s_.phase == Phase::Fight) {
        if (config_.training) {
            for (Fighter& f : s_.f)
                if (f.comboHits == 0 && f.state != FState::Hitstun && f.state != FState::Knockdown)
                    f.health = moves_->fighter.health;
        } else if (s_.timer > 0 && --s_.timer == 0) {
            const int16_t h0 = s_.f[0].health, h1 = s_.f[1].health;
            endRound(h0 > h1 ? 0 : (h1 > h0 ? 1 : -1));
            events_.push({EventType::TimeUp, 0, -1, {}, 0, 0, 0});
        }
    } else if (s_.phase == Phase::RoundEnd && --s_.phaseTimer == 0) {
        const bool decided = s_.wins[0] >= config_.roundsToWin || s_.wins[1] >= config_.roundsToWin;
        if (decided || s_.round >= kMaxRounds) {
            s_.phase = Phase::MatchEnd;
            s_.matchWinner = s_.wins[0] == s_.wins[1] ? -1 : (s_.wins[0] > s_.wins[1] ? 0 : 1);
            events_.push({EventType::MatchOver, 0, -1, {}, 0, 0, 0});
        } else {
            startRound();
        }
    }
}

void Match::setState(Fighter& f, FState to) {
    if (f.state == to && !reentrant(to)) return;
    if (!transitionAllowed(f.state, to)) ++illegalTransitions;
    f.state = to;
    f.stateFrame = 0;
}

void Match::updateFighter(int i) {
    Fighter& f = s_.f[i];
    const FighterData& fd = moves_->fighter;
    if (f.stateFrame < 0xFFFF) ++f.stateFrame;
    if (f.invuln) --f.invuln;

    switch (f.state) {
        case FState::Idle:
        case FState::WalkForward:
        case FState::WalkBackward:
        case FState::Crouch:
            groundControl(i);
            break;
        case FState::JumpSquat:
            if (f.stateFrame >= fd.jumpSquat) {
                setState(f, FState::Airborne);
                f.air = 1;
                f.slide = 0;
                f.vel.y = fd.jumpVelocity;
                f.vel.x = f.jumpDir * f.facing * fd.jumpForward;
                events_.push({EventType::Jump, static_cast<uint8_t>(i), -1, f.pos, 0, 0, 0});
            }
            break;
        case FState::Airborne:
            if (!f.airAttackUsed) tryStartMove(i, nullptr, 0);
            break;
        case FState::Landing:
            if (f.stateFrame >= fd.landing) groundControl(i);
            break;
        case FState::Attack: {
            const MoveData& m = moves_->moves[f.move];
            if (f.cancelTimer > 0) {
                if (tryStartMove(i, m.cancels, m.cancelCount)) break;
                --f.cancelTimer;
            }
            ++f.moveFrame;
            if (f.moveFrame == m.startup)
                events_.push({EventType::Whiff, static_cast<uint8_t>(i), f.move, f.pos, 0, 0, 0});
            if (f.moveFrame >= m.total()) {
                f.move = -1;
                f.cancelTimer = 0;
                if (f.air) setState(f, FState::Airborne);
                else groundControl(i);
            }
            break;
        }
        case FState::Blockstun:
            if (f.stun > 0) --f.stun;
            if (f.stun == 0) groundControl(i);
            break;
        case FState::Hitstun:
            if (f.stun > 0) --f.stun;
            if (f.stun == 0 && !f.air) {
                f.comboHits = 0;
                f.comboDamage = 0;
                f.juggle = 0;
                groundControl(i);
            }
            break;
        case FState::Knockdown:
            if (f.stateFrame >= fd.knockdownFrames) {
                f.invuln = fd.wakeupInvuln;
                f.comboHits = 0;
                f.comboDamage = 0;
                f.juggle = 0;
                groundControl(i);
            }
            break;
        case FState::KO:
        case FState::Count:
            break;
    }
}

void Match::groundControl(int i) {
    Fighter& f = s_.f[i];
    const FighterData& fd = moves_->fighter;
    if (tryStartMove(i, nullptr, 0)) return;
    const InputBits in = f.input.current();
    const int h = horizontal(in) * f.facing;
    if (in & IN_UP) {
        setState(f, FState::JumpSquat);
        f.jumpDir = static_cast<int8_t>(h);
        f.vel.x = 0;
    } else if (in & IN_DOWN) {
        setState(f, FState::Crouch);
        f.vel.x = 0;
    } else if (h > 0) {
        setState(f, FState::WalkForward);
        f.vel.x = f.facing * fd.walkForward;
    } else if (h < 0) {
        setState(f, FState::WalkBackward);
        f.vel.x = -f.facing * fd.walkBackward;
    } else {
        setState(f, FState::Idle);
        f.vel.x = 0;
    }
}

// Picks the most specific move whose button was pressed within the buffer
// window: motion inputs beat directional normals, which beat plain normals.
bool Match::tryStartMove(int i, const int8_t* allowed, int allowedCount) {
    Fighter& f = s_.f[i];
    const MoveSet& ms = *moves_;
    const int n = allowed ? allowedCount : ms.count;
    int best = -1, bestScore = 0, bestAgo = 0;
    for (int k = 0; k < n; ++k) {
        const int mi = allowed ? allowed[k] : k;
        const MoveData& m = ms.moves[mi];
        if ((m.stance == Stance::Air) != (f.air != 0)) continue;
        const int ago = f.input.findPress(m.button, config_.bufferFrames);
        if (ago < 0) continue;
        const InputBits held = f.input.at(ago);
        const bool down = (held & IN_DOWN) != 0;
        const int h = horizontal(held) * f.facing;
        if (m.stance == Stance::Crouch && !down) continue;
        if (m.stance == Stance::Stand && down && !m.motion) continue;
        if (m.dir == DirReq::Forward && h <= 0) continue;
        if (m.dir == DirReq::Back && h >= 0) continue;
        if (m.motion && !f.input.quarterCircleForward(f.facing, ago, kMotionWindow)) continue;
        const int score = m.motion ? 3 : (m.dir != DirReq::None ? 2 : 1);
        if (score > bestScore) {
            best = mi;
            bestScore = score;
            bestAgo = ago;
        }
    }
    if (best < 0) return false;
    f.input.consume(ms.moves[best].button, bestAgo);
    startMove(i, best);
    return true;
}

void Match::startMove(int i, int move) {
    Fighter& f = s_.f[i];
    const MoveData& m = moves_->moves[move];
    setState(f, FState::Attack);
    f.move = static_cast<int8_t>(move);
    f.moveFrame = 0;
    f.connected = 0;
    f.cancelTimer = 0;
    if (f.air) {
        f.airAttackUsed = 1;
    } else {
        f.vel.x = 0;
        if (m.lunge) f.slide = f.facing * m.lunge;
    }
}

void Match::physics(int i, fx& movedX) {
    Fighter& f = s_.f[i];
    const FighterData& fd = moves_->fighter;
    const fx oldX = f.pos.x;
    if (f.air) f.vel.y -= fd.gravity;
    f.pos.x += f.vel.x + f.slide;
    f.pos.y += f.vel.y;
    f.slide = fxScale(f.slide, kFrictionNum, 256);
    if (fxAbs(f.slide) < 16) f.slide = 0;

    if (f.air && f.pos.y <= 0 && f.vel.y <= 0) {
        f.pos.y = 0;
        f.vel.y = 0;
        f.vel.x = 0;
        f.air = 0;
        onLand(i);
    }
    if (!f.air) f.pos.y = 0;
    const fx lim = kStageHalfWidth - fd.pushWidth;
    f.pos.x = fxClamp(f.pos.x, -lim, lim);
    movedX = f.pos.x - oldX;
}

void Match::onLand(int i) {
    Fighter& f = s_.f[i];
    f.airAttackUsed = 0;
    switch (f.state) {
        case FState::Airborne:
            setState(f, FState::Landing);
            events_.push({EventType::Land, static_cast<uint8_t>(i), -1, f.pos, 0, 0, 0});
            break;
        case FState::Attack:
            f.move = -1;
            f.cancelTimer = 0;
            setState(f, FState::Landing);
            events_.push({EventType::Land, static_cast<uint8_t>(i), -1, f.pos, 0, 0, 0});
            break;
        case FState::Hitstun:
            f.launched = 0;
            f.stun = 0;
            setState(f, FState::Knockdown);
            events_.push({EventType::Knockdown, static_cast<uint8_t>(i), -1, f.pos, 0, 0, 0});
            break;
        default:
            break;
    }
}

bool Match::atWall(const Fighter& f) const {
    return fxAbs(f.pos.x) >= kStageHalfWidth - moves_->fighter.pushWidth - toFx(2);
}

void Match::separate(const fx movedX[2]) {
    Fighter& a = s_.f[0];
    Fighter& b = s_.f[1];
    const fx lim = kStageHalfWidth - moves_->fighter.pushWidth;
    const fx minDist = 2 * moves_->fighter.pushWidth;

    fx dx = b.pos.x - a.pos.x;
    if (fxAbs(a.pos.y - b.pos.y) < kPushHeight && fxAbs(dx) < minDist) {
        const int dir = dx != 0 ? sign(dx) : a.facing;  // side of a that b is on
        const fx overlap = minDist - fxAbs(dx);
        a.pos.x -= dir * (overlap / 2);
        b.pos.x += dir * (overlap - overlap / 2);
        // A fighter pinned against the wall cannot give ground; the other takes it all.
        if (a.pos.x < -lim) { b.pos.x += -lim - a.pos.x; a.pos.x = -lim; }
        if (a.pos.x > lim) { b.pos.x -= a.pos.x - lim; a.pos.x = lim; }
        if (b.pos.x < -lim) { a.pos.x += -lim - b.pos.x; b.pos.x = -lim; }
        if (b.pos.x > lim) { a.pos.x -= b.pos.x - lim; b.pos.x = lim; }
    }

    dx = b.pos.x - a.pos.x;
    if (fxAbs(dx) > kMaxSeparation) {
        const int dir = sign(dx);
        const fx excess = fxAbs(dx) - kMaxSeparation;
        const bool aAway = movedX[0] != 0 && sign(movedX[0]) == -dir;
        const bool bAway = movedX[1] != 0 && sign(movedX[1]) == dir;
        if (aAway && !bAway) a.pos.x += dir * excess;
        else if (bAway && !aAway) b.pos.x -= dir * excess;
        else {
            a.pos.x += dir * (excess / 2);
            b.pos.x -= dir * (excess - excess / 2);
        }
    }
}

void Match::updateFacing() {
    for (int i = 0; i < 2; ++i) {
        Fighter& f = s_.f[i];
        const Fighter& o = s_.f[1 - i];
        if (f.air) continue;
        switch (f.state) {
            case FState::Idle:
            case FState::WalkForward:
            case FState::WalkBackward:
            case FState::Crouch:
            case FState::Landing:
            case FState::Knockdown:
                break;
            default:
                continue;
        }
        const int want = sign(o.pos.x - f.pos.x);
        if (want == 0 || want == f.facing) continue;
        f.facing = static_cast<int8_t>(want);
        if (f.state == FState::WalkForward) setState(f, FState::WalkBackward);
        else if (f.state == FState::WalkBackward) setState(f, FState::WalkForward);
    }
}

Box Match::hurtbox(int who) const {
    const Fighter& f = s_.f[who];
    const FighterData& fd = moves_->fighter;
    const Box* local = &fd.hurtStand;
    if (f.air) {
        local = &fd.hurtAir;
    } else if (f.state == FState::Crouch || f.state == FState::Knockdown || f.state == FState::KO ||
               (f.state == FState::Attack && f.move >= 0 && moves_->moves[f.move].stance == Stance::Crouch) ||
               (f.state == FState::Blockstun && (f.input.current() & IN_DOWN))) {
        local = &fd.hurtCrouch;
    }
    return localToWorld(*local, f.pos, f.facing);
}

bool Match::activeBox(int who, Box& out) const {
    const Fighter& f = s_.f[who];
    if (f.state != FState::Attack || f.move < 0) return false;
    const MoveData& m = moves_->moves[f.move];
    const int act = f.moveFrame - m.startup;
    if (act < 0 || act >= m.active || m.hit.shape != Shape::Box) return false;
    out = m.hit.box;
    return true;
}

bool Match::activeArc(int who, ArcRegion& out) const {
    const Fighter& f = s_.f[who];
    if (f.state != FState::Attack || f.move < 0) return false;
    const MoveData& m = moves_->moves[f.move];
    const int act = f.moveFrame - m.startup;
    if (act < 0 || act >= m.active || m.hit.shape != Shape::Arc) return false;
    const int sweep = m.hit.a1 - m.hit.a0;
    const int from = m.hit.a0 + sweep * act / m.active;
    const int to = m.hit.a0 + sweep * (act + 1) / m.active;
    out = {m.hit.cx, m.hit.cy, m.hit.r0, m.hit.r1, from < to ? from : to, from < to ? to : from};
    return true;
}

bool Match::isActionable(int who) const {
    switch (s_.f[who].state) {
        case FState::Idle:
        case FState::WalkForward:
        case FState::WalkBackward:
        case FState::Crouch:
            return true;
        default:
            return false;
    }
}

bool Match::canBlock(int defender, int attacker, const MoveData& m) const {
    const Fighter& d = s_.f[defender];
    const Fighter& a = s_.f[attacker];
    if (d.air) return false;
    switch (d.state) {
        case FState::Idle:
        case FState::WalkForward:
        case FState::WalkBackward:
        case FState::Crouch:
        case FState::Blockstun:
            break;
        default:
            return false;
    }
    // Guard direction comes from where the attacker actually is, not from the
    // defender's facing, so cross-ups must be blocked the other way.
    int side = sign(a.pos.x - d.pos.x);
    if (side == 0) side = d.facing;
    const InputBits in = d.input.current();
    if (horizontal(in) != -side) return false;
    const bool crouching = (in & IN_DOWN) != 0;
    if (m.guard == Guard::High && crouching) return false;
    if (m.guard == Guard::Low && !crouching) return false;
    return true;
}

void Match::resolveHits() {
    if (s_.phase != Phase::Fight) return;
    const FighterData& fd = moves_->fighter;
    bool hit[2] = {false, false};
    bool blocked[2] = {false, false};
    int moveIdx[2] = {-1, -1};
    Vec2 at[2] = {};

    for (int a = 0; a < 2; ++a) {
        const int d = 1 - a;
        const Fighter& A = s_.f[a];
        const Fighter& D = s_.f[d];
        if (A.state != FState::Attack || A.move < 0 || A.connected) continue;
        const MoveData& m = moves_->moves[A.move];
        const int act = A.moveFrame - m.startup;
        if (act < 0 || act >= m.active) continue;
        if (D.invuln || D.state == FState::KO || D.state == FState::Knockdown) continue;
        if (D.air && D.state == FState::Hitstun && D.juggle >= fd.maxJuggle) continue;
        // Facing check: most strikes only connect in front of the attacker.
        if (m.frontOnly && sign(D.pos.x - A.pos.x) == -A.facing) continue;

        const Box hurt = worldToLocal(hurtbox(d), A.pos, A.facing);
        fx lx = 0, ly = 0;
        if (m.hit.shape == Shape::Box) {
            Box hb;
            activeBox(a, hb);
            if (!boxesOverlap(hb, hurt)) continue;
            const fx x0 = fxMax(hb.x, hurt.x), x1 = fxMin(hb.x + hb.w, hurt.x + hurt.w);
            const fx y0 = fxMax(hb.y, hurt.y), y1 = fxMin(hb.y + hb.h, hurt.y + hurt.h);
            lx = (x0 + x1) / 2;
            ly = (y0 + y1) / 2;
        } else {
            ArcRegion arc;
            activeArc(a, arc);
            if (!arcOverlapsBox(arc, hurt)) continue;
            int32_t dx, dy;
            dirFromDegrees((arc.lo + arc.hi) / 2, dx, dy);
            const fx r = (arc.r0 + arc.r1) / 2;
            lx = fxClamp(arc.cx + static_cast<fx>(static_cast<int64_t>(dx) * r / kDirScale), hurt.x, hurt.x + hurt.w);
            ly = fxClamp(arc.cy + static_cast<fx>(static_cast<int64_t>(dy) * r / kDirScale), hurt.y, hurt.y + hurt.h);
        }
        hit[a] = true;
        blocked[a] = canBlock(d, a, m);
        moveIdx[a] = A.move;
        at[a] = {A.pos.x + A.facing * lx, A.pos.y + ly};
    }
    // Both hits resolve together so simultaneous attacks trade.
    for (int a = 0; a < 2; ++a)
        if (hit[a]) applyHit(a, 1 - a, blocked[a], moveIdx[a], at[a]);
}

void Match::applyHit(int attacker, int defender, bool blocked, int moveIdx, Vec2 at) {
    Fighter& A = s_.f[attacker];
    Fighter& D = s_.f[defender];
    const MoveData& m = moves_->moves[moveIdx];
    int dir = sign(D.pos.x - A.pos.x);
    if (dir == 0) dir = A.facing;

    if (A.move == moveIdx && A.state == FState::Attack) {
        A.connected = blocked ? 2 : 1;
        A.cancelTimer = m.cancelWindow;
    }
    A.hitstop = m.hitstop;
    D.hitstop = m.hitstop;

    if (blocked) {
        D.health = static_cast<int16_t>(D.health - m.chip);
        D.move = -1;
        setState(D, FState::Blockstun);
        D.stun = m.blockstun;
        D.vel.x = 0;
        D.slide = dir * m.blockPush;
        if (atWall(D)) A.slide = -dir * m.blockPush;
        events_.push({EventType::Block, static_cast<uint8_t>(attacker), static_cast<int8_t>(moveIdx), at, m.chip, 0, 0});
    } else {
        const bool counter = D.state == FState::Attack && D.move >= 0 && D.moveFrame < moves_->moves[D.move].startup;
        if (D.state != FState::Hitstun) {
            D.comboHits = 0;
            D.comboDamage = 0;
            D.juggle = 0;
        }
        int scale = 100 - 10 * D.comboHits;
        if (scale < 30) scale = 30;
        int dmg = m.damage * scale / 100;
        if (counter) dmg = dmg * 120 / 100;
        if (dmg < 1 && m.damage > 0) dmg = 1;
        D.health = static_cast<int16_t>(D.health - dmg);
        ++D.comboHits;
        D.comboDamage = static_cast<int16_t>(D.comboDamage + dmg);

        // Knockback grows with combo length so long combos push the victim out.
        const fx kx = fxScale(m.knockbackX, 100 + 6 * (D.comboHits - 1), 100);
        D.move = -1;
        D.cancelTimer = 0;
        D.counterHit = counter ? 1 : 0;
        setState(D, FState::Hitstun);
        D.stun = static_cast<uint16_t>(m.hitstun + (counter ? 4 : 0));
        if (m.knockbackY > 0 || D.air || m.knockdown) {
            D.air = 1;
            D.launched = 1;
            ++D.juggle;
            D.vel.x = dir * kx;
            D.vel.y = m.knockbackY > 0 ? m.knockbackY : toFx(6);
            D.slide = 0;
        } else {
            D.vel.x = 0;
            D.slide = dir * kx;
            if (atWall(D)) A.slide = -dir * kx;
        }
        events_.push({EventType::Hit, static_cast<uint8_t>(attacker), static_cast<int8_t>(moveIdx), at,
                      static_cast<int16_t>(dmg), D.comboHits, static_cast<uint8_t>(counter)});
    }

    if (D.health <= 0) {
        if (config_.training) {
            D.health = 1;
        } else if (s_.phase == Phase::Fight) {
            D.health = 0;
            D.move = -1;
            setState(D, FState::KO);
            D.air = 1;
            D.stun = 0;
            D.slide = 0;
            D.vel.x = dir * toFx(5);
            D.vel.y = toFx(10);
            A.hitstop = D.hitstop = 24;
            events_.push({EventType::KO, static_cast<uint8_t>(attacker), static_cast<int8_t>(moveIdx), at, 0, 0, 0});
            endRound(attacker);
        }
    }
}

void Match::endRound(int winner) {
    if (s_.phase != Phase::Fight) return;
    s_.phase = Phase::RoundEnd;
    s_.phaseTimer = kRoundEndFrames;
    s_.roundWinner = static_cast<int8_t>(winner);
    if (winner >= 0) ++s_.wins[winner];
}

}  // namespace sp
