#include "core/input.h"

namespace sp {

void InputBuffer::clear() {
    for (auto& h : held) h = 0;
    count = 0;
    for (auto& c : consumed) c = -1;
}

void InputBuffer::push(InputBits in) {
    held[count & (kHistory - 1)] = in;
    ++count;
}

InputBits InputBuffer::at(int ago) const {
    if (ago < 0 || static_cast<uint32_t>(ago) >= count || ago >= kHistory) return 0;
    return held[(count - 1 - static_cast<uint32_t>(ago)) & (kHistory - 1)];
}

InputBits InputBuffer::pressedAt(int ago) const {
    return static_cast<InputBits>(at(ago) & ~at(ago + 1));
}

int InputBuffer::findPress(int button, int window) const {
    const InputBits bit = buttonBit(button);
    const int limit = window < kHistory - 2 ? window : kHistory - 2;
    for (int ago = 0; ago <= limit; ++ago) {
        if (static_cast<uint32_t>(ago) >= count) break;
        const int32_t frame = static_cast<int32_t>(count - 1) - ago;
        if (frame <= consumed[button]) break;  // anything older was already used
        if (pressedAt(ago) & bit) return ago;
    }
    return -1;
}

void InputBuffer::consume(int button, int ago) {
    consumed[button] = static_cast<int32_t>(count - 1) - ago;
}

bool InputBuffer::quarterCircleForward(int facing, int ago, int window) const {
    int stage = 0;  // scanning backwards: forward, then down-forward, then down
    for (int k = ago; k <= ago + window && k < kHistory && static_cast<uint32_t>(k) < count; ++k) {
        const InputBits d = at(k);
        const int fwd = horizontal(d) * facing;
        const bool down = (d & IN_DOWN) != 0;
        if (stage == 0 && fwd > 0 && !down) stage = 1;
        else if (stage == 1 && fwd > 0 && down) stage = 2;
        else if (stage == 2 && down && fwd <= 0) return true;
    }
    return false;
}

}  // namespace sp
