#include "linkerbot/hand/o30/pid_manager.hpp"
#include <linkerbot/hand/errors.hpp>

#include "u8_io.hpp"

#include <stdexcept>

namespace linkerbot::hand::o30 {

    namespace {
        uint8_t mi_of(PidTerm term) {
            switch (term) {
            case PidTerm::kPositionP:
            case PidTerm::kPositionI:
            case PidTerm::kPositionD:
            case PidTerm::kSpeedP:
            case PidTerm::kSpeedI:
            case PidTerm::kSpeedD:
                return static_cast<uint8_t>(term);
            }
            throw ValidationError("unknown O30 PID term");
        }
    } // namespace

    PidManager::PidManager(Client &client) : client_(&client) {}

    PidData PidManager::get(PidTerm term, std::chrono::milliseconds timeout_ms, ValueKind kind) {
        auto operation = client_->operation(timeout_ms);
        PidData data{};
        data.term = term;
        data.values = detail::read_joints(*client_, mi_of(term), kind, timeout_ms);
        data.kind = kind;
        data.timestamp = client_->received_at();
        return data;
    }

    void PidManager::set(PidTerm term, const std::array<uint8_t, kJointCount> &values,
                         std::chrono::milliseconds timeout_ms) {
        auto operation = client_->operation(timeout_ms);
        detail::write_joints(*client_, mi_of(term), values, timeout_ms);
    }

} // namespace linkerbot::hand::o30
