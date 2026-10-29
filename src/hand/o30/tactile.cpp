#include <linkerbot/hand/o30/protocol.hpp>
#include <linkerbot/hand/protocol/tactile.hpp>
namespace linkerbot::hand::o30 {
    std::vector<SensorField> decode_tactile_fields(const SensorReading &reading) {
        try {
            return hand::protocol::hop::decode_tactile_fields(reading);
        } catch (const hand::ProtocolError &error) {
            throw protocol::ProtocolError(error.what());
        }
    }
}
