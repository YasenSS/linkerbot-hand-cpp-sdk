#include <linkerbot/hand/api/commands.hpp>

#include "unified/detail/command_handle_access.hpp"

#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <utility>

#include <linkerbot/hand/errors.hpp>

namespace linkerbot::hand {
    struct CommandHandle::State {
        State(uint64_t selected_id, CommandFamily selected_family, AckSupport selected_ack_support,
              AckExpectation selected_ack_expectation, std::chrono::steady_clock::time_point accepted_at)
            : id(selected_id), family(selected_family), ack_support(selected_ack_support),
              ack_expectation(selected_ack_expectation), history{{CommandStage::kAccepted, accepted_at, 0}} {
            // At most Accepted -> Dispatched -> Acknowledged/Failed. Reserve
            // before admission so receipt callbacks never allocate history.
            history.reserve(3);
        }

        uint64_t id;
        CommandFamily family;
        AckSupport ack_support;
        AckExpectation ack_expectation;
        mutable std::mutex mutex;
        mutable std::condition_variable changed;
        CommandStage stage = CommandStage::kAccepted;
        uint64_t sent_frames = 0;
        std::exception_ptr error;
        std::vector<CommandTransition> history;
        bool done = false;
    };

    CommandHandle::CommandHandle(uint64_t command_id, CommandFamily family, AckSupport ack_support,
                                 std::chrono::steady_clock::time_point accepted_at)
        : CommandHandle(command_id, family, ack_support,
                        ack_support == AckSupport::kUnsupported ? AckExpectation::kNone : AckExpectation::kRequired,
                        accepted_at) {}

    CommandHandle::CommandHandle(uint64_t command_id, CommandFamily family, AckSupport ack_support,
                                 AckExpectation ack_expectation, std::chrono::steady_clock::time_point accepted_at) {
        if (command_id == 0) {
            throw ValidationError("command_id must be positive");
        }
        if (ack_support == AckSupport::kUnsupported && ack_expectation == AckExpectation::kRequired) {
            throw ValidationError("ACK cannot be required when the protocol does not support it");
        }
        if (ack_support == AckSupport::kRequired && ack_expectation == AckExpectation::kNone) {
            throw ValidationError("ACK must be required when the protocol requires it");
        }
        state_ = std::make_shared<State>(command_id, family, ack_support, ack_expectation, accepted_at);
    }

    CommandHandle::CommandHandle(std::shared_ptr<State> state) : state_(std::move(state)) {
        if (!state_) {
            throw ValidationError("command state must not be null");
        }
    }

    uint64_t CommandHandle::command_id() const noexcept { return state_->id; }

    CommandFamily CommandHandle::family() const noexcept { return state_->family; }

    CommandStage CommandHandle::stage() const noexcept {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->stage;
    }

    AckSupport CommandHandle::ack_support() const noexcept { return state_->ack_support; }

    AckExpectation CommandHandle::ack_expectation() const noexcept { return state_->ack_expectation; }

    uint64_t CommandHandle::sent_frames() const noexcept {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->sent_frames;
    }

    std::exception_ptr CommandHandle::error() const noexcept {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->error;
    }

    std::vector<CommandTransition> CommandHandle::history() const {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->history;
    }

    bool CommandHandle::done() const noexcept {
        const std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->done;
    }

    bool CommandHandle::wait_for(std::chrono::milliseconds timeout) const {
        if (timeout.count() <= 0) {
            throw ValidationError("command wait timeout must be positive");
        }
        std::unique_lock<std::mutex> lock(state_->mutex);
        return state_->changed.wait_for(lock, timeout, [this] { return state_->done; });
    }

    CommandStage CommandHandle::wait() const {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->changed.wait(lock, [this] { return state_->done; });
        return state_->stage;
    }

    namespace {
        bool terminal(CommandStage stage, AckExpectation ack_expectation) noexcept {
            return stage == CommandStage::kAcknowledged || stage == CommandStage::kFailed ||
                   stage == CommandStage::kSuperseded || stage == CommandStage::kCancelled ||
                   (stage == CommandStage::kDispatched && ack_expectation == AckExpectation::kNone);
        }

    } // namespace

    void command_detail::CommandHandleAccess::transition(CommandHandle &handle, CommandStage next, uint64_t sent_frames,
                                                         std::exception_ptr error,
                                                         std::chrono::steady_clock::time_point timestamp) {
        const auto &state = handle.state_;
        std::unique_lock<std::mutex> lock(state->mutex);
        if (state->done) {
            throw LifecycleError("command already reached a terminal stage");
        }

        const bool allowed = (state->stage == CommandStage::kAccepted &&
                              (next == CommandStage::kDispatched || next == CommandStage::kFailed ||
                               next == CommandStage::kSuperseded || next == CommandStage::kCancelled)) ||
                             (state->stage == CommandStage::kDispatched &&
                              (next == CommandStage::kAcknowledged || next == CommandStage::kFailed ||
                               next == CommandStage::kCancelled));
        if (!allowed) {
            throw LifecycleError("illegal command stage transition");
        }
        if (timestamp < state->history.back().timestamp) {
            throw LifecycleError("command transition timestamp moved backwards");
        }
        if (next == CommandStage::kDispatched && sent_frames == 0) {
            throw ValidationError("a dispatched command must send at least one frame");
        }
        if (state->stage == CommandStage::kDispatched && sent_frames != state->sent_frames) {
            throw LifecycleError("post-dispatch transition changed sent frame count");
        }
        if (next == CommandStage::kAcknowledged && state->ack_expectation != AckExpectation::kRequired) {
            throw LifecycleError("a command without an ACK expectation cannot be acknowledged");
        }
        if (next == CommandStage::kFailed && !error) {
            throw ValidationError("failed command requires an error");
        }

        state->stage = next;
        state->sent_frames = sent_frames;
        state->error = std::move(error);
        state->history.push_back({next, timestamp, sent_frames});
        state->done = terminal(next, state->ack_expectation);
        const bool done = state->done;
        lock.unlock();
        if (done) {
            state->changed.notify_all();
        }
    }

    void command_detail::CommandHandleAccess::record_sent(CommandHandle &handle, uint64_t sent_frames) {
        const std::lock_guard<std::mutex> lock(handle.state_->mutex);
        if (handle.state_->done) {
            return;
        }
        if (sent_frames < handle.state_->sent_frames) {
            throw LifecycleError("command send progress moved backwards");
        }
        handle.state_->sent_frames = sent_frames;
    }

    void command_detail::CommandHandleAccess::mark_dispatched(CommandHandle &handle, uint64_t sent_frames,
                                                              std::chrono::steady_clock::time_point timestamp) {
        transition(handle, CommandStage::kDispatched, sent_frames, {}, timestamp);
    }

    void command_detail::CommandHandleAccess::mark_acknowledged(CommandHandle &handle,
                                                                std::chrono::steady_clock::time_point timestamp) {
        transition(handle, CommandStage::kAcknowledged, handle.sent_frames(), {}, timestamp);
    }

    void command_detail::CommandHandleAccess::mark_failed(CommandHandle &handle, std::exception_ptr error,
                                                          uint64_t sent_frames,
                                                          std::chrono::steady_clock::time_point timestamp) {
        transition(handle, CommandStage::kFailed, sent_frames, std::move(error), timestamp);
    }

    void command_detail::CommandHandleAccess::mark_superseded(CommandHandle &handle,
                                                              std::chrono::steady_clock::time_point timestamp) {
        transition(handle, CommandStage::kSuperseded, 0, {}, timestamp);
    }

    void command_detail::CommandHandleAccess::mark_cancelled(CommandHandle &handle,
                                                             std::chrono::steady_clock::time_point timestamp) {
        transition(handle, CommandStage::kCancelled, handle.sent_frames(), {}, timestamp);
    }
} // namespace linkerbot::hand
