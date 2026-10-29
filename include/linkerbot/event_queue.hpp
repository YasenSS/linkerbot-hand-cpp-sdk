#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace linkerbot {

    template <typename T> class EventQueue {
    public:
        explicit EventQueue(size_t max_size) : max_size_(max_size) {
            if (max_size == 0) {
                throw std::invalid_argument("EventQueue max_size must be positive");
            }
        }
        ~EventQueue() = default;

        EventQueue(const EventQueue &) = delete;
        EventQueue &operator=(const EventQueue &) = delete;
        EventQueue(EventQueue &&) = delete;
        EventQueue &operator=(EventQueue &&) = delete;

        void push(const T &item) {
            const std::scoped_lock lock(mutex_);
            if (closed_) {
                return;
            }
            if (buffer_.size() >= max_size_) {
                buffer_.pop_front();
                ++drop_count_;
            }
            buffer_.push_back(item);
            cv_.notify_one();
        }

        [[nodiscard]] uint64_t drop_count() const {
            const std::scoped_lock lock(mutex_);
            return drop_count_;
        }

        void reset_drop_count() {
            const std::scoped_lock lock(mutex_);
            drop_count_ = 0;
        }

        std::optional<T> pop() {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return !buffer_.empty() || closed_; });
            if (buffer_.empty()) {
                return std::nullopt;
            }
            T item = std::move(buffer_.front());
            buffer_.pop_front();
            return item;
        }

        void close() {
            const std::scoped_lock lock(mutex_);
            closed_ = true;
            cv_.notify_all();
        }

        [[nodiscard]] bool closed() const {
            const std::scoped_lock lock(mutex_);
            return closed_;
        }

        struct Sentinel {};

        class Iterator {
        public:
            explicit Iterator(EventQueue &queue) : queue_(&queue) { advance(); }

            const T &operator*() const {
                return *current_; // NOLINT(bugprone-unchecked-optional-access)
            }
            const T *operator->() const { return &*current_; }

            Iterator &operator++() {
                advance();
                return *this;
            }

            bool operator!=(Sentinel /*unused*/) const { return current_.has_value(); }

        private:
            void advance() { current_ = queue_->pop(); }

            EventQueue *queue_;
            std::optional<T> current_;
        };

        Iterator begin() { return Iterator(*this); }
        Sentinel end() { return {}; }

    private:
        std::deque<T> buffer_;
        mutable std::mutex mutex_;
        std::condition_variable cv_;
        size_t max_size_;
        uint64_t drop_count_ = 0;
        bool closed_ = false;
    };

} // namespace linkerbot
