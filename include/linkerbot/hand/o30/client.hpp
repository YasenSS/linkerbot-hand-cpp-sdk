#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <linkerbot/hand/runtime/call_drain.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/o30/joints.hpp>
#include <linkerbot/hand/o30/protocol.hpp>

namespace linkerbot::hand::o30 {

    // Source-compatible O30 facade. DeviceRuntime owns routing, pending
    // requests, exclusion and waits; pure protocol plans assemble HOP ranges.
    class Client {
    public:
        explicit Client(can::CANDispatcher &dispatcher, uint32_t request_id = protocol::kDefaultRequestId);
        ~Client();

        Client(const Client &) = delete;
        Client &operator=(const Client &) = delete;
        Client(Client &&) = delete;
        Client &operator=(Client &&) = delete;

        [[nodiscard]] uint32_t request_id() const { return request_id_; }
        [[nodiscard]] uint32_t response_id() const { return response_id_; }

        // Retarget only the local client after an external device ID change.
        // ConfigManager::set_standard_id already retargets on a successful ACK.
        void set_request_id(uint32_t request_id);

        // Write the device's standard ID and retarget on success. The ACK may
        // use either the old or the new response ID; other requests stay serialized.
        void change_request_id(uint16_t request_id, std::chrono::milliseconds timeout);

        // Lazily reads and caches the device's HOP version. Unknown versions
        // fail closed for operations whose object meanings differ by revision.
        [[nodiscard]] protocol::Revision revision(std::chrono::milliseconds timeout);

        // Read: CTRL = MI or MI|0x80, empty payload, EDL is the requested length.
        [[nodiscard]] std::vector<uint8_t> read(uint8_t mi, uint8_t si, uint8_t edl, bool rts,
                                                std::chrono::milliseconds timeout);

        // Write: CTRL = MI (MI|0x80 for 0.0.4 current limits), EDL = payload.size().
        // Returns the assembled ACK
        // payload (often a status echo, not necessarily the written value).
        [[nodiscard]] std::vector<uint8_t> write(uint8_t mi, uint8_t si, const std::vector<uint8_t> &payload,
                                                 std::chrono::milliseconds timeout);

        // Writes only the mounted slot ranges of a 36-byte object.
        void write_mounted(uint8_t mi, const std::array<uint8_t, kSlotCount> &slots, std::chrono::milliseconds timeout);

        void close();

        Client(std::shared_ptr<runtime::DeviceRuntime> runtime, uint32_t request_id = protocol::kDefaultRequestId);
        [[nodiscard]] std::shared_ptr<runtime::DeviceRuntime> runtime() const { return runtime_; }
        [[nodiscard]] runtime::Deadline received_at() const { return runtime_->response_provenance().received_at; }
        runtime::DeviceRuntime::Operation operation(std::chrono::milliseconds timeout) {
            const auto deadline = runtime::deadline_after(timeout);
            auto call = calls_.hold();
            auto op = runtime_->operation(deadline);
            op.retain_call(std::move(call));
            return op;
        }

    private:
        std::vector<uint8_t> transact(uint8_t mi, uint8_t si, uint8_t edl, const std::vector<uint8_t> &payload,
                                      bool rts, std::chrono::milliseconds timeout,
                                      std::optional<uint32_t> next_request_id = {});
        std::shared_ptr<runtime::DeviceRuntime> runtime_;
        runtime::CallDrain calls_;
        friend struct ClientTestAccess;
        std::atomic<uint32_t> request_id_;
        std::atomic<uint32_t> response_id_;
        std::optional<protocol::Revision> revision_;
        uint64_t revision_generation_ = 0;
    };
} // namespace linkerbot::hand::o30
