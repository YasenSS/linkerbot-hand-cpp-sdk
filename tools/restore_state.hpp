#pragma once

#include <exception>
#include <utility>

namespace l6_benchmark {
    // Pose restoration and reply-mode restoration are independent obligations.
    // Always attempt both and preserve the first failure for the caller.
    template <class RestorePose, class RestoreReply> void restore_state(RestorePose &&pose, RestoreReply &&reply) {
        std::exception_ptr first;
        try {
            std::forward<RestorePose>(pose)();
        } catch (...) {
            first = std::current_exception();
        }
        try {
            std::forward<RestoreReply>(reply)();
        } catch (...) {
            if (!first) {
                first = std::current_exception();
            }
        }
        if (first) {
            std::rethrow_exception(first);
        }
    }
}
