#include "ui/stage_scroll.h"
#include "ui/stage_open.h"

#include <iostream>
#include <stdexcept>

using namespace atrium;

int main()
{
    try {
        auto require = [](bool condition, const char* message) {
            if (!condition)
                throw std::runtime_error(message);
        };
        StageOpenIntent intent;
        intent.Arm(L"editor", 100);
        require(!intent.Matches(L"browser", 101), "a launch intent never targets another app");
        require(intent.Matches(L"editor", 10099), "the requested app joins when its window arrives");
        require(!intent.Matches(L"editor", 10100), "a stale launch intent expires");
        intent.Arm(L"editor", 20000);
        intent.Cancel();
        require(!intent.Matches(L"editor", 20001), "disable or stage switching cancels a launch intent");
        StageScroll scroll;
        require(!scroll.Wheel(-120, false, 0) && scroll.Offset() == 0, "empty strip does not scroll");
        require(!scroll.Wheel(-120, false, 1), "one group does not scroll");
        require(scroll.Wheel(-120, false, 10) && scroll.Offset() == 1, "wheel down advances beyond the visible page");
        require(scroll.Index(5, 10) == 6, "viewport indexes the entire list, not just six cards");
        require(scroll.Wheel(240, false, 10) && scroll.Offset() == 9, "wheel up wraps past the start");
        require(scroll.Index(1, 10) == 0, "viewport wraps at the end");
        scroll.Reset();
        for (int i = 0; i < 3; ++i)
            require(!scroll.Wheel(-30, false, 8), "partial notches must accumulate");
        require(scroll.Wheel(-30, false, 8) && scroll.Offset() == 1, "four precision deltas equal one notch");
        require(!scroll.Wheel(30, false, 8), "opposite partial delta waits");
        require(!scroll.Wheel(-30, false, 8), "reversing partial input cancels its remainder");
        require(scroll.Wheel(120, true, 8) && scroll.Offset() == 2, "horizontal right advances");
        require(scroll.Wheel(-360, true, 8) && scroll.Offset() == 7, "horizontal left wraps");
        scroll.Fit(3);
        require(scroll.Offset() == 1, "shrinking list normalizes offset");
        scroll.Anchor(2, 3);
        require(scroll.Index(0, 3) == 2, "rebuild preserves the first visible identity");
        scroll.Reset();
        require(scroll.Offset() == 0 && !scroll.Wheel(-90, false, 3), "reset clears both offset and fractional input");
        require(scroll.Wheel(-32760, false, 8) && scroll.Offset() < 8, "large wheel input stays bounded");
        scroll.Fit(0);
        require(scroll.Offset() == 0 && scroll.Index(4, 0) == 0, "empty rebuild is safe");
        std::cout << "PASS: cyclic Stage scrolling, precision input, horizontal input and rebuilds\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
