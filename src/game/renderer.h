#pragma once
#include <cstdint>

#include "core/sim.h"
#include "game/sfx.h"
#include "raylib.h"

namespace sp {

struct HudOptions {
    const char* names[2] = {"SOUTHPAW", "ORTHODOX"};
    bool training = false;
    bool showHitboxes = false;
    bool showInputs = false;
    const char* banner = nullptr;   // top-centre mode label, e.g. "TRAINING"
    const char* footer = nullptr;   // bottom help line
    const char* status = nullptr;   // bottom-right status (replay verification)
    Color statusColor = WHITE;
    int bufferFrames = 6;
    const char* dummyLabel = nullptr;
};

enum class Align { Left, Center, Right };

class Renderer {
public:
    static constexpr int kWidth = 1280;
    static constexpr int kHeight = 720;

    void init();
    void shutdown();
    void reset(const Match& match);

    // Call once per simulation step: turns events into effects and sounds and
    // tracks frame advantage / input history.
    void onStep(const Match& match, SoundBank& sfx);
    void update(const Match& match, float dt);
    void drawWorld(const Match& match, const HudOptions& opt, float time);
    void drawHud(const Match& match, const HudOptions& opt, float time);

    bool consumeKoSlowmo() {
        const bool v = koSlowmo_;
        koSlowmo_ = false;
        return v;
    }

    void text(const char* s, float x, float y, float size, Color c, Align align = Align::Left, bool title = false,
              bool shadow = true) const;
    float textWidth(const char* s, float size, bool title = false) const;

private:
    struct Particle {
        Vector2 p, v;  // world pixels, y up
        float life, maxLife, size;
        Color color;
        uint8_t kind;  // 0 spark, 1 ring, 2 dust, 3 flash
    };
    struct Popup {
        const char* text;
        Vector2 p;
        float life;
        Color color;
    };
    struct InputEntry {
        InputBits bits;
        int frames;
    };

    Vector2 toScreen(float wx, float wy) const;
    void spawn(const Particle& p);
    void sparks(Vector2 at, int count, float speed, Color c, float dirX);
    void drawBackground(float time);
    void drawFighter(const Match& match, int who, float time);
    void drawDebug(const Match& match, int who);
    void drawHealth(const Match& match, const HudOptions& opt, float time);
    void drawInputHistory(int who, float x, float y);
    void drawFrameData(const Match& match, const HudOptions& opt);
    void drawArrow(InputBits bits, Vector2 c, float r, Color col) const;

    Font titleFont_{};
    Font uiFont_{};
    bool ownTitleFont_ = false, ownUiFont_ = false;

    float camX_ = 0, zoom_ = 1.2f, shake_ = 0, shakeTime_ = 0;
    Vector2 shakeOffset_{0, 0};
    static constexpr int kMaxParticles = 512;
    Particle particles_[kMaxParticles] = {};
    int particleCount_ = 0;
    Popup popups_[8] = {};
    float flash_[2] = {0, 0};
    float trail_[2] = {1000, 1000};
    float trailDelay_[2] = {0, 0};
    int comboHits_[2] = {0, 0};
    int comboDamage_[2] = {0, 0};
    float comboTimer_[2] = {0, 0};
    bool comboCounter_[2] = {false, false};
    const char* banner_ = nullptr;
    char bannerBuf_[32] = {};
    float bannerTimer_ = 0, bannerDuration_ = 1;
    Color bannerColor_ = WHITE;
    bool koSlowmo_ = false;

    InputEntry history_[2][20] = {};
    int historyCount_[2] = {0, 0};

    // Frame advantage: after a hit/block, wait for both fighters to recover.
    bool advPending_ = false;
    int advAttacker_ = 0;
    bool advBlocked_ = false;
    int64_t advRecovered_[2] = {-1, -1};
    int advResult_ = 0;
    bool advValid_ = false;
    bool advWasBlock_ = false;
    int lastMove_ = -1;
};

}  // namespace sp
