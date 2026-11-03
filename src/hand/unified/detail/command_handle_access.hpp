#pragma once

#include <chrono>
#include <cstdint>
#include <exception>

#include <linkerbot/hand/api/commands.hpp>

namespace linkerbot::hand::command_detail {
    class CommandHandleAccess {
    public:
        static void reserve_send_result(CommandHandle &handle);
        static void record_send_result(CommandHandle &handle, const transport::SendResult &result);
        static void record_sent(CommandHandle &handle, uint64_t sent_frames);
        static void mark_dispatched(CommandHandle &handle, uint64_t sent_frames,
                                    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());
        static void
        mark_acknowledged(CommandHandle &handle,
                          std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());
        static void mark_failed(CommandHandle &handle, std::exception_ptr error, uint64_t sent_frames,
                                std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());
        static void mark_superseded(CommandHandle &handle,
                                    std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());
        static void mark_cancelled(CommandHandle &handle,
                                   std::chrono::steady_clock::time_point timestamp = std::chrono::steady_clock::now());

    private:
        static void transition(CommandHandle &handle, CommandStage next, uint64_t sent_frames, std::exception_ptr error,
                               std::chrono::steady_clock::time_point timestamp);
    };
} // namespace linkerbot::hand::command_detail
