#pragma once
#include <linkerbot/hand/l20/force_sensor.hpp>
#include <linkerbot/hand/runtime/channel.hpp>
namespace linkerbot::hand::runtime {
    template <> inline l20::AllFingersData Channel<l20::AllFingersData>::project(const Sample &sample) {
        auto data = std::any_cast<l20::AllFingersData>(sample.value);
        for (auto *finger : {&data.thumb, &data.index, &data.middle, &data.ring, &data.pinky}) {
            finger->timestamp = sample.received_at;
        }
        return data;
    }
}
namespace linkerbot::hand::l20::detail {
    template <class Data> using Channel = runtime::Channel<Data>;
}
