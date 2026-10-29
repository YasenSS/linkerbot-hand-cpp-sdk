#pragma once
#include <linkerbot/hand/o6/force_sensor.hpp>
#include <linkerbot/hand/runtime/channel.hpp>
namespace linkerbot::hand::runtime {
    template <> inline o6::AllFingersData Channel<o6::AllFingersData>::project(const Sample &sample) {
        auto data = std::any_cast<o6::AllFingersData>(sample.value);
        for (auto *finger : {&data.thumb, &data.index, &data.middle, &data.ring, &data.pinky}) {
            finger->timestamp = sample.received_at;
        }
        return data;
    }
}
namespace linkerbot::hand::o6::detail {
    template <class Data> using Channel = runtime::Channel<Data>;
}
