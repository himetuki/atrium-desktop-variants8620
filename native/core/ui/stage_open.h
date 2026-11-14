#pragma once

#include <cstdint>
#include <string>

namespace atrium {

// A launch from the bar joins the stage once its window arrives, not whichever app opens next.
class StageOpenIntent {
public:
    void Arm(const std::wstring& app, uint64_t now) { app_ = app; until_ = now + 10000; }
    void Cancel() { app_.clear(); until_ = 0; }
    bool Matches(const std::wstring& app, uint64_t now)
    {
        if (now >= until_)
            Cancel();
        return !app_.empty() && app_ == app;
    }
private:
    std::wstring app_;
    uint64_t until_ = 0;
};

} // namespace atrium
