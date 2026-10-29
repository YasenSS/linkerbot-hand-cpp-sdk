#pragma once
#include <linkerbot/hand/l20lite/force_sensor.hpp>
#include <linkerbot/hand/runtime/channel.hpp>
namespace linkerbot::hand::runtime {
    template <> inline l20lite::AllFingersData Channel<l20lite::AllFingersData>::project(const Sample &sample) {
        auto data = std::any_cast<l20lite::AllFingersData>(sample.value);
        for (auto *finger : {&data.thumb, &data.index, &data.middle, &data.ring, &data.pinky}) {
            finger->timestamp = sample.received_at;
        }
        return data;
    }
}
namespace linkerbot::hand::l20lite::detail {
    template <class Data> using Channel = runtime::Channel<Data>;
}
