#pragma once

#include <atomic>

namespace atrium {

// Process-wide shell state: tray workers must not dereference a UI-owned
// SystemChrome while restoring a temporary reveal (including during exit).
struct TaskbarVisibility {
    std::atomic_bool hidden = false;
    std::atomic_bool stopping = false;
    std::atomic_uint reveals = 0;

    bool ShouldHide() const { return hidden && !stopping && reveals == 0; }
};

} // namespace atrium
