#include "hand_device_hardware_fixture.hpp"
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace linkerbot::hand;
using namespace linkerbot::hand::hardware_test;
using namespace std::chrono_literals;

namespace {
    double process_cpu_seconds() {
#ifdef _WIN32
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
            throw std::runtime_error("GetProcessTimes failed");
        }
        const auto ticks = [](const FILETIME &time) {
            return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
        };
        return static_cast<double>(ticks(kernel) + ticks(user)) / 1e7;
#else
        const auto ticks = std::clock();
        if (ticks == static_cast<std::clock_t>(-1)) {
            throw std::runtime_error("Process CPU time unavailable");
        }
        return static_cast<double>(ticks) / CLOCKS_PER_SEC;
#endif
    }

    double required_limit(const char *name, double minimum, double maximum) {
        const auto text = env(name);
        if (!text) {
            throw std::invalid_argument(std::string{"Freeze acceptance limit first: "} + name);
        }
        size_t consumed = 0;
        const auto value = std::stod(*text, &consumed);
        if (consumed != text->size() || !std::isfinite(value) || value < minimum || value > maximum) {
            throw std::invalid_argument(std::string{"Invalid acceptance limit: "} + name);
        }
        return value;
    }
}

TEST_CASE("HandDevice hardware sustained angle read load", "[hardware][device][load][manual][read-only]") {
    if (env("HAND_DEVICE_ENABLE_LOAD_TESTS") != "1") {
        SKIP("set HAND_DEVICE_ENABLE_LOAD_TESTS=1 after freezing workload and limits");
    }
    // All options are required, so an unknown hardware combination cannot pass
    // against guessed release thresholds. This profile never sends motion.
    const auto seconds = required_limit("HAND_DEVICE_LOAD_SECONDS", 600, 86400);
    const auto hz = required_limit("HAND_DEVICE_LOAD_HZ", 0.1, 1000);
    REQUIRE(seconds * hz <= 2000000); // Bound retained latency samples to 16 MB.
    const auto timeout =
        std::chrono::milliseconds{static_cast<int64_t>(required_limit("HAND_DEVICE_LOAD_TIMEOUT_MS", 1, 60000))};
    const auto min_completion = required_limit("HAND_DEVICE_LOAD_MIN_COMPLETION_PERCENT", 0.01, 100);
    const auto max_p99 = required_limit("HAND_DEVICE_LOAD_MAX_P99_MS", 0.01, 60000);
    const auto max_cpu = required_limit("HAND_DEVICE_LOAD_MAX_CPU_PERCENT", 0, 100000);
    const auto max_timeouts = required_limit("HAND_DEVICE_LOAD_MAX_TIMEOUTS", 0, 1e9);
    const auto max_drops = required_limit("HAND_DEVICE_LOAD_MAX_QUEUE_DROPS", 0, 1e9);
    const auto max_queue = required_limit("HAND_DEVICE_LOAD_MAX_QUEUE_HIGH_WATER", 0, 1e9);
    const auto output = env("HAND_DEVICE_LOAD_CSV");
    REQUIRE(output.has_value());
    // Explicit UTF-8 paths support Chinese/space directories on Windows.
    std::ofstream csv(std::filesystem::u8path(*output), std::ios::out | std::ios::trunc);
    REQUIRE(csv.is_open());
    std::cout << "frozen load seconds=" << seconds << " hz=" << hz << " timeout_ms=" << timeout.count()
              << " min_completion_percent=" << min_completion << " max_p99_ms=" << max_p99
              << " max_cpu_percent=" << max_cpu << " max_timeouts=" << max_timeouts << " max_queue_drops=" << max_drops
              << " max_queue_high_water=" << max_queue << '\n';
    csv << "elapsed_s,latency_ms,success,tx_frames,rx_frames,session_overflows,queue_high_water\n";
    csv.flush();
    DeviceFixture fixture;
    auto &hand = fixture.device();
    hand.open();
    const auto baseline = hand.get_health();
    const auto generation = baseline.connection_generation;
    const auto start = std::chrono::steady_clock::now();
    const auto cpu_start = process_cpu_seconds();
    const auto duration = std::chrono::duration<double>{seconds};
    const auto interval =
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>{1.0 / hz});
    auto scheduled = start;
    const auto expected = static_cast<size_t>(std::ceil(seconds / std::chrono::duration<double>{interval}.count()));
    size_t attempted = 0, completed = 0, errors = 0;
    size_t queue_peak = baseline.session.queue_high_water;
    uint64_t timeouts = 0;
    std::vector<double> latencies;
    while (scheduled - start < duration) {
        std::this_thread::sleep_until(scheduled);
        const auto read_start = std::chrono::steady_clock::now();
        if (read_start - start >= duration) {
            break;
        }
        bool success = false;
        try {
            const auto sample = hand.get_angles(timeout);
            success =
                sample.meta.valid && !sample.meta.stale &&
                sample.meta.origin == TelemetryValueOrigin::kDeviceMeasured &&
                sample.meta.connection_generation == generation &&
                sample.raw.size() == hand.get_capabilities().joints.size() &&
                std::all_of(sample.raw.begin(), sample.raw.end(), [](double value) { return std::isfinite(value); });
        } catch (const HandError &error) {
            if (error.code() == ErrorCode::kTimeout) {
                ++timeouts;
            } else {
                ++errors;
            }
        }
        ++attempted;
        const auto now = std::chrono::steady_clock::now();
        const auto latency = std::chrono::duration<double, std::milli>{now - read_start}.count();
        if (success) {
            ++completed;
            latencies.push_back(latency);
        }
        const auto health = hand.get_health();
        queue_peak = std::max(queue_peak, health.session.queue_high_water);
        csv << std::chrono::duration<double>{now - start}.count() << ',' << latency << ',' << success << ','
            << health.session.tx_frames << ',' << health.session.rx_frames << ',' << health.session.overflows << ','
            << health.session.queue_high_water << '\n';
        csv.flush();
        REQUIRE(csv.good());
        scheduled += interval;
        // Do not burst to catch up after a slow driver call. Lost workload
        // slots reduce the completion ratio instead of disappearing.
        while (scheduled < now && scheduled - start < duration) {
            scheduled += interval;
        }
    }
    const auto elapsed = std::chrono::duration<double>{std::chrono::steady_clock::now() - start}.count();
    const auto cpu = 100.0 * (process_cpu_seconds() - cpu_start) / elapsed;
    const auto final = hand.get_health();
    const auto drops = final.session.overflows - baseline.session.overflows;
    hand.close();
    REQUIRE_FALSE(latencies.empty());
    std::sort(latencies.begin(), latencies.end());
    const auto percentile = [&](double fraction) {
        return latencies[static_cast<size_t>(std::ceil(fraction * latencies.size())) - 1];
    };
    const auto missed = expected - attempted;
    const auto completion = 100.0 * completed / expected;
    std::cout << "load elapsed_s=" << elapsed << " attempted=" << attempted << " missed=" << missed
              << " completion_percent=" << completion << " timeouts=" << timeouts << " errors=" << errors
              << " p50_ms=" << percentile(0.50) << " p95_ms=" << percentile(0.95) << " p99_ms=" << percentile(0.99)
              << " process_cpu_percent=" << cpu << " session_queue_high_water=" << queue_peak
              << " session_overflows=" << drops << '\n';
    REQUIRE(elapsed >= 600);
    REQUIRE(completion >= min_completion);
    REQUIRE(timeouts <= max_timeouts);
    REQUIRE(errors == 0);
    REQUIRE(percentile(0.99) <= max_p99);
    REQUIRE(cpu <= max_cpu);
    REQUIRE(drops <= max_drops);
    REQUIRE(queue_peak <= max_queue);
    REQUIRE(final.connection_generation == generation);
    // Session overflow is local loss, not a bus/adapter dropped-frame counter.
    // Attach independent CAN capture and Windows resource samples to the report.
}
