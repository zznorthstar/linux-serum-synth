#include "PatchExchange.h"
#include <chrono>
#include <iostream>
#include <thread>

int main() {
    auto initial = std::make_shared<zyg::Patch>();
    zyg::PatchExchange exchange(initial);
    std::atomic<bool> stop {false}, corrupt {false};
    std::atomic<std::uint64_t> heard {0};
    std::thread audio([&] {
        const zyg::Patch* bound = initial.get();
        while (!stop.load(std::memory_order_acquire)) {
            exchange.activate([&](const zyg::Patch* patch) noexcept { bound = patch; });
            if (bound->presetId) {
                for (float x : bound->oscillators[0].audio)
                    if (x != float(bound->presetId)) corrupt.store(true);
                heard.store(bound->presetId, std::memory_order_release);
            }
            std::this_thread::yield();
        }
    });
    constexpr std::uint64_t last = 10000;
    for (std::uint64_t id = 1; id <= last; ++id) {
        auto patch = std::make_shared<zyg::Patch>();
        patch->presetId = id;
        patch->oscillators[0].audio.assign(257, float(id));
        exchange.publish(std::move(patch));
        if (exchange.latest()->presetId != id) corrupt.store(true);
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (heard.load(std::memory_order_acquire) != last && std::chrono::steady_clock::now() < deadline) {
        exchange.flush();
        std::this_thread::yield();
    }
    stop.store(true, std::memory_order_release);
    audio.join();
    if (corrupt.load() || heard.load() != last) {
        std::cerr << "Patch handoff corruption or missing final publication\n"; return 1;
    }
    std::cout << "10000 concurrent patch publications passed\n";
}
