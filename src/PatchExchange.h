#pragma once
#include "Patch.h"
#include <array>
#include <atomic>
#include <memory>

namespace zyg {
// Control operations (publish/flush/latest) require external serialization.
// The audio thread only calls activate(), never copies/releases shared_ptrs.
// One publication can be in flight; later edits coalesce on the control side.
class PatchExchange {
public:
    explicit PatchExchange(std::shared_ptr<const Patch> initial) { slots_[0] = std::move(initial); }

    void publish(std::shared_ptr<const Patch> patch) {
        pending_ = std::move(patch);
        flush();
    }
    void flush() {
        if (!pending_) return;
        const int active = active_.load(std::memory_order_acquire);
        if (requested_.load(std::memory_order_relaxed) != active) return;
        const int next = (active + 1) % 3;
        slots_[std::size_t(next)] = std::move(pending_);
        requested_.store(next, std::memory_order_release);
    }
    std::shared_ptr<const Patch> latest() const {
        return pending_ ? pending_ : slots_[std::size_t(requested_.load(std::memory_order_acquire))];
    }
    template<class Bind> bool activate(Bind&& bind) noexcept {
        const int requested = requested_.load(std::memory_order_acquire);
        if (requested == active_.load(std::memory_order_relaxed)) return false;
        bind(slots_[std::size_t(requested)].get());
        active_.store(requested, std::memory_order_release);
        return true;
    }
private:
    std::array<std::shared_ptr<const Patch>, 3> slots_;
    std::shared_ptr<const Patch> pending_;
    std::atomic<int> active_ {0}, requested_ {0};
};
}
