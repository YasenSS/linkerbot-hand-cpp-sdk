#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <linkerbot/can/dispatcher.hpp>
#include <linkerbot/can/message.hpp>
#include <linkerbot/hand/l30/protocol.hpp>
#include <linkerbot/hand/runtime/call_drain.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>
#include <linkerbot/transport/session.hpp>

namespace linkerbot::hand::l30 {

    // Compatibility facade: all request execution and report delivery use DeviceRuntime.
    class Client {
    public:
        using ReportHandler = std::function<void(const std::vector<uint8_t> &)>;
        using HandlerId = uint64_t;
        class QuarantinedError : public DeviceNotReadyError {
        public:
            using DeviceNotReadyError::DeviceNotReadyError;
        };

        // node_id addresses the device (1..31); host_id is this host's own ID
        // (0..31) and becomes the SrcID of every request.
        Client(can::CANDispatcher &dispatcher, uint8_t node_id, uint8_t host_id);
        Client(std::shared_ptr<transport::Session> session, uint8_t node_id, uint8_t host_id);
        Client(std::shared_ptr<runtime::DeviceRuntime> core, uint8_t node_id, uint8_t host_id);
        ~Client();
        [[nodiscard]] std::shared_ptr<runtime::DeviceRuntime> runtime() const { return core_; }
        void publish(const std::string &domain, std::any value);
        // Lifetime accounting includes manager decoding/publication and nested calls.
        [[nodiscard]] std::shared_ptr<void> enter();

        Client(const Client &) = delete;
        Client &operator=(const Client &) = delete;
        Client(Client &&) = delete;
        Client &operator=(Client &&) = delete;

        [[nodiscard]] uint8_t node_id() const { return node_id_; }
        [[nodiscard]] uint8_t host_id() const { return host_id_; }

        // Fire-and-forget write. The batch position/current-limit/speed commands are
        // specified as unacknowledged, so there is nothing to wait for.
        void send_no_ack(uint8_t parent, uint8_t subcmd, const std::vector<uint8_t> &payload, uint8_t wire_bytes);

        // Write command that expects an ACK. Returns the raw ACK payload; the
        // caller decides how to validate it.
        [[nodiscard]] std::vector<uint8_t> request_ack(uint8_t parent, uint8_t subcmd,
                                                       const std::vector<uint8_t> &payload, uint8_t wire_bytes,
                                                       std::chrono::milliseconds timeout);

        // Read request (empty payload, Access=0). Returns the raw response.
        [[nodiscard]] std::vector<uint8_t> read(uint8_t parent, uint8_t subcmd, std::chrono::milliseconds timeout);

        // Reads one finger's 12x6 matrix, reassembled from the two response
        // frames. Tactile reads serialize against each other but not against
        // ordinary requests.
        [[nodiscard]] std::array<uint8_t, protocol::kTactileDataLength> read_tactile(uint8_t subcmd,
                                                                                     std::chrono::milliseconds timeout);

        // Registers a callback for one periodic-report subcommand. The returned
        // id removes it again; handlers run on the Runtime event worker.
        [[nodiscard]] HandlerId add_report_handler(uint8_t subcmd, ReportHandler handler);

        void remove_report_handler(uint8_t subcmd, HandlerId id);

        // A device semantic operation holds admission across all nested calls.
        // The absolute deadline also bounds every nested request. tactile_only
        // retains the existing independence of ordinary and tactile reads.
        class Operation {
        public:
            ~Operation();
            Operation(const Operation &) = delete;
            Operation &operator=(const Operation &) = delete;
            [[nodiscard]] std::chrono::milliseconds remaining() const;
            [[nodiscard]] std::chrono::steady_clock::time_point deadline() const { return deadline_; }

        private:
            friend class Client;
            Operation(Client &client, int kind, std::chrono::steady_clock::time_point deadline);
            runtime::DeviceRuntime::Operation operation_;
            std::chrono::steady_clock::time_point deadline_;
        };
        [[nodiscard]] Operation operation(std::chrono::milliseconds timeout, bool tactile_only = false);
        // Reject application work and cancel it before teardown ACKs are sent.
        void begin_shutdown();
        // Polling cancellation is scoped to the listed worker threads.
        void cancel_thread(std::thread::id thread);
        void resume_thread(std::thread::id thread);

        // Unsubscribes and fails every waiter. External callers also drain
        // active and queued operations and claimed sends. RX/event close only cancels;
        // a later external close supplies the barrier. Idempotent.
        void close();

    private:
        friend class L30;
        void drain_calls() { calls_.drain(); }
        [[nodiscard]] std::vector<uint8_t> request(uint8_t parent, uint8_t subcmd, uint8_t access,
                                                   const std::vector<uint8_t> &payload, uint8_t wire_bytes,
                                                   std::chrono::milliseconds timeout);
        runtime::CancellationToken token();
        void remember(runtime::Sample sample);
        void throw_if_closed() const; // mutex_ held
        std::shared_ptr<runtime::DeviceRuntime> core_;
        uint8_t node_id_;
        uint8_t host_id_;
        runtime::CallDrain calls_;
        mutable std::mutex mutex_;
        bool closed_ = false;
        bool stopping_ = false;
        std::thread::id shutdown_thread_;
        // Compatibility cancellation scopes only; no response slots or waiters.
        struct Scope {
            runtime::CancellationSource cancel;
            size_t depth = 0;
            std::optional<runtime::Sample> sample;
        };
        std::map<std::thread::id, Scope> scopes_;
        std::set<std::thread::id> cancelled_threads_;
        std::map<HandlerId, uint8_t> handlers_;
        HandlerId observer_ = 0;
        std::string report_domain_;
    };

} // namespace linkerbot::hand::l30
