// Cyclic Stage strip paging, including precision wheels and horizontal touchpad input.
#pragma once

#include <cstddef>

namespace atrium {

class StageScroll {
public:
    size_t Offset() const { return offset_; }
    void Reset() { offset_ = 0; vertical_ = horizontal_ = 0; }
    void Fit(size_t count) { if (count) offset_ %= count; else Reset(); }
    void Anchor(size_t offset, size_t count) { offset_ = count ? offset % count : 0; }
    size_t Index(size_t slot, size_t count) const { return count ? (offset_ + slot) % count : 0; }

    bool Wheel(int delta, bool horizontal, size_t count)
    {
        if (count < 2) {
            Reset();
            return false;
        }
        int& remainder = horizontal ? horizontal_ : vertical_;
        remainder += delta;
        const int steps = remainder / 120;
        remainder %= 120;
        const auto old = offset_;
        const auto n = static_cast<long long>(count);
        const auto move = horizontal ? steps : -steps;
        offset_ = size_t((static_cast<long long>(offset_ % count) + move % n + n) % n);
        return old != offset_;
    }

private:
    size_t offset_ = 0;
    int vertical_ = 0, horizontal_ = 0;
};

} // namespace atrium
