#pragma once
// Sound effects synthesised at startup, so the game ships with no audio assets.
#include "raylib.h"

namespace sp {

enum class Sfx { HitLight, HitHeavy, Block, Whoosh, Jump, Land, Thud, KO, Menu, Select, Count };

class SoundBank {
public:
    void init();
    void shutdown();
    void play(Sfx s, float volume = 1.0f, float pitch = 1.0f);

private:
    Sound sounds_[static_cast<int>(Sfx::Count)] = {};
    bool ready_ = false;
};

}  // namespace sp
