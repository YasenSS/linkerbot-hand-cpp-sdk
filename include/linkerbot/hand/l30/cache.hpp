#pragma once
#include <linkerbot/hand/l30/client.hpp>
#include <linkerbot/hand/runtime/event_binding.hpp>
namespace linkerbot::hand::l30 {
    // Typed facade over Runtime publication: no local cache, waiter or executor.
    template <class T> class Cache {
    public:
        Cache(Client &client, std::string domain)
            : client_(&client), core_(client.runtime()), domain_(std::move(domain)), events_(core_, domain_) {}
        void push(T data) { client_->publish(domain_, std::move(data)); }
        std::optional<T> snapshot() const { return core_->snapshot_as<T>(domain_); }
        void set_sink(std::function<void(const T &)> sink) {
            events_.replace(
                sink ? runtime::DeviceRuntime::EventHandler([sink = std::move(sink)](const runtime::Sample &s) {
                    sink(std::any_cast<const T &>(s.value));
                })
                     : runtime::DeviceRuntime::EventHandler{});
        }
        void close() { events_.close(); }

    private:
        Client *client_;
        std::shared_ptr<runtime::DeviceRuntime> core_;
        std::string domain_;
        runtime::EventBinding events_;
    };
}
