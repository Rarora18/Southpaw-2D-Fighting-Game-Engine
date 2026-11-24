// Southpaw front end: menus, local versus, CPU, training mode, replay viewer
// and hot reload of data/moves.json. The simulation itself lives in src/core
// and knows nothing about raylib.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#include "core/ai.h"
#include "core/move_data.h"
#include "core/replay.h"
#include "core/sim.h"
#include "game/renderer.h"
#include "game/sfx.h"
#include "raylib.h"
#include "rlgl.h"

using namespace sp;

namespace {

constexpr double kStep = 1.0 / 60.0;
constexpr size_t kReplayReserve = 60 * 60 * 15;  // 15 minutes of input

InputBits readKeys(int player) {
    struct Map {
        int up, down, left, right, lp, hp, lk, hk;
    };
    static const Map maps[2] = {
        {KEY_W, KEY_S, KEY_A, KEY_D, KEY_U, KEY_I, KEY_J, KEY_K},
        {KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_KP_4, KEY_KP_5, KEY_KP_1, KEY_KP_2},
    };
    const Map& m = maps[player];
    InputBits b = 0;
    if (IsKeyDown(m.up)) b |= IN_UP;
    if (IsKeyDown(m.down)) b |= IN_DOWN;
    if (IsKeyDown(m.left)) b |= IN_LEFT;
    if (IsKeyDown(m.right)) b |= IN_RIGHT;
    if (IsKeyDown(m.lp)) b |= IN_LP;
    if (IsKeyDown(m.hp)) b |= IN_HP;
    if (IsKeyDown(m.lk)) b |= IN_LK;
    if (IsKeyDown(m.hk)) b |= IN_HK;
    if (player == 1) {  // laptop-friendly alternative for player 2
        if (IsKeyDown(KEY_COMMA)) b |= IN_LP;
        if (IsKeyDown(KEY_PERIOD)) b |= IN_HP;
        if (IsKeyDown(KEY_M)) b |= IN_LK;
        if (IsKeyDown(KEY_SLASH)) b |= IN_HK;
    }
    return b;
}

InputBits readPad(int pad) {
    if (!IsGamepadAvailable(pad)) return 0;
    InputBits b = 0;
    const float ax = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X);
    const float ay = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_Y);
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_UP) || ay < -0.5f) b |= IN_UP;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || ay > 0.5f) b |= IN_DOWN;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || ax < -0.5f) b |= IN_LEFT;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || ax > 0.5f) b |= IN_RIGHT;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) b |= IN_LP;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP)) b |= IN_HP;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) b |= IN_LK;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) b |= IN_HK;
    return b;
}

bool menuUp() { return IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_W) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP); }
bool menuDown() { return IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_S) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN); }
bool menuLeft() { return IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_A) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT); }
bool menuRight() { return IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_D) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT); }
bool menuOk() {
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_U) ||
           IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
}
bool menuBack() { return IsKeyPressed(KEY_ESCAPE) || IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT); }

std::string resolveDataPath(const std::string& override) {
    if (!override.empty()) return override;
    if (FileExists("data/moves.json")) return "data/moves.json";
    return std::string(SOUTHPAW_DATA_DIR) + "/moves.json";
}

std::string moveNotation(const MoveData& m) {
    static const char* buttons[4] = {"LP", "HP", "LK", "HK"};
    std::string s;
    if (m.stance == Stance::Air) s = "(air) ";
    if (m.motion) s += "QCF + ";
    else if (m.stance == Stance::Crouch) s += "DOWN + ";
    else if (m.dir == DirReq::Forward) s += "FWD + ";
    else if (m.dir == DirReq::Back) s += "BACK + ";
    return s + buttons[m.button];
}

enum class Screen { Title, Fight, Replay, Controls };
enum class Mode { VsCpu, Versus, Training };
enum class Dummy { Stand, Block, Crouch, Jump, Cpu, Count };
const char* const kDummyNames[] = {"STAND", "BLOCK ALL", "CROUCH", "JUMP", "CPU"};
const char* const kLevelNames[] = {"EASY", "NORMAL", "HARD", "BRUTAL"};

class App {
public:
    bool init(const std::string& dataOverride, const std::string& replayPath);
    void run();
    void shutdown();
    void enableCapture(const std::string& dir) { captureDir_ = dir; }

private:
    const char* captureUpdate();
    void checkHotReload(bool force);
    void toast(const std::string& msg, Color c, float seconds = 3.0f);
    void startFight(Mode mode);
    void startReplay();
    void tickFight();
    void tickReplay();
    void tickTitleDemo();
    InputBits dummyInput();

    void updateTitle();
    void updateFight(float dt);
    void updateReplay(float dt);
    void drawTitle(float time);
    void drawFight(float time);
    void drawReplay(float time);
    void drawControls();
    void drawToast();
    void saveReplay();

    MoveSet moves_;
    std::string dataPath_;
    long dataMTime_ = 0;
    float reloadTimer_ = 0;

    Renderer renderer_;
    SoundBank sfx_;
    Screen screen_ = Screen::Title;
    Mode mode_ = Mode::VsCpu;
    int menuIndex_ = 0;
    int cpuLevel_ = 1;
    bool quit_ = false;

    Match match_;
    CpuBrain cpu_;
    Replay recording_;
    bool recordingValid_ = true;
    Replay lastReplay_;
    bool hasReplay_ = false;
    bool matchEndHandled_ = false;
    double accumulator_ = 0;
    float slowmo_ = 0;
    bool paused_ = false;
    int pauseIndex_ = 0;
    bool showHitboxes_ = false;
    bool showInputs_ = false;
    bool simFrozen_ = false;  // training: F3
    Dummy dummy_ = Dummy::Stand;

    Match demo_;
    CpuBrain demoCpu_[2];

    Match replayMatch_;
    size_t replayCursor_ = 0;
    float replaySpeed_ = 1;
    bool replayPaused_ = false;
    int64_t replayMismatch_ = -1;

    std::string toast_;
    Color toastColor_ = WHITE;
    float toastTimer_ = 0;

    // --capture: the game plays itself and saves screenshots of each feature.
    std::string captureDir_;
    int capStep_ = 0;
    int capWait_ = 0;
    bool autoP1_ = false;
    CpuBrain autoCpu_;
    int capCombo_ = 0;
    bool capKO_ = false;
    float fastForward_ = 1;
};

bool App::init(const std::string& dataOverride, const std::string& replayPath) {
    dataPath_ = resolveDataPath(dataOverride);
    std::string err;
    if (!loadMoveSet(dataPath_, moves_, err)) {
        std::fprintf(stderr, "southpaw: %s\n", err.c_str());
        return false;
    }
    dataMTime_ = GetFileModTime(dataPath_.c_str());

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(Renderer::kWidth, Renderer::kHeight, "Southpaw");
    SetExitKey(KEY_NULL);
    SetTargetFPS(144);
    renderer_.init();
    sfx_.init();

    MatchConfig demoCfg;
    demoCfg.roundFrames = 45 * 60;
    demo_.reset(moves_, demoCfg);
    demoCpu_[0].reset(static_cast<uint32_t>(std::time(nullptr)), 2);
    demoCpu_[1].reset(static_cast<uint32_t>(std::time(nullptr)) * 31 + 7, 2);
    renderer_.reset(demo_);

    if (!replayPath.empty()) {
        if (lastReplay_.load(replayPath, err)) {
            hasReplay_ = true;
            startReplay();
        } else {
            toast(err, {255, 110, 100, 255}, 5);
        }
    }
    return true;
}

void App::shutdown() {
    sfx_.shutdown();
    renderer_.shutdown();
    CloseWindow();
}

void App::toast(const std::string& msg, Color c, float seconds) {
    toast_ = msg;
    toastColor_ = c;
    toastTimer_ = seconds;
}

void App::checkHotReload(bool force) {
    const long mtime = GetFileModTime(dataPath_.c_str());
    if (!force && mtime == dataMTime_) return;
    dataMTime_ = mtime;
    MoveSet fresh;
    std::string err;
    if (!loadMoveSet(dataPath_, fresh, err)) {
        toast("moves.json NOT reloaded: " + err, {255, 110, 100, 255}, 6);
        return;
    }
    if (fresh.hash == moves_.hash) {
        if (force) toast("moves.json unchanged", {200, 200, 210, 255});
        return;
    }
    moves_ = fresh;
    match_.setMoves(moves_);
    demo_.setMoves(moves_);
    replayMatch_.setMoves(moves_);
    if (screen_ == Screen::Fight) recordingValid_ = false;  // the inputs no longer describe this data
    char buf[96];
    std::snprintf(buf, sizeof buf, "moves.json hot-reloaded  (%d moves, hash %08llx)", moves_.count,
                  static_cast<unsigned long long>(moves_.hash & 0xFFFFFFFFull));
    toast(buf, {120, 230, 140, 255});
}

void App::startFight(Mode mode) {
    mode_ = mode;
    MatchConfig cfg;
    cfg.training = mode == Mode::Training;
    match_.reset(moves_, cfg);
    recording_.begin(cfg, moves_.hash, kReplayReserve);
    recordingValid_ = true;
    matchEndHandled_ = false;
    cpu_.reset(static_cast<uint32_t>(std::time(nullptr)) | 1u, cpuLevel_);
    renderer_.reset(match_);
    accumulator_ = 0;
    slowmo_ = 0;
    paused_ = false;
    simFrozen_ = false;
    screen_ = Screen::Fight;
}

void App::startReplay() {
    replayMatch_.reset(moves_, lastReplay_.config);
    replayCursor_ = 0;
    replaySpeed_ = 1;
    replayPaused_ = false;
    replayMismatch_ = -1;
    accumulator_ = 0;
    slowmo_ = 0;
    renderer_.reset(replayMatch_);
    screen_ = Screen::Replay;
    if (lastReplay_.movesHash != moves_.hash)
        toast("move data changed since this replay was recorded - expect a desync", {255, 200, 90, 255}, 5);
}

InputBits App::dummyInput() {
    const Fighter& p1 = match_.state().f[0];
    const Fighter& p2 = match_.state().f[1];
    const InputBits away = p1.pos.x < p2.pos.x ? IN_RIGHT : IN_LEFT;
    switch (dummy_) {
        case Dummy::Stand: return 0;
        case Dummy::Crouch: return IN_DOWN;
        case Dummy::Jump: return IN_UP;
        case Dummy::Cpu: return cpu_.think(match_, 1);
        case Dummy::Block:
            if (p1.state == FState::Attack && p1.move >= 0) {
                const MoveData& m = moves_.moves[p1.move];
                return m.guard == Guard::Low ? static_cast<InputBits>(away | IN_DOWN) : away;
            }
            return p2.state == FState::Blockstun ? away : 0;
        case Dummy::Count: break;
    }
    return 0;
}

void App::tickFight() {
    const InputBits p1 = autoP1_ ? autoCpu_.think(match_, 0) : static_cast<InputBits>(readKeys(0) | readPad(0));
    InputBits p2 = 0;
    switch (mode_) {
        case Mode::Versus: p2 = static_cast<InputBits>(readKeys(1) | readPad(1)); break;
        case Mode::VsCpu: p2 = cpu_.think(match_, 1); break;
        case Mode::Training: p2 = dummyInput(); break;
    }
    match_.step(p1, p2);
    if (recording_.frames() < kReplayReserve) recording_.record(p1, p2, match_.hash());
    renderer_.onStep(match_, sfx_);
    if (renderer_.consumeKoSlowmo()) slowmo_ = 1.5f;
    for (int i = 0; i < match_.events().count; ++i) {
        const Event& e = match_.events().list[i];
        if (e.type == EventType::Hit && e.combo > capCombo_) capCombo_ = e.combo;
        if (e.type == EventType::KO) capKO_ = true;
    }
}

void App::tickReplay() {
    if (replayCursor_ >= lastReplay_.frames()) return;
    replayMatch_.step(lastReplay_.p1(replayCursor_), lastReplay_.p2(replayCursor_));
    if (replayMismatch_ < 0 && replayMatch_.hash() != lastReplay_.checksums[replayCursor_])
        replayMismatch_ = static_cast<int64_t>(replayCursor_);
    ++replayCursor_;
    renderer_.onStep(replayMatch_, sfx_);
    if (renderer_.consumeKoSlowmo()) slowmo_ = 1.5f;
}

void App::tickTitleDemo() {
    demo_.step(demoCpu_[0].think(demo_, 0), demoCpu_[1].think(demo_, 1));
    if (demo_.state().phase == Phase::MatchEnd && demo_.state().phaseTimer == 0) {
        static int restartDelay = 0;
        if (++restartDelay > 180) {
            restartDelay = 0;
            demo_.reset(moves_, demo_.config());
            renderer_.reset(demo_);
        }
    }
    SoundBank silent;  // the attract demo runs muted
    renderer_.onStep(demo_, silent);
}

void App::saveReplay() {
    if (!DirectoryExists("replays")) MakeDirectory("replays");
    char name[64];
    const std::time_t now = std::time(nullptr);
    std::strftime(name, sizeof name, "replays/southpaw_%Y%m%d_%H%M%S.sprp", std::localtime(&now));
    std::string err;
    if (lastReplay_.save(name, err)) toast(std::string("saved ") + name, {120, 230, 140, 255});
    else toast(err, {255, 110, 100, 255});
}

// ---------------------------------------------------------------- update

void App::updateTitle() {
    constexpr int kItems = 6;
    if (menuUp()) { menuIndex_ = (menuIndex_ + kItems - 1) % kItems; sfx_.play(Sfx::Menu); }
    if (menuDown()) { menuIndex_ = (menuIndex_ + 1) % kItems; sfx_.play(Sfx::Menu); }
    if (menuIndex_ == 0 && (menuLeft() || menuRight())) {
        cpuLevel_ = (cpuLevel_ + (menuRight() ? 1 : 3)) % 4;
        sfx_.play(Sfx::Menu);
    }
    if (!menuOk()) return;
    sfx_.play(Sfx::Select);
    switch (menuIndex_) {
        case 0: startFight(Mode::VsCpu); break;
        case 1: startFight(Mode::Versus); break;
        case 2: dummy_ = Dummy::Stand; startFight(Mode::Training); break;
        case 3:
            if (hasReplay_) startReplay();
            else toast("finish a match first (or run with --replay file.sprp)", {255, 200, 90, 255});
            break;
        case 4: screen_ = Screen::Controls; break;
        case 5: quit_ = true; break;
    }
}

void App::updateFight(float dt) {
    if (IsKeyPressed(KEY_F1)) showHitboxes_ = !showHitboxes_;
    if (IsKeyPressed(KEY_F2)) showInputs_ = !showInputs_;

    if (paused_) {
        if (menuUp()) { pauseIndex_ = (pauseIndex_ + 3) % 4; sfx_.play(Sfx::Menu); }
        if (menuDown()) { pauseIndex_ = (pauseIndex_ + 1) % 4; sfx_.play(Sfx::Menu); }
        if (menuBack()) paused_ = false;
        else if (menuOk()) {
            sfx_.play(Sfx::Select);
            switch (pauseIndex_) {
                case 0: paused_ = false; break;
                case 1: startFight(mode_); break;
                case 2: showHitboxes_ = !showHitboxes_; break;
                case 3: screen_ = Screen::Title; renderer_.reset(demo_); break;
            }
        }
        return;
    }

    const bool over = match_.state().phase == Phase::MatchEnd;
    if (over) {
        if (!matchEndHandled_) {
            matchEndHandled_ = true;
            if (recordingValid_) {
                lastReplay_ = recording_;
                hasReplay_ = true;
            }
        }
        if (IsKeyPressed(KEY_ENTER)) { startFight(mode_); return; }
        if (IsKeyPressed(KEY_R) && hasReplay_) { startReplay(); return; }
        if (IsKeyPressed(KEY_S) && hasReplay_ && recordingValid_) saveReplay();
        if (menuBack()) { screen_ = Screen::Title; renderer_.reset(demo_); return; }
    } else if (menuBack()) {
        if (mode_ == Mode::Training) { screen_ = Screen::Title; renderer_.reset(demo_); return; }
        paused_ = true;
        pauseIndex_ = 0;
        return;
    }

    if (mode_ == Mode::Training) {
        if (IsKeyPressed(KEY_F3)) simFrozen_ = !simFrozen_;
        if (IsKeyPressed(KEY_F5)) checkHotReload(true);
        if (IsKeyPressed(KEY_F6)) {
            const uint8_t b = match_.config().bufferFrames ? 0 : 6;
            match_.setBufferFrames(b);
            recordingValid_ = false;
            toast(b ? "input buffer ON (6 frames)" : "input buffer OFF - combos need frame-perfect timing",
                  b ? Color{120, 230, 140, 255} : Color{255, 200, 90, 255});
        }
        if (IsKeyPressed(KEY_F7)) {
            dummy_ = static_cast<Dummy>((static_cast<int>(dummy_) + 1) % static_cast<int>(Dummy::Count));
            cpu_.reset(static_cast<uint32_t>(std::time(nullptr)) | 1u, 2);
        }
        if (IsKeyPressed(KEY_F8)) {
            const uint8_t b = match_.config().bufferFrames;
            startFight(Mode::Training);
            match_.setBufferFrames(b);
        }
        if (simFrozen_) {
            if (IsKeyPressed(KEY_F4)) tickFight();
            renderer_.update(match_, dt * 0.25f);
            return;
        }
    }

    if (slowmo_ > 0) slowmo_ -= dt;
    const double scale = slowmo_ > 0 ? 0.3 : 1.0;
    accumulator_ += dt * scale * fastForward_;
    int guard = 0;
    while (accumulator_ >= kStep && guard++ < 8) {
        tickFight();
        accumulator_ -= kStep;
    }
    renderer_.update(match_, dt * static_cast<float>(scale));
}

void App::updateReplay(float dt) {
    if (IsKeyPressed(KEY_F1)) showHitboxes_ = !showHitboxes_;
    if (menuBack()) { screen_ = Screen::Title; renderer_.reset(demo_); return; }
    if (IsKeyPressed(KEY_SPACE)) replayPaused_ = !replayPaused_;
    if (IsKeyPressed(KEY_R)) { startReplay(); return; }
    if (IsKeyPressed(KEY_UP)) replaySpeed_ = std::fmin(8.0f, replaySpeed_ * 2);
    if (IsKeyPressed(KEY_DOWN)) replaySpeed_ = std::fmax(0.25f, replaySpeed_ / 2);
    if (replayPaused_) {
        if (IsKeyPressed(KEY_RIGHT)) tickReplay();
        renderer_.update(replayMatch_, dt * 0.25f);
        return;
    }
    if (slowmo_ > 0) slowmo_ -= dt;
    const double scale = (slowmo_ > 0 ? 0.3 : 1.0) * replaySpeed_;
    accumulator_ += dt * scale;
    int guard = 0;
    while (accumulator_ >= kStep && guard++ < 64) {
        tickReplay();
        accumulator_ -= kStep;
    }
    renderer_.update(replayMatch_, dt * static_cast<float>(std::fmin(scale, 2.0)));
}

// ---------------------------------------------------------------- draw

void App::drawToast() {
    if (toastTimer_ <= 0) return;
    const float a = std::fmin(1.0f, toastTimer_ * 2);
    const float w = renderer_.textWidth(toast_.c_str(), 22) + 40;
    DrawRectangleRounded({Renderer::kWidth / 2.0f - w / 2, 128, w, 38}, 0.4f, 8, {10, 6, 16, static_cast<unsigned char>(200 * a)});
    Color c = toastColor_;
    c.a = static_cast<unsigned char>(255 * a);
    renderer_.text(toast_.c_str(), Renderer::kWidth / 2.0f, 136, 22, c, Align::Center);
}

void App::drawTitle(float time) {
    HudOptions opt;
    renderer_.drawWorld(demo_, opt, time);
    DrawRectangleGradientV(0, 0, Renderer::kWidth, Renderer::kHeight, {10, 6, 20, 150}, {10, 6, 20, 230});

    const float bob = std::sin(time * 2) * 4;
    renderer_.text("SOUTHPAW", Renderer::kWidth / 2.0f + 6, 70 + bob + 6, 150, {214, 40, 57, 255}, Align::Center, true, false);
    renderer_.text("SOUTHPAW", Renderer::kWidth / 2.0f, 70 + bob, 150, {255, 236, 200, 255}, Align::Center, true);
    renderer_.text("2D FIGHTING GAME ENGINE  -  C++17 / RAYLIB", Renderer::kWidth / 2.0f, 236, 24, {255, 200, 150, 220}, Align::Center);

    char vsCpu[48];
    std::snprintf(vsCpu, sizeof vsCpu, "VS CPU   <  %s  >", kLevelNames[cpuLevel_]);
    const char* items[6] = {vsCpu, "VS PLAYER", "TRAINING", hasReplay_ ? "WATCH LAST REPLAY" : "WATCH LAST REPLAY  (none)",
                            "CONTROLS & MOVES", "QUIT"};
    for (int i = 0; i < 6; ++i) {
        const bool sel = i == menuIndex_;
        const float y = 300 + i * 52.0f;
        if (sel) {
            const float w = renderer_.textWidth(items[i], 40) + 60;
            DrawRectangleRounded({Renderer::kWidth / 2.0f - w / 2, y - 4, w, 48}, 0.3f, 8, {214, 40, 57, 200});
        }
        renderer_.text(items[i], Renderer::kWidth / 2.0f, y, 40, sel ? WHITE : Color{220, 200, 220, 200}, Align::Center);
    }
    renderer_.text("ARROWS / WASD to move   ENTER to select", Renderer::kWidth / 2.0f, Renderer::kHeight - 40, 20,
                   {200, 190, 210, 200}, Align::Center);
    char buf[96];
    std::snprintf(buf, sizeof buf, "data: %s  (%d moves, hot reload on)", dataPath_.c_str(), moves_.count);
    renderer_.text(buf, 16, Renderer::kHeight - 26, 16, {160, 150, 170, 200}, Align::Left, false, false);
}

void App::drawFight(float time) {
    HudOptions opt;
    opt.training = mode_ == Mode::Training;
    opt.showHitboxes = showHitboxes_;
    opt.showInputs = showInputs_;
    opt.bufferFrames = match_.config().bufferFrames;
    opt.dummyLabel = kDummyNames[static_cast<int>(dummy_)];
    if (mode_ == Mode::VsCpu) opt.names[1] = "ORTHODOX (CPU)";
    if (opt.training) {
        opt.banner = simFrozen_ ? "TRAINING  -  PAUSED (F4 step)" : "TRAINING";
        opt.footer = "F1 boxes  F3 pause  F4 step  F5 reload  F6 buffer  F7 dummy  F8 reset  ESC menu";
    } else if (match_.state().phase == Phase::MatchEnd) {
        opt.footer = recordingValid_ ? "ENTER rematch    R watch replay    S save replay    ESC menu"
                                     : "ENTER rematch    ESC menu    (replay unavailable: data changed mid-match)";
    } else {
        opt.footer = "ESC pause    F1 hitboxes    F2 input display";
    }
    renderer_.drawWorld(match_, opt, time);
    renderer_.drawHud(match_, opt, time);

    if (paused_) {
        DrawRectangle(0, 0, Renderer::kWidth, Renderer::kHeight, {10, 6, 20, 170});
        renderer_.text("PAUSED", Renderer::kWidth / 2.0f, 190, 90, WHITE, Align::Center, true);
        const char* items[4] = {"RESUME", "RESTART", showHitboxes_ ? "HITBOXES: ON" : "HITBOXES: OFF", "MAIN MENU"};
        for (int i = 0; i < 4; ++i) {
            const bool sel = i == pauseIndex_;
            const float y = 320 + i * 52.0f;
            if (sel) {
                const float w = renderer_.textWidth(items[i], 38) + 60;
                DrawRectangleRounded({Renderer::kWidth / 2.0f - w / 2, y - 4, w, 46}, 0.3f, 8, {214, 40, 57, 200});
            }
            renderer_.text(items[i], Renderer::kWidth / 2.0f, y, 38, sel ? WHITE : Color{220, 200, 220, 200}, Align::Center);
        }
    }
}

void App::drawReplay(float time) {
    HudOptions opt;
    opt.showHitboxes = showHitboxes_;
    opt.showInputs = true;
    char banner[64];
    std::snprintf(banner, sizeof banner, "REPLAY  %.2gx%s", replaySpeed_, replayPaused_ ? "  PAUSED" : "");
    opt.banner = banner;
    opt.footer = "SPACE pause   RIGHT step   UP/DOWN speed   R restart   F1 hitboxes   ESC menu";
    char status[128];
    if (replayMismatch_ >= 0) {
        std::snprintf(status, sizeof status, "DESYNC at frame %lld", static_cast<long long>(replayMismatch_));
        opt.statusColor = {255, 110, 100, 255};
    } else {
        std::snprintf(status, sizeof status, "frame %zu / %zu   state hash %016llx  VERIFIED", replayCursor_, lastReplay_.frames(),
                      static_cast<unsigned long long>(replayMatch_.hash()));
        opt.statusColor = {120, 230, 140, 255};
    }
    opt.status = status;
    renderer_.drawWorld(replayMatch_, opt, time);
    renderer_.drawHud(replayMatch_, opt, time);
}

void App::drawControls() {
    DrawRectangleGradientV(0, 0, Renderer::kWidth, Renderer::kHeight, {28, 18, 52, 255}, {60, 24, 50, 255});
    renderer_.text("CONTROLS", 80, 40, 64, WHITE, Align::Left, true);
    const char* lines[] = {
        "PLAYER 1     move W A S D      LP U    HP I    LK J    HK K",
        "PLAYER 2     move ARROWS       LP , / NUM4   HP . / NUM5   LK M / NUM1   HK / / NUM2",
        "GAMEPAD      d-pad / stick     LP X   HP Y   LK A   HK B",
        "",
        "Hold BACK to block. Hold DOWN-BACK to block lows. Overheads must be blocked standing.",
        "Cross-ups flip the guard direction. Moves listed with cancels can chain on hit or block.",
    };
    float y = 130;
    for (const char* l : lines) {
        renderer_.text(l, 80, y, 24, {230, 220, 235, 255}, Align::Left, false, false);
        y += 32;
    }
    renderer_.text("MOVE LIST  (loaded from moves.json)", 80, y + 10, 34, {255, 220, 140, 255}, Align::Left, true);
    y += 60;
    for (int i = 0; i < moves_.count; ++i) {
        const MoveData& m = moves_.moves[i];
        const float x = i < 7 ? 80.0f : 660.0f;
        const float ry = y + (i % 7) * 30.0f;
        char buf[128];
        std::snprintf(buf, sizeof buf, "%-16s %-14s %2d/%d/%d", m.name, moveNotation(m).c_str(), m.startup, m.active, m.recovery);
        renderer_.text(m.name, x, ry, 24, WHITE, Align::Left, false, false);
        renderer_.text(moveNotation(m).c_str(), x + 200, ry, 24, {255, 200, 150, 255}, Align::Left, false, false);
        std::snprintf(buf, sizeof buf, "%d / %d / %d", m.startup, m.active, m.recovery);
        renderer_.text(buf, x + 400, ry, 24, {170, 200, 255, 255}, Align::Left, false, false);
    }
    renderer_.text("frame data shown as startup / active / recovery      ESC back", Renderer::kWidth / 2.0f,
                   Renderer::kHeight - 40, 20, {200, 190, 210, 200}, Align::Center);
    if (menuBack() || menuOk()) screen_ = Screen::Title;
}

const char* App::captureUpdate() {
    if (captureDir_.empty()) return nullptr;
    ++capWait_;
    auto next = [&] {
        ++capStep_;
        capWait_ = 0;
    };
    auto anyActive = [&](const Match& m) {
        Box b;
        ArcRegion a;
        for (int i = 0; i < 2; ++i)
            if (m.activeBox(i, b) || m.activeArc(i, a)) return true;
        return false;
    };
    switch (capStep_) {
        case 0:
            if (capWait_ == 200) { next(); return "01_title.png"; }
            break;
        case 1:
            cpuLevel_ = 3;
            autoP1_ = true;
            autoCpu_.reset(4242, 3);
            startFight(Mode::VsCpu);
            capCombo_ = 0;
            fastForward_ = 3;
            next();
            break;
        case 2:
            if (capWait_ > 150 && (capCombo_ >= 3 || (capWait_ > 600 && capCombo_ >= 2))) { next(); return "02_combo.png"; }
            if (capCombo_ < 3 && capWait_ % 30 == 0) capCombo_ = 0;
            break;
        case 3:
            showHitboxes_ = true;
            fastForward_ = 1;
            if (capWait_ > 30 && anyActive(match_)) {
                next();
                return "03_hitboxes.png";
            }
            break;
        case 4:
            showHitboxes_ = false;
            if (!capKO_) {
                fastForward_ = 6;
                capWait_ = 0;
            } else {
                fastForward_ = 1;
                if (capWait_ == 30) { next(); return "04_ko.png"; }
            }
            break;
        case 5:
            fastForward_ = 6;
            capKO_ = false;
            if (match_.state().phase != Phase::MatchEnd) capWait_ = 0;
            if (capWait_ == 60) { next(); return "05_match_end.png"; }
            break;
        case 6:
            fastForward_ = 1;
            if (hasReplay_) saveReplay();
            startReplay();
            showHitboxes_ = true;
            next();
            break;
        case 7:
            if (capWait_ > 400 && anyActive(replayMatch_)) { next(); return "06_replay.png"; }
            break;
        case 8:
            dummy_ = Dummy::Block;
            startFight(Mode::Training);
            autoCpu_.reset(99, 3);
            next();
            break;
        case 9:
            if (capWait_ > 300 && anyActive(match_)) { next(); return "07_training.png"; }
            break;
        case 10:
            showHitboxes_ = false;
            screen_ = Screen::Controls;
            next();
            break;
        case 11:
            if (capWait_ == 10) { next(); return "08_controls.png"; }
            break;
        default:
            quit_ = true;
            break;
    }
    return nullptr;
}

void App::run() {
    while (!WindowShouldClose() && !quit_) {
        const float dt = std::fmin(GetFrameTime(), 0.1f);
        const float time = static_cast<float>(GetTime());
        const char* shot = captureUpdate();
        toastTimer_ -= dt;
        if ((reloadTimer_ -= dt) <= 0) {
            reloadTimer_ = 0.5f;
            checkHotReload(false);
        }

        switch (screen_) {
            case Screen::Title:
                updateTitle();
                accumulator_ += dt;
                while (accumulator_ >= kStep) {
                    tickTitleDemo();
                    accumulator_ -= kStep;
                }
                renderer_.update(demo_, dt);
                break;
            case Screen::Fight: updateFight(dt); break;
            case Screen::Replay: updateReplay(dt); break;
            case Screen::Controls: break;
        }

        BeginDrawing();
        ClearBackground(BLACK);
        switch (screen_) {
            case Screen::Title: drawTitle(time); break;
            case Screen::Fight: drawFight(time); break;
            case Screen::Replay: drawReplay(time); break;
            case Screen::Controls: drawControls(); break;
        }
        drawToast();
        if (shot) {
            rlDrawRenderBatchActive();
            Image img = LoadImageFromScreen();
            ExportImage(img, (captureDir_ + "/" + shot).c_str());
            UnloadImage(img);
        }
        EndDrawing();
    }
}

int verifyHeadless(const std::string& dataOverride, const std::string& path) {
    MoveSet moves;
    std::string err;
    const std::string dataPath = resolveDataPath(dataOverride);
    if (!loadMoveSet(dataPath, moves, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    Replay r;
    if (!r.load(path, err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 2;
    }
    if (r.movesHash != moves.hash) std::printf("warning: replay was recorded with different move data\n");
    const auto t0 = std::chrono::steady_clock::now();
    const VerifyResult v = verifyReplay(r, moves);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (v.ok)
        std::printf("OK  %llu frames re-simulated, every state hash matched (%.1f ms). final hash %016llx\n",
                    static_cast<unsigned long long>(v.frames), secs * 1000, static_cast<unsigned long long>(v.finalHash));
    else
        std::printf("DESYNC at frame %lld of %zu\n", static_cast<long long>(v.firstMismatch), r.frames());
    return v.ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string dataOverride, replayPath, verifyPath, captureDir;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--data" && i + 1 < argc) dataOverride = argv[++i];
        else if (a == "--capture" && i + 1 < argc) captureDir = argv[++i];
        else if (a == "--replay" && i + 1 < argc) replayPath = argv[++i];
        else if (a == "--verify" && i + 1 < argc) verifyPath = argv[++i];
        else {
            std::printf("usage: southpaw [--data moves.json] [--replay file.sprp] [--verify file.sprp] [--capture dir]\n");
            return a == "--help" || a == "-h" ? 0 : 2;
        }
    }
    if (!verifyPath.empty()) return verifyHeadless(dataOverride, verifyPath);

    SetTraceLogLevel(LOG_WARNING);
    App app;
    if (!app.init(dataOverride, replayPath)) return 1;
    if (!captureDir.empty()) {
        if (!DirectoryExists(captureDir.c_str())) MakeDirectory(captureDir.c_str());
        app.enableCapture(captureDir);
    }
    app.run();
    app.shutdown();
    return 0;
}
