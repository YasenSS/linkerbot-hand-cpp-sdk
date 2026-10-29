#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <linkerbot/hand/api/config.hpp>
#include <linkerbot/hand/identity.hpp>
#include <linkerbot/hand/protocol/transaction.hpp>
#include <linkerbot/hand/runtime/device_runtime.hpp>

namespace linkerbot::hand::identity_probe {
    struct ModelRule {
        Model model = Model::kUnknown;
        TransportKind wire_protocol = TransportKind::kCan;
        std::vector<std::string_view> factory_prefixes;
    };

    struct Target {
        Model model = Model::kUnknown;
        HandSide expected_side = HandSide::kUnknown;
        uint32_t request_id = 0;
        uint8_t host_id = 0;
    };

    // Single registry used by HandDevice identity verification and discovery.
    // Adding a model must add one rule and one probe dispatch branch.
    [[nodiscard]] const std::vector<ModelRule> &model_rules();
    [[nodiscard]] const ModelRule &rule_for(Model model);
    [[nodiscard]] Model classic_model_from_product_code(std::string_view code);

    // The first-stage classic discovery request. It accepts both indexed and
    // byte-offset C0 fragment markers and never constructs a model Manager.
    [[nodiscard]] protocol::TransactionPlan classic_product_plan(uint32_t arbitration_id);

    // Executes identity-only protocol transactions on an already-routed
    // runtime. It starts no polling and performs no writes or device commands.
    [[nodiscard]] DeviceIdentity probe(const std::shared_ptr<runtime::DeviceRuntime> &runtime, const Target &target,
                                       std::chrono::milliseconds timeout);
} // namespace linkerbot::hand::identity_probe
