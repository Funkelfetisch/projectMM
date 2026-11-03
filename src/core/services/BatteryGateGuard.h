#pragma once

#include <mutex>
#include <utility>

namespace mm {
/// Serialize the divider gate with shutdown, including a worker that outlives
/// the platform's bounded task stop. ADC reads never hold this lock.
class BatteryGateGuard {
public:
    template <typename Action>
    bool whileActive(Action&& action) {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_ && std::forward<Action>(action)();
    }
    template <typename Action>
    auto stop(Action&& action) {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        return std::forward<Action>(action)();
    }
private:
    std::mutex mutex_;
    bool active_ = true;
};
} // namespace mm
