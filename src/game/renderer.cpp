#include "game/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "game/pose.h"

namespace sp {
namespace {

constexpr float kGroundY = 610;
constexpr float kStageHalf = 900;

struct Palette {
    Color skin, skinShade, primary, primaryShade, accent, band, wraps, hair;
};
const Palette kPalettes[2] = {
    {{236, 188, 146, 255}, {196, 146, 108, 255}, {214, 40, 57, 255}, {150, 22, 38, 255}, {255, 209, 102, 255},
     {214, 40, 57, 255}, {240, 240, 235, 255}, {38, 28, 26, 255}},
    {{205, 150, 110, 255}, {162, 112, 80, 255}, {38, 110, 196, 255}, {22, 66, 130, 255}, {230, 230, 240, 255},
     {245, 245, 245, 255}, {40, 44, 56, 255}, {20, 20, 28, 255}},
};
const Color kOutline{18, 14, 24, 255};

Color mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return {static_cast<unsigned char>(a.r + (b.r - a.r) * t), static_cast<unsigned char>(a.g + (b.g - a.g) * t),
            static_cast<unsigned char>(a.b + (b.b - a.b) * t), static_cast<unsigned char>(a.a + (b.a - a.a) * t)};
}

Color alpha(Color c, float a) {
    c.a = static_cast<unsigned char>(std::clamp(a, 0.0f, 1.0f) * c.a);
    return c;
}

float hash01(int i) {
    uint32_t x = static_cast<uint32_t>(i) * 2654435761u;
    x ^= x >> 15;
    x *= 2246822519u;
    x ^= x >> 13;
    return (x & 0xFFFF) / 65535.0f;
}

Font tryLoadFont(const char* const* paths, int count, int size, bool& owned) {
    for (int i = 0; i < count; ++i) {
        if (FileExists(paths[i])) {
            Font f = LoadFontEx(paths[i], size, nullptr, 0);
            if (f.texture.id != 0) {
                SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
                owned = true;
                return f;
            }
        }
    }
    owned = false;
    return GetFontDefault();
}

}  // namespace

void Renderer::init() {
    const char* titles[] = {"/System/Library/Fonts/Supplemental/Impact.ttf", "C:/Windows/Fonts/impact.ttf",
                            "/usr/share/fonts/truetype/msttcorefonts/Impact.ttf"};
    const char* ui[] = {"/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf",
                        "/System/Library/Fonts/Supplemental/Arial Black.ttf", "C:/Windows/Fonts/arialbd.ttf",
                        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"};
    titleFont_ = tryLoadFont(titles, 3, 128, ownTitleFont_);
    uiFont_ = tryLoadFont(ui, 4, 64, ownUiFont_);
}

void Renderer::shutdown() {
    if (ownTitleFont_) UnloadFont(titleFont_);
    if (ownUiFont_) UnloadFont(uiFont_);
}

void Renderer::reset(const Match& match) {
    const MatchState& s = match.state();
    camX_ = fxToFloat(s.f[0].pos.x + s.f[1].pos.x) / 2;
    zoom_ = 1.2f;
    shake_ = 0;
    particleCount_ = 0;
    for (Popup& p : popups_) p.life = 0;
    for (int i = 0; i < 2; ++i) {
        flash_[i] = 0;
        trail_[i] = s.f[i].health;
        comboTimer_[i] = 0;
        historyCount_[i] = 0;
    }
    banner_ = nullptr;
    advPending_ = advValid_ = false;
    lastMove_ = -1;
}

float Renderer::textWidth(const char* s, float size, bool title) const {
    const Font& f = title ? titleFont_ : uiFont_;
    return MeasureTextEx(f, s, size, size * 0.04f).x;
}

void Renderer::text(const char* s, float x, float y, float size, Color c, Align align, bool title, bool shadow) const {
    const Font& f = title ? titleFont_ : uiFont_;
    const float spacing = size * 0.04f;
    const float w = MeasureTextEx(f, s, size, spacing).x;
    if (align == Align::Center) x -= w / 2;
    if (align == Align::Right) x -= w;
    if (shadow) {
        const float o = std::max(2.0f, size * 0.05f);
        DrawTextEx(f, s, {x + o, y + o}, size, spacing, alpha(kOutline, c.a / 255.0f * 0.85f));
    }
    DrawTextEx(f, s, {x, y}, size, spacing, c);
}

Vector2 Renderer::toScreen(float wx, float wy) const {
    return {(wx - camX_) * zoom_ + kWidth / 2.0f + shakeOffset_.x, kGroundY - wy * zoom_ + shakeOffset_.y};
}

void Renderer::spawn(const Particle& p) {
    if (particleCount_ < kMaxParticles) particles_[particleCount_++] = p;
}

void Renderer::sparks(Vector2 at, int count, float speed, Color c, float dirX) {
    for (int i = 0; i < count; ++i) {
        const float a = (GetRandomValue(-70, 70) / 100.0f) + (dirX < 0 ? 3.14159f : 0);
        const float sp = speed * (0.4f + GetRandomValue(0, 100) / 100.0f);
        spawn({at, {std::cos(a) * sp, std::sin(a) * sp + 1.5f}, 0.25f, 0.25f, 2.5f + GetRandomValue(0, 20) / 10.0f, c, 0});
    }
}

void Renderer::onStep(const Match& match, SoundBank& sfx) {
    const MatchState& s = match.state();
    const MoveSet& ms = match.moves();

    for (int i = 0; i < match.events().count; ++i) {
        const Event& e = match.events().list[i];
        const Vector2 at{fxToFloat(e.at.x), fxToFloat(e.at.y)};
        const int def = 1 - e.who;
        const float dirX = static_cast<float>(sign(s.f[def].pos.x - s.f[e.who].pos.x));
        switch (e.type) {
            case EventType::Hit: {
                const bool heavy = e.damage >= 80;
                const Color c = e.counter ? Color{255, 210, 60, 255} : Color{255, 240, 200, 255};
                sparks(at, heavy ? 26 : 14, heavy ? 9.0f : 6.0f, c, dirX);
                spawn({at, {0, 0}, 0.22f, 0.22f, heavy ? 70.0f : 42.0f, heavy ? Color{255, 120, 60, 255} : c, 1});
                spawn({at, {0, 0}, 0.08f, 0.08f, heavy ? 38.0f : 24.0f, WHITE, 3});
                shake_ = std::max(shake_, heavy ? 7.0f : 2.5f);
                flash_[def] = 0.07f;
                sfx.play(heavy ? Sfx::HitHeavy : Sfx::HitLight, 0.9f, 0.95f + GetRandomValue(0, 10) / 100.0f);
                comboHits_[e.who] = e.combo;
                comboDamage_[e.who] = s.f[def].comboDamage;
                comboTimer_[e.who] = 1.6f;
                comboCounter_[e.who] = e.counter != 0;
                if (e.counter)
                    for (Popup& p : popups_)
                        if (p.life <= 0) {
                            p = {"COUNTER", {at.x, at.y + 40}, 0.9f, {255, 210, 60, 255}};
                            break;
                        }
                advPending_ = true;
                advAttacker_ = e.who;
                advBlocked_ = false;
                advRecovered_[0] = advRecovered_[1] = -1;
                break;
            }
            case EventType::Block:
                sparks(at, 10, 5.0f, {140, 200, 255, 255}, dirX);
                spawn({at, {0, 0}, 0.16f, 0.16f, 30.0f, {140, 200, 255, 255}, 1});
                shake_ = std::max(shake_, 1.5f);
                sfx.play(Sfx::Block, 0.8f);
                advPending_ = true;
                advAttacker_ = e.who;
                advBlocked_ = true;
                advRecovered_[0] = advRecovered_[1] = -1;
                break;
            case EventType::Whiff:
                if (e.move >= 0 && e.move < ms.count) {
                    const MoveData& m = ms.moves[e.move];
                    sfx.play(Sfx::Whoosh, 0.55f, m.damage >= 80 ? 0.75f : 1.15f);
                    if (e.who == 0) lastMove_ = e.move;
                }
                break;
            case EventType::Jump:
                sfx.play(Sfx::Jump, 0.4f);
                for (int k = 0; k < 6; ++k)
                    spawn({{fxToFloat(e.at.x), 2}, {GetRandomValue(-20, 20) / 10.0f, GetRandomValue(5, 15) / 10.0f}, 0.4f,
                           0.4f, 6, {200, 190, 180, 255}, 2});
                break;
            case EventType::Land:
                sfx.play(Sfx::Land, 0.5f);
                for (int k = 0; k < 8; ++k)
                    spawn({{fxToFloat(e.at.x), 2}, {GetRandomValue(-30, 30) / 10.0f, GetRandomValue(2, 10) / 10.0f}, 0.45f,
                           0.45f, 7, {200, 190, 180, 255}, 2});
                break;
            case EventType::Knockdown:
                sfx.play(Sfx::Thud, 0.9f);
                shake_ = std::max(shake_, 4.0f);
                for (int k = 0; k < 16; ++k)
                    spawn({{fxToFloat(e.at.x) + GetRandomValue(-40, 40), 2},
                           {GetRandomValue(-40, 40) / 10.0f, GetRandomValue(5, 25) / 10.0f}, 0.7f, 0.7f, 10,
                           {190, 180, 170, 255}, 2});
                break;
            case EventType::KO:
                sfx.play(Sfx::KO, 1.0f);
                shake_ = 14.0f;
                spawn({at, {0, 0}, 0.6f, 0.6f, 160.0f, {255, 80, 60, 255}, 1});
                spawn({at, {0, 0}, 0.15f, 0.15f, 90.0f, WHITE, 3});
                sparks(at, 40, 12.0f, {255, 230, 160, 255}, dirX);
                banner_ = "K.O.";
                bannerTimer_ = bannerDuration_ = 2.4f;
                bannerColor_ = {255, 70, 60, 255};
                koSlowmo_ = true;
                break;
            case EventType::RoundStart:
                std::snprintf(bannerBuf_, sizeof bannerBuf_, "ROUND %d", e.combo);
                banner_ = bannerBuf_;
                bannerTimer_ = bannerDuration_ = 1.2f;
                bannerColor_ = {255, 230, 150, 255};
                sfx.play(Sfx::Select, 0.7f);
                break;
            case EventType::Fight:
                banner_ = "FIGHT!";
                bannerTimer_ = bannerDuration_ = 0.9f;
                bannerColor_ = {255, 200, 60, 255};
                sfx.play(Sfx::HitHeavy, 0.6f, 1.4f);
                break;
            case EventType::TimeUp:
                banner_ = "TIME";
                bannerTimer_ = bannerDuration_ = 2.0f;
                bannerColor_ = {255, 230, 150, 255};
                break;
            case EventType::MatchOver:
                break;
        }
    }

    // Frame advantage: compare the frames each side regained control.
    if (advPending_) {
        for (int i = 0; i < 2; ++i)
            if (advRecovered_[i] < 0 && match.isActionable(i)) advRecovered_[i] = s.frame;
        if (advRecovered_[0] >= 0 && advRecovered_[1] >= 0) {
            const int a = advAttacker_;
            advResult_ = static_cast<int>(advRecovered_[1 - a] - advRecovered_[a]);
            advValid_ = true;
            advWasBlock_ = advBlocked_;
            advPending_ = false;
        }
    }

    for (int i = 0; i < 2; ++i) {
        const InputBits b = s.f[i].input.current();
        if (historyCount_[i] > 0 && history_[i][0].bits == b) {
            if (history_[i][0].frames < 99) ++history_[i][0].frames;
            continue;
        }
        for (int k = std::min(historyCount_[i], 19); k > 0; --k) history_[i][k] = history_[i][k - 1];
        history_[i][0] = {b, 1};
        historyCount_[i] = std::min(historyCount_[i] + 1, 20);
    }
}

void Renderer::update(const Match& match, float dt) {
    const MatchState& s = match.state();
    const float x0 = fxToFloat(s.f[0].pos.x), x1 = fxToFloat(s.f[1].pos.x);
    const float sep = std::fabs(x1 - x0);
    const float targetZoom = std::clamp(1280.0f / (sep + 560.0f), 0.92f, 1.25f);
    const float k = 1 - std::exp(-dt * 6);
    zoom_ += (targetZoom - zoom_) * k;
    const float half = kWidth / 2.0f / zoom_;
    const float targetX = std::clamp((x0 + x1) / 2, -kStageHalf - 60 + half, kStageHalf + 60 - half);
    camX_ += (targetX - camX_) * k;

    shakeTime_ += dt;
    shake_ = std::max(0.0f, shake_ - dt * 40);
    shakeOffset_ = {std::sin(shakeTime_ * 90) * shake_, std::cos(shakeTime_ * 77) * shake_ * 0.6f};

    for (int i = 0; i < 2; ++i) {
        flash_[i] = std::max(0.0f, flash_[i] - dt);
        comboTimer_[i] = std::max(0.0f, comboTimer_[i] - dt);
        const float h = s.f[i].health;
        if (h >= trail_[i]) {
            trail_[i] = h;
            trailDelay_[i] = 0.5f;
        } else if ((trailDelay_[i] -= dt) <= 0) {
            trail_[i] = std::max(h, trail_[i] - dt * 600);
        }
    }
    bannerTimer_ = std::max(0.0f, bannerTimer_ - dt);

    int n = 0;
    for (int i = 0; i < particleCount_; ++i) {
        Particle p = particles_[i];
        p.life -= dt;
        if (p.life <= 0) continue;
        p.p.x += p.v.x * dt * 60;
        p.p.y += p.v.y * dt * 60;
        if (p.kind == 0) {
            p.v.x *= 0.9f;
            p.v.y = p.v.y * 0.9f - 0.15f;
        } else if (p.kind == 2) {
            p.v.x *= 0.94f;
            p.v.y *= 0.94f;
        }
        particles_[n++] = p;
    }
    particleCount_ = n;
    for (Popup& p : popups_)
        if (p.life > 0) {
            p.life -= dt;
            p.p.y += dt * 50;
        }
}

void Renderer::drawBackground(float time) {
    DrawRectangleGradientV(0, 0, kWidth, static_cast<int>(kGroundY), {28, 18, 52, 255}, {236, 110, 84, 255});

    // Sun with horizontal bands, barely moves with the camera.
    const Vector2 sun{kWidth * 0.5f - camX_ * 0.04f, 300};
    for (int r = 0; r < 6; ++r) DrawCircleV(sun, 150.0f + r * 22, alpha({255, 170, 90, 255}, 0.06f));
    DrawCircleV(sun, 140, {255, 196, 112, 255});
    for (int b = 0; b < 6; ++b) {
        const float y = sun.y + 30 + b * 18;
        DrawRectangle(static_cast<int>(sun.x - 150), static_cast<int>(y), 300, 3 + b, mix({236, 110, 84, 255}, {200, 80, 90, 255}, b / 6.0f));
    }

    // Far skyline.
    for (int i = -14; i < 30; ++i) {
        const float w = 70 + hash01(i * 3) * 80;
        const float h = 120 + hash01(i * 5 + 1) * 210;
        const float x = i * 95 - camX_ * 0.25f * zoom_ + 100;
        DrawRectangle(static_cast<int>(x), static_cast<int>(kGroundY - 40 - h), static_cast<int>(w), static_cast<int>(h + 40),
                      {70, 36, 78, 255});
    }
    // Near skyline with windows and a neon sign.
    for (int i = -10; i < 22; ++i) {
        const float w = 110 + hash01(i * 7 + 2) * 90;
        const float h = 90 + hash01(i * 11 + 3) * 170;
        const float x = i * 150 - camX_ * 0.5f * zoom_ + 40;
        const float top = kGroundY - 60 - h;
        DrawRectangle(static_cast<int>(x), static_cast<int>(top), static_cast<int>(w), static_cast<int>(h + 60), {40, 22, 50, 255});
        for (int wy = 0; wy < static_cast<int>(h / 22); ++wy)
            for (int wx = 0; wx < static_cast<int>(w / 22); ++wx) {
                const float lit = hash01(i * 1000 + wy * 37 + wx);
                if (lit > 0.62f)
                    DrawRectangle(static_cast<int>(x + 8 + wx * 22), static_cast<int>(top + 10 + wy * 22), 9, 11,
                                  alpha({255, 214, 140, 255}, 0.35f + 0.5f * (lit - 0.62f)));
            }
        if (i == 3) {
            const bool on = std::fmod(time, 5.0f) > 0.15f && std::fmod(time, 5.0f) < 4.7f;
            const Color neon = on ? Color{255, 70, 140, 255} : Color{90, 40, 70, 255};
            if (on) DrawRectangle(static_cast<int>(x - 6), static_cast<int>(top - 48), static_cast<int>(w + 92), 40, alpha(neon, 0.15f));
            text("SOUTHPAW", x, top - 46, 36, neon, Align::Left, true, false);
        }
        if (i == 7) {  // antenna with blinking light
            DrawLineEx({x + w / 2, top}, {x + w / 2, top - 70}, 3, {40, 22, 50, 255});
            if (std::fmod(time, 1.4f) < 0.7f) DrawCircleV({x + w / 2, top - 72}, 4, {255, 60, 60, 255});
        }
    }

    // Rooftop floor with perspective lines.
    DrawRectangleGradientV(0, static_cast<int>(kGroundY), kWidth, kHeight - static_cast<int>(kGroundY), {62, 44, 70, 255},
                           {24, 16, 30, 255});
    DrawRectangle(0, static_cast<int>(kGroundY) - 2, kWidth, 4, {255, 150, 110, 140});
    const float vanishX = kWidth / 2.0f - camX_ * 0.2f;
    for (int i = -20; i <= 20; ++i) {
        const Vector2 a = toScreen(i * 90.0f, 0);
        const float bx = vanishX + (a.x - vanishX) * 2.6f;
        DrawLineEx({a.x, kGroundY + shakeOffset_.y}, {bx, static_cast<float>(kHeight)}, 1.5f, {255, 150, 110, 40});
    }
    for (int r = 1; r < 5; ++r) {
        const float y = kGroundY + r * r * 7.0f;
        DrawLineEx({0, y}, {static_cast<float>(kWidth), y}, 1, {255, 150, 110, 28});
    }
    // Railings at the stage edges.
    for (int side : {-1, 1}) {
        const float edge = side * (kStageHalf + 10);
        for (int k = 0; k < 6; ++k) {
            const Vector2 base = toScreen(edge + side * k * 30.0f, 0);
            DrawLineEx(base, {base.x, base.y - 70 * zoom_}, 5 * zoom_, {30, 18, 36, 255});
        }
        const Vector2 a = toScreen(edge, 70), b = toScreen(edge + side * 200.0f, 70);
        DrawLineEx(a, b, 6 * zoom_, {30, 18, 36, 255});
    }
}

void Renderer::drawFighter(const Match& match, int who, float time) {
    const Fighter& f = match.state().f[who];
    const Skeleton sk = buildSkeleton(match, who, time);
    const Palette& base = kPalettes[who];
    const float fl = flash_[who] > 0 ? 1.0f : 0.0f;
    const bool blink = f.invuln > 0 && (static_cast<int>(time * 30) & 1);
    const float a = blink ? 0.45f : 1.0f;
    auto tint = [&](Color c) { return alpha(mix(c, WHITE, fl * 0.5f), a); };
    const Color outline = alpha(kOutline, a);
    const float px = fxToFloat(f.pos.x), py = fxToFloat(f.pos.y);
    const float face = f.facing;
    auto W = [&](Vector2 l) { return toScreen(px + face * l.x, py + l.y); };
    const float z = zoom_;

    auto seg = [&](Vector2 p0, Vector2 p1, float w, Color c) {
        const Vector2 s0 = W(p0), s1 = W(p1);
        DrawLineEx(s0, s1, (w + 5) * z, outline);
        DrawCircleV(s0, (w + 5) * z / 2, outline);
        DrawCircleV(s1, (w + 5) * z / 2, outline);
        DrawLineEx(s0, s1, w * z, tint(c));
        DrawCircleV(s0, w * z / 2, tint(c));
        DrawCircleV(s1, w * z / 2, tint(c));
    };
    auto towards = [](Vector2 p0, Vector2 p1, float t) { return Vector2{p0.x + (p1.x - p0.x) * t, p0.y + (p1.y - p0.y) * t}; };
    auto leg = [&](Vector2 knee, Vector2 foot, bool front) {
        const Color skin = front ? base.skin : base.skinShade;
        const Color shorts = front ? base.primary : base.primaryShade;
        seg(sk.hip, knee, 17, skin);
        seg(knee, foot, 14, skin);
        seg(towards(knee, foot, 0.7f), foot, 15, front ? base.wraps : mix(base.wraps, kOutline, 0.25f));
        seg(sk.hip, towards(sk.hip, knee, 0.5f), 22, shorts);
        const Vector2 toe{foot.x + 13, foot.y + 1};
        seg(foot, toe, 9, front ? base.wraps : mix(base.wraps, kOutline, 0.25f));
    };
    auto arm = [&](Vector2 elbow, Vector2 hand, bool front) {
        const Color skin = front ? base.skin : base.skinShade;
        seg(sk.shoulder, elbow, 14, skin);
        seg(elbow, hand, 12, skin);
        const Vector2 h = W(hand);
        DrawCircleV(h, 13 * z, outline);
        DrawCircleV(h, 10.5f * z, tint(front ? base.primary : base.primaryShade));
        DrawCircleV({h.x - 3 * z * face, h.y - 3 * z}, 3.5f * z, alpha(WHITE, 0.45f * a));
    };

    // Shadow shrinks as the fighter rises.
    const Vector2 sh = toScreen(px, 0);
    const float hscale = std::max(0.35f, 1 - py / 260);
    DrawEllipse(static_cast<int>(sh.x), static_cast<int>(sh.y + 2), 46 * z * hscale, 9 * z * hscale, {0, 0, 0, 90});

    leg(sk.backKnee, sk.backFoot, false);
    arm(sk.backElbow, sk.backHand, false);

    // Torso, waistband and head.
    seg(sk.hip, sk.neck, 27, base.skin);
    const Vector2 chestA = towards(sk.hip, sk.neck, 0.55f), chestB = towards(sk.hip, sk.neck, 0.85f);
    DrawLineEx(W(chestA), W(chestB), 8 * z, tint(base.skinShade));
    seg(towards(sk.hip, sk.neck, -0.05f), towards(sk.hip, sk.neck, 0.2f), 30, base.primary);
    DrawLineEx(W(towards(sk.hip, sk.neck, 0.2f)), W(towards(sk.hip, sk.neck, 0.26f)), 30 * z, tint(base.accent));

    const Vector2 head = W(sk.head);
    DrawCircleV(head, (sk.headRadius + 2.5f) * z, outline);
    DrawCircleV(head, sk.headRadius * z, tint(base.skin));
    // Hair covers the back and top of the head (screen angles: 0 right, 270 up).
    const float hairFrom = (face > 0 ? 160.0f : 230.0f) + sk.lean * -face;
    DrawCircleSector(head, sk.headRadius * z, hairFrom, hairFrom + 150, 14, tint(base.hair));
    // Headband with tails that stream behind.
    const float leanR = sk.lean * 3.14159f / 180;
    const Vector2 fwd{std::cos(leanR), -std::sin(leanR)};
    const Vector2 bandBack{sk.head.x - fwd.x * sk.headRadius, sk.head.y + 4 - fwd.y * sk.headRadius};
    DrawLineEx(W({sk.head.x - fwd.x * sk.headRadius, sk.head.y + 4}), W({sk.head.x + fwd.x * sk.headRadius, sk.head.y + 4}),
               5 * z, tint(base.band));
    for (int t = 0; t < 2; ++t) {
        const float wave = std::sin(time * 9 + t * 1.3f + who) * 5;
        const Vector2 end{bandBack.x - 20 - t * 4, bandBack.y - 6 - t * 5 + wave};
        DrawLineEx(W(bandBack), W(end), 4 * z, tint(base.band));
    }
    const Vector2 eye = W({sk.head.x + fwd.x * 8, sk.head.y + 2});
    DrawRectangleV({eye.x - 1.5f * z, eye.y - 2 * z}, {3 * z, 4 * z}, outline);

    leg(sk.frontKnee, sk.frontFoot, true);
    arm(sk.frontElbow, sk.frontHand, true);
}

void Renderer::drawDebug(const Match& match, int who) {
    const Fighter& f = match.state().f[who];
    const MoveSet& ms = match.moves();
    auto rect = [&](const Box& b, Color fill, Color line) {
        const Vector2 tl = toScreen(fxToFloat(b.x), fxToFloat(b.y + b.h));
        const Rectangle r{tl.x, tl.y, fxToFloat(b.w) * zoom_, fxToFloat(b.h) * zoom_};
        DrawRectangleRec(r, fill);
        DrawRectangleLinesEx(r, 2, line);
    };
    rect(match.hurtbox(who), {60, 220, 120, 45}, {60, 220, 120, 220});
    const fx pw = ms.fighter.pushWidth;
    const Vector2 p0 = toScreen(fxToFloat(f.pos.x - pw), fxToFloat(f.pos.y + toFx(90)));
    DrawRectangleLinesEx({p0.x, p0.y, fxToFloat(2 * pw) * zoom_, 90 * zoom_}, 1, {255, 220, 80, 160});

    Box hb;
    if (match.activeBox(who, hb)) rect(localToWorld(hb, f.pos, f.facing), {255, 50, 60, 110}, {255, 70, 80, 255});
    ArcRegion arc;
    if (match.activeArc(who, arc)) {
        const MoveData& m = ms.moves[f.move];
        const Vector2 c = toScreen(fxToFloat(f.pos.x + f.facing * arc.cx), fxToFloat(f.pos.y + arc.cy));
        auto screenAngles = [&](int lo, int hi, float& s0, float& s1) {
            if (f.facing > 0) { s0 = -static_cast<float>(hi); s1 = -static_cast<float>(lo); }
            else { s0 = static_cast<float>(lo) - 180; s1 = static_cast<float>(hi) - 180; }
        };
        float s0, s1;
        const int fullLo = std::min(m.hit.a0, m.hit.a1), fullHi = std::max(m.hit.a0, m.hit.a1);
        screenAngles(fullLo, fullHi, s0, s1);
        DrawRing(c, fxToFloat(arc.r0) * zoom_, fxToFloat(arc.r1) * zoom_, s0, s1, 32, {255, 60, 70, 35});
        screenAngles(arc.lo, arc.hi, s0, s1);
        DrawRing(c, fxToFloat(arc.r0) * zoom_, fxToFloat(arc.r1) * zoom_, s0, s1, 16, {255, 50, 60, 140});
    }
    const Vector2 o = toScreen(fxToFloat(f.pos.x), fxToFloat(f.pos.y));
    DrawLineEx({o.x - 6, o.y}, {o.x + 6, o.y}, 2, WHITE);
    DrawLineEx({o.x, o.y - 6}, {o.x, o.y + 6}, 2, WHITE);

    char buf[96];
    if (f.state == FState::Attack && f.move >= 0 && f.move < ms.count) {
        const MoveData& m = ms.moves[f.move];
        const char* phase = f.moveFrame < m.startup ? "startup" : (f.moveFrame < m.startup + m.active ? "ACTIVE" : "recovery");
        std::snprintf(buf, sizeof buf, "%s  %d/%d %s", m.name, f.moveFrame + 1, m.total(), phase);
    } else if (f.stun > 0) {
        std::snprintf(buf, sizeof buf, "%s  stun %d", stateName(f.state), f.stun);
    } else {
        std::snprintf(buf, sizeof buf, "%s  f%d", stateName(f.state), f.stateFrame);
    }
    const Vector2 top = toScreen(fxToFloat(f.pos.x), fxToFloat(f.pos.y) + 200);
    text(buf, top.x, top.y - who * 24, 20, who == 0 ? Color{255, 200, 200, 235} : Color{200, 220, 255, 235}, Align::Center);
}

void Renderer::drawWorld(const Match& match, const HudOptions& opt, float time) {
    drawBackground(time);
    // The attacker is drawn on top.
    const bool p2First = match.state().f[0].state == FState::Attack || match.state().f[1].state != FState::Attack;
    if (p2First) {
        drawFighter(match, 1, time);
        drawFighter(match, 0, time);
    } else {
        drawFighter(match, 0, time);
        drawFighter(match, 1, time);
    }

    for (int i = 0; i < particleCount_; ++i) {
        const Particle& p = particles_[i];
        const float t = p.life / p.maxLife;
        const Vector2 s = toScreen(p.p.x, p.p.y);
        switch (p.kind) {
            case 0: {
                const Vector2 tail = toScreen(p.p.x - p.v.x * 2.5f, p.p.y - p.v.y * 2.5f);
                DrawLineEx(s, tail, p.size * zoom_ * t + 1, alpha(p.color, t));
                break;
            }
            case 1: {
                const float r = p.size * (1.2f - t) * zoom_;
                DrawRing(s, r * 0.8f, r, 0, 360, 36, alpha(p.color, t));
                break;
            }
            case 2:
                DrawCircleV(s, p.size * zoom_ * (1.5f - t * 0.5f), alpha(p.color, t * 0.35f));
                break;
            case 3:
                DrawCircleV(s, p.size * zoom_, alpha(p.color, t));
                break;
        }
    }
    for (const Popup& p : popups_)
        if (p.life > 0) {
            const Vector2 s = toScreen(p.p.x, p.p.y);
            text(p.text, s.x, s.y, 30, alpha(p.color, std::min(1.0f, p.life * 2)), Align::Center, true);
        }

    if (opt.showHitboxes) {
        drawDebug(match, 0);
        drawDebug(match, 1);
    }
}

void Renderer::drawHealth(const Match& match, const HudOptions& opt, float time) {
    const MatchState& s = match.state();
    const float maxH = match.moves().fighter.health;
    const float barW = 500, barH = 28, y = 30;
    for (int i = 0; i < 2; ++i) {
        const float x = i == 0 ? 40 : kWidth - 40 - barW;
        const float pct = std::max(0.0f, s.f[i].health / maxH);
        const float trail = std::max(0.0f, trail_[i] / maxH);
        DrawRectangleRounded({x - 4, y - 4, barW + 8, barH + 8}, 0.3f, 6, kOutline);
        DrawRectangle(static_cast<int>(x), static_cast<int>(y), static_cast<int>(barW), static_cast<int>(barH), {60, 20, 30, 255});
        // Bars are anchored at the centre of the screen and drain outward-in.
        auto fill = [&](float p, Color top, Color bottom) {
            const float w = barW * p;
            const float fx0 = i == 0 ? x + barW - w : x;
            DrawRectangleGradientV(static_cast<int>(fx0), static_cast<int>(y), static_cast<int>(w), static_cast<int>(barH), top, bottom);
        };
        fill(trail, {255, 90, 80, 255}, {200, 40, 40, 255});
        const bool low = pct < 0.25f && static_cast<int>(time * 4) % 2 == 0;
        fill(pct, low ? Color{255, 120, 90, 255} : Color{255, 236, 120, 255}, low ? Color{230, 60, 50, 255} : Color{240, 170, 40, 255});
        DrawRectangle(static_cast<int>(x), static_cast<int>(y + 3), static_cast<int>(barW), 3, {255, 255, 255, 50});

        const float nameX = i == 0 ? x : x + barW;
        text(opt.names[i], nameX, y + barH + 6, 30, WHITE, i == 0 ? Align::Left : Align::Right);
        for (int w = 0; w < match.config().roundsToWin && !opt.training; ++w) {
            const float cx = i == 0 ? x + barW - 14 - w * 26 : x + 14 + w * 26;
            DrawCircleV({cx, y + barH + 22}, 9, kOutline);
            DrawCircleV({cx, y + barH + 22}, 7, w < s.wins[i] ? Color{255, 200, 60, 255} : Color{70, 50, 70, 255});
        }
    }
    // Timer.
    DrawRectangleRounded({kWidth / 2.0f - 44, 16, 88, 64}, 0.25f, 6, kOutline);
    char buf[16];
    if (opt.training) std::snprintf(buf, sizeof buf, "--");
    else std::snprintf(buf, sizeof buf, "%02d", (s.timer + 59) / 60);
    const bool urgent = !opt.training && s.timer < 10 * 60 && s.phase == Phase::Fight;
    text(buf, kWidth / 2.0f, 18, 58, urgent ? Color{255, 90, 80, 255} : WHITE, Align::Center, true);
}

void Renderer::drawArrow(InputBits bits, Vector2 c, float r, Color col) const {
    const int h = horizontal(bits);
    const int v = ((bits & IN_UP) ? 1 : 0) - ((bits & IN_DOWN) ? 1 : 0);
    if (h == 0 && v == 0) {
        DrawCircleV(c, 3, alpha(col, 0.5f));
        return;
    }
    float dx = static_cast<float>(h), dy = static_cast<float>(-v);
    const float len = std::sqrt(dx * dx + dy * dy);
    dx /= len;
    dy /= len;
    const Vector2 tip{c.x + dx * r, c.y + dy * r};
    const Vector2 tail{c.x - dx * r * 0.7f, c.y - dy * r * 0.7f};
    DrawLineEx(tail, tip, 3, col);
    const Vector2 l{tip.x - dx * 7 - dy * 6, tip.y - dy * 7 + dx * 6};
    const Vector2 rr{tip.x - dx * 7 + dy * 6, tip.y - dy * 7 - dx * 6};
    DrawTriangle(tip, l, rr, col);
    DrawTriangle(tip, rr, l, col);
}

void Renderer::drawInputHistory(int who, float x, float y) {
    static const char* names[4] = {"LP", "HP", "LK", "HK"};
    static const Color colors[4] = {{90, 170, 255, 255}, {255, 90, 90, 255}, {120, 230, 140, 255}, {255, 200, 70, 255}};
    DrawRectangleRounded({x - 8, y - 8, 150, 20 * 22.0f + 16}, 0.08f, 6, {10, 6, 16, 150});
    for (int k = 0; k < historyCount_[who]; ++k) {
        const InputEntry& e = history_[who][k];
        const float ry = y + k * 22;
        const float fade = 1 - k / 22.0f;
        char buf[8];
        std::snprintf(buf, sizeof buf, "%2d", e.frames);
        text(buf, x, ry, 18, alpha({200, 200, 210, 255}, fade), Align::Left, false, false);
        drawArrow(e.bits, {x + 42, ry + 9}, 8, alpha(WHITE, fade));
        float bx = x + 62;
        for (int b = 0; b < 4; ++b)
            if (e.bits & buttonBit(b)) {
                DrawCircleV({bx + 9, ry + 9}, 10, alpha(colors[b], fade));
                text(names[b], bx + 9, ry + 1, 15, alpha(kOutline, fade), Align::Center, false, false);
                bx += 20;
            }
    }
}

void Renderer::drawFrameData(const Match& match, const HudOptions& opt) {
    const MoveSet& ms = match.moves();
    const float x = kWidth - 320, y = kHeight - 190;
    DrawRectangleRounded({x, y, 300, 150}, 0.08f, 6, {10, 6, 16, 170});
    char buf[96];
    if (lastMove_ >= 0 && lastMove_ < ms.count) {
        const MoveData& m = ms.moves[lastMove_];
        text(m.name, x + 14, y + 8, 26, {255, 220, 140, 255});
        std::snprintf(buf, sizeof buf, "startup %d  active %d  recovery %d", m.startup, m.active, m.recovery);
        text(buf, x + 14, y + 38, 18, WHITE, Align::Left, false, false);
        std::snprintf(buf, sizeof buf, "damage %d   %s   %s", m.damage,
                      m.guard == Guard::Low ? "LOW" : (m.guard == Guard::High ? "OVERHEAD" : "MID"),
                      m.hit.shape == Shape::Arc ? "ARC" : "BOX");
        text(buf, x + 14, y + 60, 18, WHITE, Align::Left, false, false);
    } else {
        text("throw out a move", x + 14, y + 8, 22, {200, 200, 210, 255});
    }
    if (advValid_) {
        std::snprintf(buf, sizeof buf, "%+d on %s", advResult_, advWasBlock_ ? "block" : "hit");
        text(buf, x + 14, y + 84, 26, advResult_ >= 0 ? Color{120, 230, 140, 255} : Color{255, 110, 100, 255});
    }
    std::snprintf(buf, sizeof buf, "buffer %df [F6]   dummy: %s [F7]", opt.bufferFrames, opt.dummyLabel ? opt.dummyLabel : "-");
    text(buf, x + 14, y + 120, 17, {200, 200, 210, 255}, Align::Left, false, false);
}

void Renderer::drawHud(const Match& match, const HudOptions& opt, float time) {
    drawHealth(match, opt, time);

    for (int i = 0; i < 2; ++i) {
        if (comboTimer_[i] <= 0 || comboHits_[i] < 2) continue;
        const float a = std::min(1.0f, comboTimer_[i] * 2);
        const float x = i == 0 ? 50.0f : kWidth - 50.0f;
        const Align al = i == 0 ? Align::Left : Align::Right;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%d HITS", comboHits_[i]);
        text(buf, x, 200, 64, alpha({255, 210, 70, 255}, a), al, true);
        std::snprintf(buf, sizeof buf, "%d DAMAGE", comboDamage_[i]);
        text(buf, x, 268, 28, alpha(WHITE, a), al);
    }

    if (banner_ && bannerTimer_ > 0 && match.state().phase != Phase::MatchEnd) {
        const float t = 1 - bannerTimer_ / bannerDuration_;
        const float scale = t < 0.12f ? 2.2f - 1.2f * (t / 0.12f) : 1.0f;
        const float a = bannerTimer_ < 0.25f ? bannerTimer_ / 0.25f : 1.0f;
        const float size = 120 * scale;
        DrawRectangle(0, static_cast<int>(kHeight / 2.0f - 70), kWidth, 140, alpha({0, 0, 0, 255}, 0.35f * a));
        text(banner_, kWidth / 2.0f, kHeight / 2.0f - size / 2, size, alpha(bannerColor_, a), Align::Center, true);
    }
    const MatchState& s = match.state();
    if (s.phase == Phase::MatchEnd) {
        char buf[48];
        if (s.matchWinner >= 0) std::snprintf(buf, sizeof buf, "%s WINS", opt.names[s.matchWinner]);
        else std::snprintf(buf, sizeof buf, "DRAW");
        DrawRectangle(0, static_cast<int>(kHeight / 2.0f - 80), kWidth, 160, {0, 0, 0, 140});
        text(buf, kWidth / 2.0f, kHeight / 2.0f - 70, 110, {255, 210, 70, 255}, Align::Center, true);
    }

    if (opt.banner) text(opt.banner, kWidth / 2.0f, 92, 26, {255, 220, 140, 255}, Align::Center);
    if (opt.training) {
        drawInputHistory(0, 24, 120);
        drawFrameData(match, opt);
    } else if (opt.showInputs) {
        drawInputHistory(0, 24, 120);
        drawInputHistory(1, kWidth - 166, 120);
    }
    if (opt.status) text(opt.status, kWidth - 20, kHeight - 70, 22, opt.statusColor, Align::Right);
    if (opt.footer) text(opt.footer, kWidth / 2.0f, kHeight - 34, 20, {230, 220, 235, 220}, Align::Center);
}

}  // namespace sp
