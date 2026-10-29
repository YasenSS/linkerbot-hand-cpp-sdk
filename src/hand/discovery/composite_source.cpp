#include <linkerbot/hand/discovery.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <tuple>
#include <typeinfo>
#include <unordered_set>
#include <utility>
#include <vector>

namespace linkerbot::hand {
    namespace {
        using Clock = std::chrono::steady_clock;

        [[nodiscard]] std::string channel_key(const DiscoveryChannel &channel) {
            return channel.backend() + '\x1f' + channel.native_name();
        }

        [[nodiscard]] bool same_descriptor(const DiscoveryChannel &left, const DiscoveryChannel &right) noexcept {
            return left.backend() == right.backend() && left.name() == right.name() &&
                   left.native_name() == right.native_name() && left.kind() == right.kind() &&
                   left.nominal_bitrate() == right.nominal_bitrate() && left.data_bitrate() == right.data_bitrate() &&
                   left.supports_classic() == right.supports_classic() && left.supports_fd() == right.supports_fd() &&
                   left.configuration_id() == right.configuration_id();
        }

        [[nodiscard]] auto channel_sort_key(const DiscoveryChannel &channel) {
            return std::make_tuple(channel.backend(), channel.native_name(), channel.name(), channel.kind(),
                                   channel.nominal_bitrate(), channel.data_bitrate(), channel.supports_classic(),
                                   channel.supports_fd(), channel.configuration_id());
        }

        [[nodiscard]] auto candidate_sort_key(const DiscoveryCandidate &candidate) {
            const auto &identity = candidate.identity();
            const auto &address = candidate.address();
            return std::make_tuple(channel_sort_key(candidate.channel()), candidate.wire_protocol(), address.scheme(),
                                   address.request_id(), address.response_id(), address.extended_id(),
                                   address.node_id(), address.host_id(), candidate.status(), identity.model,
                                   identity.side, identity.address, identity.product_code, identity.serial_number,
                                   identity.device_uid, identity.firmware_version, identity.protocol_name,
                                   identity.protocol_version, identity.hardware_version, identity.mechanical_version);
        }

        [[nodiscard]] auto failure_sort_key(const DiscoveryFailure &failure) {
            return std::make_tuple(failure.backend(), failure.native_channel(), failure.channel(), failure.stage(),
                                   failure.operation(), failure.code(), failure.message(), failure.error_type(),
                                   failure.native_status());
        }

        void sort_report(std::vector<DiscoveryChannel> &channels, std::vector<DiscoveryCandidate> &candidates,
                         std::vector<DiscoveryFailure> &failures) {
            std::stable_sort(channels.begin(), channels.end(), [](const auto &left, const auto &right) {
                return channel_sort_key(left) < channel_sort_key(right);
            });
            std::stable_sort(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
                return candidate_sort_key(left) < candidate_sort_key(right);
            });
            std::stable_sort(failures.begin(), failures.end(), [](const auto &left, const auto &right) {
                return failure_sort_key(left) < failure_sort_key(right);
            });
        }

        [[nodiscard]] std::chrono::milliseconds remaining_budget(Clock::time_point deadline) {
            const auto now = Clock::now();
            if (now >= deadline) {
                return std::chrono::milliseconds{0};
            }
            return std::max(std::chrono::milliseconds{1},
                            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now));
        }

        void append_timeout_failure(std::vector<DiscoveryFailure> &failures, const std::string &backend) {
            failures.emplace_back("scan_backend", "timeout",
                                  "discovery total time budget exhausted before starting backend", std::nullopt,
                                  backend, "select_backend", std::nullopt, "TimeoutError");
        }

        [[nodiscard]] bool matches_channel(const DiscoveryChannel &channel, const std::vector<std::string> &requested) {
            return requested.empty() ||
                   std::any_of(requested.begin(), requested.end(), [&channel](const std::string &name) {
                       return name == channel.name() || name == channel.native_name();
                   });
        }

        [[nodiscard]] bool contains(const std::vector<std::string> &values, const std::string &value) {
            return std::find(values.begin(), values.end(), value) != values.end();
        }

        void append_failure(std::vector<DiscoveryFailure> &failures, const DiscoveryFailure &failure,
                            const std::string &source_backend) {
            failures.emplace_back(failure.operation(), failure.code(), failure.message(), failure.channel(),
                                  failure.backend() ? failure.backend() : std::optional<std::string>{source_backend},
                                  failure.stage() ? failure.stage() : std::optional<std::string>{failure.operation()},
                                  failure.native_channel() ? failure.native_channel() : failure.channel(),
                                  failure.error_type(), failure.native_status());
        }

        void erase_channel(const std::string &key, std::vector<DiscoveryChannel> &channels,
                           std::vector<DiscoveryCandidate> &candidates) {
            channels.erase(
                std::remove_if(channels.begin(), channels.end(),
                               [&key](const DiscoveryChannel &channel) { return channel_key(channel) == key; }),
                channels.end());
            candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                            [&key](const DiscoveryCandidate &candidate) {
                                                return channel_key(candidate.channel()) == key;
                                            }),
                             candidates.end());
        }
    } // namespace

    CompositeDiscoverySource::CompositeDiscoverySource(std::vector<DiscoveryBackendSource> sources)
        : sources_(std::move(sources)) {
        for (const auto &entry : sources_) {
            if (entry.backend.empty()) {
                throw ValidationError("composite discovery backend must not be empty");
            }
            if (!entry.source) {
                throw ValidationError("composite discovery source must not be null");
            }
        }
    }

    DiscoveryReport CompositeDiscoverySource::scan(const DiscoveryConfig &config) {
        const auto started = Clock::now();
        const auto total_deadline = started + config.total_timeout;
        std::vector<DiscoveryChannel> channels;
        std::vector<DiscoveryCandidate> candidates;
        std::vector<DiscoveryFailure> failures;
        std::unordered_set<std::string> matched_channels;
        std::unordered_set<std::string> conflicted_channels;

        for (const auto &requested : config.backends) {
            const auto available =
                std::any_of(sources_.begin(), sources_.end(),
                            [&requested](const DiscoveryBackendSource &entry) { return entry.backend == requested; });
            if (!available) {
                failures.emplace_back("enumerate", "backend_not_found", "requested discovery backend is not available",
                                      std::nullopt, requested, "select_backend", std::nullopt, "DiscoveryError");
            }
        }

        for (const auto &entry : sources_) {
            if (!config.backends.empty() && !contains(config.backends, entry.backend)) {
                continue;
            }

            const auto remaining = remaining_budget(total_deadline);
            if (remaining.count() <= 0) {
                append_timeout_failure(failures, entry.backend);
                break;
            }

            DiscoveryConfig child_config = config;
            child_config.backends.clear();
            child_config.total_timeout = remaining;
            std::optional<DiscoveryReport> child_report;
            try {
                child_report = entry.source->scan(child_config);
            } catch (const std::exception &error) {
                failures.emplace_back("scan_backend", "backend_failed", error.what(), std::nullopt, entry.backend,
                                      "scan", std::nullopt, typeid(error).name());
                continue;
            } catch (...) {
                failures.emplace_back("scan_backend", "backend_failed",
                                      "discovery backend failed with a non-standard exception", std::nullopt,
                                      entry.backend, "scan", std::nullopt, "unknown");
                continue;
            }

            for (const auto &failure : child_report->failures()) {
                // A channel can legitimately be absent from one backend and
                // present on another. Emit one global diagnostic below.
                if (!config.channels.empty() && failure.code() == "channel_not_found") {
                    continue;
                }
                append_failure(failures, failure, entry.backend);
            }

            for (const auto &channel : child_report->channels()) {
                if (!matches_channel(channel, config.channels)) {
                    continue;
                }
                for (const auto &requested : config.channels) {
                    if (requested == channel.name() || requested == channel.native_name()) {
                        matched_channels.insert(requested);
                    }
                }

                const auto key = channel_key(channel);
                if (conflicted_channels.count(key) != 0) {
                    continue;
                }
                const auto existing =
                    std::find_if(channels.begin(), channels.end(),
                                 [&key](const DiscoveryChannel &current) { return channel_key(current) == key; });
                if (existing != channels.end()) {
                    if (same_descriptor(*existing, channel)) {
                        continue;
                    }
                    erase_channel(key, channels, candidates);
                    conflicted_channels.insert(key);
                    failures.emplace_back("aggregate", "channel_descriptor_conflict",
                                          "discovery sources reported conflicting descriptions "
                                          "for the same backend-native channel",
                                          channel.name(), channel.backend(), "deduplicate", channel.native_name(),
                                          "DiscoveryError");
                    continue;
                }

                channels.push_back(channel);
                for (const auto &candidate : child_report->candidates()) {
                    if (channel_key(candidate.channel()) == key) {
                        candidates.push_back(candidate);
                    }
                }
            }
        }

        for (const auto &requested : config.channels) {
            if (matched_channels.count(requested) == 0) {
                failures.emplace_back("enumerate", "channel_not_found", "requested discovery channel was not found",
                                      requested, std::nullopt, "select_channel", requested, "DiscoveryError");
            }
        }

        sort_report(channels, candidates, failures);

        return DiscoveryReport{std::move(channels), std::move(candidates), std::move(failures), Clock::now() - started};
    }

    PlatformDiscoverySource::PlatformDiscoverySource() {
#if defined(__linux__)
        std::vector<DiscoveryBackendSource> sources;
        sources.push_back({"socketcan", std::make_shared<SocketCANDiscoverySource>()});
        sources.push_back({"minicanfd", std::make_shared<MiniCANFDDiscoverySource>()});
        composite_ = std::make_unique<CompositeDiscoverySource>(std::move(sources));
#elif defined(_WIN32)
        std::vector<DiscoveryBackendSource> sources;
        sources.push_back({"pcan", std::make_shared<PCANDiscoverySource>()});
        sources.push_back({"slcan", std::make_shared<SLCANDiscoverySource>()});
        sources.push_back({"minicanfd", std::make_shared<MiniCANFDDiscoverySource>()});
        composite_ = std::make_unique<CompositeDiscoverySource>(std::move(sources));
#else
        unavailable_reason_ = "no default discovery backend is implemented for this platform";
#endif
    }

    DiscoveryReport PlatformDiscoverySource::scan(const DiscoveryConfig &config) {
        if (composite_) {
            return composite_->scan(config);
        }

        std::vector<DiscoveryFailure> failures;
        if (config.backends.empty()) {
            failures.emplace_back("enumerate", "platform_backend_unavailable", unavailable_reason_, std::nullopt,
                                  "platform", "select_backend", std::nullopt, "UnsupportedOperationError");
        } else {
            for (const auto &backend : config.backends) {
                failures.emplace_back("enumerate", "backend_not_found",
                                      "requested discovery backend is not available on this "
                                      "platform",
                                      std::nullopt, backend, "select_backend", std::nullopt,
                                      "UnsupportedOperationError");
            }
        }
        return DiscoveryReport{{}, {}, std::move(failures)};
    }

    DiscoveryReport discover_hands(const DiscoveryConfig &config) {
        PlatformDiscoverySource source;
        return discover_hands(config, source);
    }
} // namespace linkerbot::hand
