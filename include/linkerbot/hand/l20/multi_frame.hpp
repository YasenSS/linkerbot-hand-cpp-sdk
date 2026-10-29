#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/errors.hpp>
#include <linkerbot/hand/l20/frame_layout.hpp>
#include <linkerbot/hand/l20/request_state.hpp>

namespace linkerbot::hand::l20 {

    class FiveFrameAssembler {
    public:
        explicit FiveFrameAssembler(FrameParts parts) : parts_(parts) {}

        [[nodiscard]] const FrameParts &parts() const { return parts_; }

        // force retains its public signature, but cannot replace in-flight
        // replies. Ambiguous/expired batches require explicit reconstruction.
        bool begin_sense(bool force = false) {
            (void)force;
            return begin_until(detail::Clock::now() + std::chrono::milliseconds{200});
        }

        bool begin_until(detail::Deadline deadline) {
            const std::scoped_lock lock(mutex_);
            expire();
            if (in_flight_ || poisoned_) {
                return false;
            }
            in_flight_ = true;
            deadline_ = deadline;
            have_.fill(false);
            return true;
        }

        void quarantine() {
            const std::scoped_lock lock(mutex_);
            poisoned_ = true;
            in_flight_ = false;
            have_.fill(false);
        }

        bool quarantined() {
            const std::scoped_lock lock(mutex_);
            expire();
            return poisoned_;
        }

        std::optional<std::array<uint8_t, kJointCount>> put(uint8_t cmd, const uint8_t *payload, size_t len) {
            const std::scoped_lock lock(mutex_);
            expire();
            if (!in_flight_ || poisoned_) {
                return std::nullopt;
            }
            const int idx = index_of(cmd);
            if (idx < 0) {
                return std::nullopt;
            }
            const auto &part = parts_[static_cast<size_t>(idx)];
            if (!payload || len != part.count) {
                return std::nullopt;
            }

            auto &slot = slots_[static_cast<size_t>(idx)];
            if (have_[static_cast<size_t>(idx)]) {
                if (!std::equal(slot.begin(), slot.begin() + part.count, payload)) {
                    poisoned_ = true;
                    in_flight_ = false;
                    have_.fill(false);
                }
                return std::nullopt;
            }
            for (size_t i = 0; i < part.count; ++i) {
                slot[i] = payload[i];
            }
            have_[static_cast<size_t>(idx)] = true;

            for (auto h : have_) {
                if (!h) {
                    return std::nullopt;
                }
            }

            std::array<uint8_t, kJointCount> raw{};
            for (size_t p = 0; p < kFramesPerDomain; ++p) {
                fill(raw, parts_[p], slots_[p]);
            }
            for (auto &h : have_) {
                h = false;
            }
            in_flight_ = false;
            return raw;
        }

    private:
        void expire() {
            if (in_flight_ && detail::Clock::now() >= deadline_) {
                poisoned_ = true;
                in_flight_ = false;
                have_.fill(false);
            }
        }

        int index_of(uint8_t cmd) const {
            for (size_t i = 0; i < kFramesPerDomain; ++i) {
                if (parts_[i].cmd == cmd) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        static void fill(std::array<uint8_t, kJointCount> &raw, const FramePart &part,
                         const std::array<uint8_t, kPayloadBytes> &payload) {
            for (size_t i = 0; i < part.count; ++i) {
                const auto j = part.joints[i];
                if (j == kNoJoint) {
                    continue;
                }
                raw[j] = payload[i];
            }
        }

        FrameParts parts_;
        std::mutex mutex_;
        std::array<std::array<uint8_t, kPayloadBytes>, kFramesPerDomain> slots_{};
        std::array<bool, kFramesPerDomain> have_{};
        bool in_flight_ = false;
        bool poisoned_ = false;
        detail::Deadline deadline_{};
    };

    // Public signatures and pacing are preserved. The dispatcher holds one
    // physical send boundary across all five frames, including their gaps.
    inline void send_joint_frames(can::CANDispatcher &dispatcher, uint32_t id, const FrameParts &parts,
                                  const std::array<uint8_t, kJointCount> &raw, std::chrono::microseconds gap = {}) {
        detail::send(dispatcher, joint_messages(id, parts, &raw), detail::Deadline::max(), gap);
    }

    inline void send_sense_frames(can::CANDispatcher &dispatcher, uint32_t id, const FrameParts &parts,
                                  std::chrono::microseconds gap = {}) {
        detail::send(dispatcher, joint_messages(id, parts, nullptr), detail::Deadline::max(), gap);
    }

    namespace detail {
        template <typename T>
        T request_domain(Window &window, FiveFrameAssembler &assembler, internal::DataRelay<T> &relay,
                         transport::Session &session, uint32_t id, std::chrono::milliseconds timeout,
                         std::chrono::microseconds gap) {
            const auto deadline = deadline_after(timeout);
            auto guard = window.admission.enter(deadline);
            window.await_idle(deadline);
            bool begun = false;
            try {
                return relay.request_until(
                    [&] {
                        {
                            const std::lock_guard lock(window.mutex);
                            window.begin_locked(deadline);
                            window.solicited = true;
                            begun = true;
                            if (!assembler.begin_until(deadline)) {
                                throw DeviceNotReadyError("L20 assembler quarantined");
                            }
                        }
                        send(session, joint_messages(id, assembler.parts(), nullptr), deadline, gap,
                             [&] { return window.admission.stopped(); });
                    },
                    deadline);
            } catch (...) {
                if (begun) {
                    const std::lock_guard lock(window.mutex);
                    window.poison_locked();
                    assembler.quarantine();
                }
                throw;
            }
        }

        inline void write_domain(Window &window, FiveFrameAssembler &assembler, transport::Session &session,
                                 const std::vector<can::Message> &frames, std::chrono::microseconds gap = {}) {
            const auto deadline = Deadline::max();
            auto guard = window.admission.enter(deadline);
            {
                const std::lock_guard lock(window.mutex);
                if (window.admission.stopped()) {
                    throw ClosedError("L20 manager stopped");
                }
                // Setters remain send-only and may issue consecutive targets
                // even on firmware without echoes. Overlapping echo windows
                // are ambiguous for reads, so quarantine readback, not writes.
                if (window.active || window.poisoned) {
                    window.poison_locked();
                    assembler.quarantine();
                } else {
                    const auto reply_deadline = Clock::now() + std::chrono::milliseconds{200};
                    window.begin_locked(reply_deadline);
                    window.solicited = false;
                    if (!assembler.begin_until(reply_deadline)) {
                        window.poison_locked();
                    }
                }
            }
            try {
                // Writes remain synchronous send-only. Echo collection keeps
                // their replies from being consumed by a subsequent read.
                send(session, frames, deadline, gap, [&] { return window.admission.stopped(); });
            } catch (...) {
                const std::lock_guard lock(window.mutex);
                window.poison_locked();
                assembler.quarantine();
                throw;
            }
        }

        inline void poll_domain(Window &window, FiveFrameAssembler &assembler, transport::Session &session, uint32_t id,
                                std::chrono::microseconds gap) {
            const auto deadline = Clock::now() + std::chrono::milliseconds{200};
            auto guard = window.admission.enter(deadline, [&] { return window.polling_cancelled.load(); });
            {
                const std::lock_guard lock(window.mutex);
                window.check_locked();
                if (window.active) {
                    return;
                }
                window.begin_locked(deadline);
                window.solicited = false;
                if (!assembler.begin_until(deadline)) {
                    throw DeviceNotReadyError("L20 assembler quarantined");
                }
            }
            try {
                send(session, joint_messages(id, assembler.parts(), nullptr), deadline, gap,
                     [&] { return window.admission.stopped() || window.polling_cancelled.load(); });
            } catch (...) {
                const std::lock_guard lock(window.mutex);
                window.poison_locked();
                assembler.quarantine();
                throw;
            }
        }
    }
} // namespace linkerbot::hand::l20
