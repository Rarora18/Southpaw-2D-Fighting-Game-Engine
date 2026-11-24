#include "core/ai.h"

namespace sp {
namespace {

constexpr InputBits kButtons = IN_LP | IN_HP | IN_LK | IN_HK;

struct Skill {
    int block;    // % chance per frame to start guarding against a live attack
    int combo;    // % chance to continue a confirmed hit
    int antiAir;  // % chance per frame to anti-air a jump-in
    int aggro;    // % chance to attack when in range
};
constexpr Skill kSkills[4] = {{3, 20, 2, 25}, {9, 55, 6, 40}, {22, 85, 14, 55}, {45, 100, 30, 65}};

int reachPx(const MoveData& m) {
    const fx r = m.hit.shape == Shape::Box ? m.hit.box.x + m.hit.box.w : m.hit.cx + m.hit.r1;
    return r / kFxOne;
}

}  // namespace

void CpuBrain::reset(uint32_t seed, int level) {
    rng_ = seed ? seed : 0x9E3779B9u;
    level_ = level < 0 ? 0 : (level > 3 ? 3 : level);
    macroLen_ = macroPos_ = 0;
    lastOut_ = holdDir_ = 0;
    holdFrames_ = blockFrames_ = 0;
}

uint32_t CpuBrain::next() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return rng_;
}

InputBits CpuBrain::pressFor(const MoveSet& ms, int move, InputBits toward, InputBits away) {
    const MoveData& m = ms.moves[move];
    const InputBits button = buttonBit(m.button);
    if (m.motion) {
        macro_[0] = IN_DOWN;
        macro_[1] = static_cast<InputBits>(IN_DOWN | toward);
        macro_[2] = static_cast<InputBits>(toward | button);
        macro_[3] = toward;
        macroLen_ = 4;
        macroPos_ = 1;
        return macro_[0];
    }
    InputBits out = button;
    if (m.stance == Stance::Crouch) out |= IN_DOWN;
    if (m.dir == DirReq::Forward) out |= toward;
    if (m.dir == DirReq::Back) out |= away;
    return out;
}

int CpuBrain::pickMove(const MoveSet& ms, int distPx, bool air) {
    int candidates[kMaxMoves];
    int n = 0;
    for (int i = 0; i < ms.count; ++i) {
        const MoveData& m = ms.moves[i];
        if ((m.stance == Stance::Air) != air) continue;
        if (!air && reachPx(m) + 20 < distPx) continue;
        candidates[n++] = i;
    }
    return n ? candidates[roll(n)] : -1;
}

InputBits CpuBrain::finish(InputBits out) {
    // A button only registers on the frame it goes down, so release it first if
    // it is already held.
    if (out & lastOut_ & kButtons) out = static_cast<InputBits>(out & ~(lastOut_ & kButtons));
    lastOut_ = out;
    return out;
}

InputBits CpuBrain::think(const Match& match, int self) {
    const MatchState& s = match.state();
    if (s.phase != Phase::Fight) {
        macroLen_ = macroPos_ = 0;
        return finish(0);
    }
    const MoveSet& ms = match.moves();
    const Skill& sk = kSkills[level_];
    const Fighter& me = s.f[self];
    const Fighter& op = s.f[1 - self];
    const fx dx = op.pos.x - me.pos.x;
    const InputBits toward = dx >= 0 ? IN_RIGHT : IN_LEFT;
    const InputBits away = dx >= 0 ? IN_LEFT : IN_RIGHT;
    const int dist = fxAbs(dx) / kFxOne;

    if (macroPos_ < macroLen_) return finish(macro_[macroPos_++]);

    // Continue a confirmed hit through the move's cancel list.
    if (me.state == FState::Attack && me.connected == 1 && me.cancelTimer > 0 && me.move >= 0) {
        const MoveData& m = ms.moves[me.move];
        if (m.cancelCount > 0 && chance(sk.combo))
            return finish(pressFor(ms, m.cancels[roll(m.cancelCount)], toward, away));
    }

    // Guard against live attacks.
    if (op.state == FState::Attack && op.move >= 0 && dist < 280) {
        const MoveData& om = ms.moves[op.move];
        if (op.moveFrame < om.startup + om.active && (blockFrames_ > 0 || chance(sk.block))) {
            if (blockFrames_ == 0) blockFrames_ = 6 + roll(8);
            holdDir_ = (om.guard == Guard::Low || (om.guard == Guard::Mid && chance(50)))
                           ? static_cast<InputBits>(away | IN_DOWN)
                           : away;
        }
    }
    if (blockFrames_ > 0) {
        --blockFrames_;
        return finish(holdDir_);
    }

    if (me.state == FState::Airborne && !me.airAttackUsed && me.vel.y < 0 && dist < 150 && chance(35)) {
        const int m = pickMove(ms, dist, true);
        if (m >= 0) return finish(pressFor(ms, m, toward, away));
    }

    if (!match.isActionable(self)) return finish(holdDir_ & static_cast<InputBits>(IN_LEFT | IN_RIGHT | IN_DOWN));

    // Anti-air a jump-in with whatever launches upward.
    if (op.air && op.state == FState::Airborne && dist < 200 && chance(sk.antiAir)) {
        for (int i = 0; i < ms.count; ++i)
            if (ms.moves[i].anim == Anim::Uppercut) return finish(pressFor(ms, i, toward, away));
    }

    if (holdFrames_ > 0) {
        --holdFrames_;
        if (dist < 160 && chance(sk.aggro / 6)) {
            const int m = pickMove(ms, dist, false);
            if (m >= 0) return finish(pressFor(ms, m, toward, away));
        }
        return finish(holdDir_);
    }

    holdFrames_ = 6 + roll(18);
    const int r = roll(100);
    if (dist > 300) {
        holdDir_ = r < 8 ? static_cast<InputBits>(IN_UP | toward) : toward;
    } else if (dist > 170) {
        if (r < 45) holdDir_ = toward;
        else if (r < 58) holdDir_ = static_cast<InputBits>(IN_UP | toward);
        else if (r < 72) holdDir_ = away;
        else {
            holdFrames_ = 0;
            const int m = pickMove(ms, dist, false);
            if (m >= 0) return finish(pressFor(ms, m, toward, away));
            holdDir_ = toward;
        }
    } else {
        if (r < sk.aggro) {
            holdFrames_ = 0;
            const int m = pickMove(ms, dist, false);
            if (m >= 0) return finish(pressFor(ms, m, toward, away));
        }
        if (r < sk.aggro + 15) holdDir_ = away;
        else if (r < sk.aggro + 27) holdDir_ = static_cast<InputBits>(IN_DOWN | away);
        else if (r < sk.aggro + 33) holdDir_ = static_cast<InputBits>(IN_UP | (chance(50) ? toward : 0));
        else holdDir_ = 0;
    }
    return finish(holdDir_);
}

}  // namespace sp
