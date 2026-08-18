#include "ServerConnectionHub.h"

#include <algorithm>
#include <utility>

struct navidrome::ServerConnectionSubscription::Registration {
    bool active = true;
    ServerConnectionHub::Callback callback;
};

navidrome::ServerConnectionSubscription::ServerConnectionSubscription(
        std::shared_ptr<Registration> registration)
    : m_registration(std::move(registration)) {}

navidrome::ServerConnectionSubscription::ServerConnectionSubscription(
        ServerConnectionSubscription&& other) noexcept
    : m_registration(std::move(other.m_registration)) {}

navidrome::ServerConnectionSubscription&
navidrome::ServerConnectionSubscription::operator=(
        ServerConnectionSubscription&& other) noexcept {
    if (this != &other) {
        reset();
        m_registration = std::move(other.m_registration);
    }
    return *this;
}

navidrome::ServerConnectionSubscription::~ServerConnectionSubscription() {
    reset();
}

void navidrome::ServerConnectionSubscription::reset() noexcept {
    if (m_registration) m_registration->active = false;
    m_registration.reset();
}

navidrome::ServerConnectionHub& navidrome::ServerConnectionHub::get() {
    static ServerConnectionHub hub;
    return hub;
}

navidrome::ServerConnectionSubscription
navidrome::ServerConnectionHub::subscribe(Callback callback) {
    auto registration =
        std::make_shared<ServerConnectionSubscription::Registration>();
    registration->callback = std::move(callback);
    m_listeners.emplace_back(registration);
    return ServerConnectionSubscription(std::move(registration));
}

std::uint64_t navidrome::ServerConnectionHub::publish(
        ServerConnectionEvent event) {
    event.revision = ++m_revision;
    std::vector<std::shared_ptr<ServerConnectionSubscription::Registration>>
        listeners;
    m_listeners.erase(
        std::remove_if(m_listeners.begin(), m_listeners.end(),
            [&](const auto& weak) {
                auto registration = weak.lock();
                if (!registration || !registration->active) return true;
                listeners.push_back(std::move(registration));
                return false;
            }),
        m_listeners.end());
    for (const auto& listener : listeners) {
        if (listener->active && listener->callback)
            listener->callback(event);
    }
    return event.revision;
}
