#pragma once
#include <cstdint>

namespace sp {

using InputBits = uint16_t;

enum : InputBits {
    IN_UP = 1 << 0,
    IN_DOWN = 1 << 1,
    IN_LEFT = 1 << 2,
    IN_RIGHT = 1 << 3,
    IN_LP = 1 << 4,
    IN_HP = 1 << 5,
    IN_LK = 1 << 6,
    IN_HK = 1 << 7,
};

constexpr int kButtonCount = 4;  // LP, HP, LK, HK
constexpr InputBits buttonBit(int button) { return static_cast<InputBits>(IN_LP << button); }

// -1 left, 0 neutral (including left+right held together), +1 right.
inline int horizontal(InputBits in) { return ((in & IN_RIGHT) ? 1 : 0) - ((in & IN_LEFT) ? 1 : 0); }

// Fixed-size ring of recent inputs. Lives inside the fighter state so it is
// hashed and replayed with everything else; never allocates.
struct InputBuffer {
    static constexpr int kHistory = 32;  // power of two

    InputBits held[kHistory];
    uint32_t count;                  // frames pushed so far
    int32_t consumed[kButtonCount];  // absolute frame of the last press that started a move

    void clear();
    void push(InputBits in);

    InputBits at(int ago) const;  // held bits `ago` frames back (0 = current frame)
    InputBits pressedAt(int ago) const;
    InputBits current() const { return at(0); }

    // Youngest unconsumed press of `button` within the last `window` frames,
    // returned as frames-ago, or -1. window = 0 means "this frame only".
    int findPress(int button, int window) const;
    void consume(int button, int ago);

    // Quarter-circle forward (down, down-forward, forward) completed at or
    // before frame `ago`, spanning at most `window` frames.
    bool quarterCircleForward(int facing, int ago, int window) const;
};

}  // namespace sp
