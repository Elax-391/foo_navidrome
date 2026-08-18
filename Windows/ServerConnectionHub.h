#pragma once

#include "ServerProfiles.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace navidrome {

// Main-thread notification sent after a persisted selection or a runtime-only
// failover has been mirrored to the legacy cfg variables. Passwords never
// enter the event bus.
struct ServerConnectionEvent {
    std::string profileId;
    std::string routeId;
    std::size_t routeIndex = 0;
    ServerRoute route = ServerRoute::Public;
    std::string serverUrl;
    std::string username;
    std::string reason;
    bool automatic = false;
    std::uint64_t revision = 0;
};

class ServerConnectionSubscription {
public:
    ServerConnectionSubscription() = default;
    ServerConnectionSubscription(ServerConnectionSubscription&& other) noexcept;
    ServerConnectionSubscription& operator=(
        ServerConnectionSubscription&& other) noexcept;
    ~ServerConnectionSubscription();

    ServerConnectionSubscription(const ServerConnectionSubscription&) = delete;
    ServerConnectionSubscription& operator=(
        const ServerConnectionSubscription&) = delete;

    void reset() noexcept;

private:
    struct Registration;
    explicit ServerConnectionSubscription(
        std::shared_ptr<Registration> registration);

    std::shared_ptr<Registration> m_registration;
    friend class ServerConnectionHub;
};

class ServerConnectionHub {
public:
    using Callback = std::function<void(const ServerConnectionEvent&)>;

    static ServerConnectionHub& get();
    ServerConnectionSubscription subscribe(Callback callback);
    std::uint64_t publish(ServerConnectionEvent event);

private:
    std::uint64_t m_revision = 0;
    std::vector<std::weak_ptr<ServerConnectionSubscription::Registration>>
        m_listeners;
};

} // namespace navidrome
