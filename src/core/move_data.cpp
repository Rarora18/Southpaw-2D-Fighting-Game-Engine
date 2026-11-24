#include "core/move_data.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

#include "core/hash.h"
#include "core/json.h"

namespace sp {
namespace {

const char* const kAnimNames[] = {
    "jab", "cross", "uppercut", "short_kick", "roundhouse", "axe_kick",
    "crouch_jab", "low_kick", "sweep", "jump_punch", "jump_kick", "haymaker",
};
static_assert(sizeof(kAnimNames) / sizeof(kAnimNames[0]) == static_cast<size_t>(Anim::Count), "anim names");

struct Ctx {
    std::string& error;
    std::string where;

    bool fail(const std::string& msg) {
        error = where + ": " + msg;
        return false;
    }
};

bool readInt(Ctx& c, const Json& o, const char* key, int lo, int hi, int fallback, int& out, bool required = false) {
    const Json* v = o.get(key);
    if (!v) {
        if (required) return c.fail(std::string("missing \"") + key + "\"");
        out = fallback;
        return true;
    }
    if (!v->isNumber()) return c.fail(std::string("\"") + key + "\" must be a number");
    double d = v->number;
    if (d != static_cast<int>(d)) return c.fail(std::string("\"") + key + "\" must be an integer");
    if (d < lo || d > hi)
        return c.fail(std::string("\"") + key + "\" out of range [" + std::to_string(lo) + ", " + std::to_string(hi) + "]");
    out = static_cast<int>(d);
    return true;
}

template <class T>
bool readSmall(Ctx& c, const Json& o, const char* key, int lo, int hi, int fallback, T& out, bool required = false) {
    int v = 0;
    if (!readInt(c, o, key, lo, hi, fallback, v, required)) return false;
    out = static_cast<T>(v);
    return true;
}

bool readFx(Ctx& c, const Json& o, const char* key, double fallback, fx& out) {
    const Json* v = o.get(key);
    if (!v) {
        out = fxFromDouble(fallback);
        return true;
    }
    if (!v->isNumber()) return c.fail(std::string("\"") + key + "\" must be a number");
    out = fxFromDouble(v->number);
    return true;
}

bool readBox(Ctx& c, const Json& o, const char* key, Box& out) {
    const Json* v = o.get(key);
    if (!v || !v->isArray() || v->array.size() != 4) return c.fail(std::string("\"") + key + "\" must be [x, y, w, h]");
    fx vals[4];
    for (int i = 0; i < 4; ++i) {
        if (!v->array[i].isNumber()) return c.fail(std::string("\"") + key + "\" must contain numbers");
        vals[i] = fxFromDouble(v->array[i].number);
    }
    out = {vals[0], vals[1], vals[2], vals[3]};
    if (out.w <= 0 || out.h <= 0) return c.fail(std::string("\"") + key + "\" needs positive size");
    return true;
}

bool parseButton(const std::string& s, uint8_t& out) {
    static const char* names[] = {"LP", "HP", "LK", "HK"};
    for (uint8_t i = 0; i < 4; ++i)
        if (s == names[i]) {
            out = i;
            return true;
        }
    return false;
}

bool parseHitShape(Ctx& c, const Json& o, HitShape& out) {
    const std::string type = o.str("type", "box");
    if (type == "box") {
        out.shape = Shape::Box;
        fx w, h;
        if (!readFx(c, o, "x", 0, out.box.x) || !readFx(c, o, "y", 0, out.box.y) || !readFx(c, o, "w", 0, w) ||
            !readFx(c, o, "h", 0, h))
            return false;
        out.box.w = w;
        out.box.h = h;
        if (w <= 0 || h <= 0) return c.fail("hitbox needs positive w/h");
        return true;
    }
    if (type == "arc") {
        out.shape = Shape::Arc;
        int a0 = 0, a1 = 0;
        if (!readFx(c, o, "cx", 0, out.cx) || !readFx(c, o, "cy", 0, out.cy) ||
            !readFx(c, o, "inner", 0, out.r0) || !readFx(c, o, "outer", 0, out.r1) ||
            !readInt(c, o, "from", -720, 720, 0, a0, true) || !readInt(c, o, "to", -720, 720, 0, a1, true))
            return false;
        if (out.r1 <= out.r0 || out.r0 < 0) return c.fail("arc needs 0 <= inner < outer");
        if (a0 == a1) return c.fail("arc needs from != to");
        if ((a1 > a0 ? a1 - a0 : a0 - a1) > 360) return c.fail("arc sweep exceeds 360 degrees");
        out.a0 = static_cast<int16_t>(a0);
        out.a1 = static_cast<int16_t>(a1);
        return true;
    }
    return c.fail("unknown hitbox type \"" + type + "\" (expected box or arc)");
}

bool parseMove(Ctx& c, const Json& o, MoveData& m, std::vector<std::string>& cancelNames) {
    const std::string name = o.str("name", "");
    if (name.empty() || name.size() >= kMaxNameLen) return c.fail("move needs a name shorter than 24 chars");
    c.where = "move \"" + name + "\"";
    std::strncpy(m.name, name.c_str(), kMaxNameLen - 1);

    const std::string anim = o.str("anim", "");
    bool animOk = false;
    for (int i = 0; i < static_cast<int>(Anim::Count); ++i)
        if (anim == kAnimNames[i]) {
            m.anim = static_cast<Anim>(i);
            animOk = true;
        }
    if (!animOk) return c.fail("unknown anim \"" + anim + "\"");

    if (!parseButton(o.str("button", ""), m.button)) return c.fail("\"button\" must be LP, HP, LK or HK");

    const std::string stance = o.str("stance", "stand");
    if (stance == "stand") m.stance = Stance::Stand;
    else if (stance == "crouch") m.stance = Stance::Crouch;
    else if (stance == "air") m.stance = Stance::Air;
    else return c.fail("\"stance\" must be stand, crouch or air");

    const std::string dir = o.str("dir", "none");
    if (dir == "none") m.dir = DirReq::None;
    else if (dir == "forward") m.dir = DirReq::Forward;
    else if (dir == "back") m.dir = DirReq::Back;
    else return c.fail("\"dir\" must be none, forward or back");

    const std::string motion = o.str("motion", "none");
    if (motion == "qcf") m.motion = true;
    else if (motion != "none") return c.fail("\"motion\" must be none or qcf");

    const std::string guard = o.str("guard", "mid");
    if (guard == "mid") m.guard = Guard::Mid;
    else if (guard == "high") m.guard = Guard::High;
    else if (guard == "low") m.guard = Guard::Low;
    else return c.fail("\"guard\" must be mid, high or low");

    if (!readSmall(c, o, "startup", 1, 60, 1, m.startup, true)) return false;
    if (!readSmall(c, o, "active", 1, 30, 1, m.active, true)) return false;
    if (!readSmall(c, o, "recovery", 1, 90, 1, m.recovery, true)) return false;
    if (!readSmall(c, o, "damage", 0, 1000, 0, m.damage, true)) return false;
    if (!readSmall(c, o, "chip", 0, 1000, 0, m.chip)) return false;
    if (!readSmall(c, o, "hitstun", 0, 120, 0, m.hitstun, true)) return false;
    if (!readSmall(c, o, "blockstun", 0, 120, 0, m.blockstun, true)) return false;
    if (!readSmall(c, o, "hitstop", 0, 30, 0, m.hitstop)) return false;
    if (!readSmall(c, o, "cancel_window", 0, 30, 0, m.cancelWindow)) return false;
    if (!readFx(c, o, "block_push", 0, m.blockPush) || !readFx(c, o, "lunge", 0, m.lunge)) return false;
    m.knockdown = o.flag("knockdown", false);
    m.frontOnly = o.flag("front_only", true);

    if (const Json* kb = o.get("knockback")) {
        if (!kb->isArray() || kb->array.size() != 2 || !kb->array[0].isNumber() || !kb->array[1].isNumber())
            return c.fail("\"knockback\" must be [x, y]");
        m.knockbackX = fxFromDouble(kb->array[0].number);
        m.knockbackY = fxFromDouble(kb->array[1].number);
    }

    const Json* hb = o.get("hitbox");
    if (!hb || !hb->isObject()) return c.fail("missing \"hitbox\" object");
    if (!parseHitShape(c, *hb, m.hit)) return false;

    cancelNames.clear();
    if (const Json* cs = o.get("cancels")) {
        if (!cs->isArray()) return c.fail("\"cancels\" must be an array of move names");
        if (cs->array.size() > kMaxCancels) return c.fail("too many cancels");
        for (const Json& n : cs->array) {
            if (!n.isString()) return c.fail("\"cancels\" must contain strings");
            cancelNames.push_back(n.string);
        }
    }
    if (m.total() > 250) return c.fail("move is longer than 250 frames");
    return true;
}

uint64_t hashMoveSet(const MoveSet& s) {
    Hasher h;
    const FighterData& f = s.fighter;
    h.add(f.health);
    h.add(f.walkForward); h.add(f.walkBackward);
    h.add(f.jumpVelocity); h.add(f.jumpForward); h.add(f.gravity);
    h.add(f.jumpSquat); h.add(f.landing); h.add(f.knockdownFrames); h.add(f.wakeupInvuln); h.add(f.maxJuggle);
    h.add(f.pushWidth);
    for (const Box* b : {&f.hurtStand, &f.hurtCrouch, &f.hurtAir}) {
        h.add(b->x); h.add(b->y); h.add(b->w); h.add(b->h);
    }
    h.add(s.count);
    for (int i = 0; i < s.count; ++i) {
        const MoveData& m = s.moves[i];
        h.bytes(m.name, kMaxNameLen);
        h.add(m.anim); h.add(m.stance); h.add(m.button); h.add(m.dir); h.add(m.motion);
        h.add(m.startup); h.add(m.active); h.add(m.recovery);
        h.add(m.damage); h.add(m.chip); h.add(m.hitstun); h.add(m.blockstun); h.add(m.hitstop);
        h.add(m.knockbackX); h.add(m.knockbackY); h.add(m.blockPush); h.add(m.guard);
        h.add(m.knockdown); h.add(m.frontOnly); h.add(m.lunge);
        h.add(m.hit.shape);
        h.add(m.hit.box.x); h.add(m.hit.box.y); h.add(m.hit.box.w); h.add(m.hit.box.h);
        h.add(m.hit.cx); h.add(m.hit.cy); h.add(m.hit.r0); h.add(m.hit.r1); h.add(m.hit.a0); h.add(m.hit.a1);
        h.add(m.cancelWindow); h.add(m.cancelCount);
        for (int k = 0; k < m.cancelCount; ++k) h.add(m.cancels[k]);
    }
    return h.h;
}

}  // namespace

const char* animName(Anim a) {
    int i = static_cast<int>(a);
    return i >= 0 && i < static_cast<int>(Anim::Count) ? kAnimNames[i] : "?";
}

int MoveSet::find(const char* name) const {
    for (int i = 0; i < count; ++i)
        if (std::strcmp(moves[i].name, name) == 0) return i;
    return -1;
}

bool parseMoveSet(const std::string& text, MoveSet& out, std::string& error) {
    Json root;
    if (!Json::parse(text, root, error)) {
        error = "moves.json " + error;
        return false;
    }
    Ctx c{error, "root"};
    if (!root.isObject()) return c.fail("expected an object");

    MoveSet s;
    const Json* fj = root.get("fighter");
    if (!fj || !fj->isObject()) return c.fail("missing \"fighter\" object");
    c.where = "fighter";
    FighterData& f = s.fighter;
    if (!readSmall(c, *fj, "health", 1, 30000, 1000, f.health, true)) return false;
    if (!readFx(c, *fj, "walk_forward", 3, f.walkForward) || !readFx(c, *fj, "walk_backward", 2.5, f.walkBackward) ||
        !readFx(c, *fj, "jump_velocity", 15, f.jumpVelocity) || !readFx(c, *fj, "jump_forward", 4, f.jumpForward) ||
        !readFx(c, *fj, "gravity", 0.8, f.gravity) || !readFx(c, *fj, "push_width", 26, f.pushWidth))
        return false;
    if (!readSmall(c, *fj, "jump_squat", 1, 30, 4, f.jumpSquat) || !readSmall(c, *fj, "landing", 1, 30, 3, f.landing) ||
        !readSmall(c, *fj, "knockdown", 1, 120, 40, f.knockdownFrames) ||
        !readSmall(c, *fj, "wakeup_invuln", 0, 60, 8, f.wakeupInvuln) ||
        !readSmall(c, *fj, "max_juggle", 1, 20, 3, f.maxJuggle))
        return false;
    if (!readBox(c, *fj, "hurtbox_stand", f.hurtStand) || !readBox(c, *fj, "hurtbox_crouch", f.hurtCrouch) ||
        !readBox(c, *fj, "hurtbox_air", f.hurtAir))
        return false;
    if (f.gravity <= 0 || f.jumpVelocity <= 0) return c.fail("gravity and jump_velocity must be positive");

    const Json* mj = root.get("moves");
    c.where = "root";
    if (!mj || !mj->isArray() || mj->array.empty()) return c.fail("\"moves\" must be a non-empty array");
    if (mj->array.size() > kMaxMoves) return c.fail("too many moves (max 32)");

    std::vector<std::vector<std::string>> cancelNames(mj->array.size());
    for (size_t i = 0; i < mj->array.size(); ++i) {
        c.where = "moves[" + std::to_string(i) + "]";
        if (!mj->array[i].isObject()) return c.fail("expected an object");
        MoveData& m = s.moves[s.count];
        if (!parseMove(c, mj->array[i], m, cancelNames[i])) return false;
        if (s.find(m.name) >= 0) return c.fail("duplicate move name");
        ++s.count;
    }
    for (int i = 0; i < s.count; ++i) {
        MoveData& m = s.moves[i];
        c.where = std::string("move \"") + m.name + "\"";
        for (const std::string& n : cancelNames[i]) {
            int idx = s.find(n.c_str());
            if (idx < 0) return c.fail("cancels into unknown move \"" + n + "\"");
            if (s.moves[idx].stance == Stance::Air && m.stance != Stance::Air)
                return c.fail("ground move cannot cancel into air move \"" + n + "\"");
            m.cancels[m.cancelCount++] = static_cast<int8_t>(idx);
        }
    }
    s.hash = hashMoveSet(s);
    out = s;
    return true;
}

bool loadMoveSet(const std::string& path, MoveSet& out, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return parseMoveSet(ss.str(), out, error);
}

}  // namespace sp
