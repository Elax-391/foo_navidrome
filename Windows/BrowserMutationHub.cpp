#include "BrowserMutationHub.h"

#include <algorithm>
#include <utility>
#include <vector>

struct navidrome::BrowserMutationSubscription::Registration {
    bool active = true;
    BrowserMutationHub::Callback callback;
};

bool navidrome::shouldApplyBrowserMutation(
        const BrowserMutationEvent& event, const std::string& currentIdentity,
        std::uint64_t lastRevision) noexcept {
    return event.identity == currentIdentity && event.revision > lastRevision;
}

navidrome::BrowserMutationSubscription::BrowserMutationSubscription(
        std::shared_ptr<Registration> registration)
    : m_registration(std::move(registration)) {}

navidrome::BrowserMutationSubscription::BrowserMutationSubscription(
        BrowserMutationSubscription&& other) noexcept
    : m_registration(std::move(other.m_registration)) {}

navidrome::BrowserMutationSubscription&
navidrome::BrowserMutationSubscription::operator=(
        BrowserMutationSubscription&& other) noexcept {
    if (this != &other) {
        reset();
        m_registration = std::move(other.m_registration);
    }
    return *this;
}

navidrome::BrowserMutationSubscription::~BrowserMutationSubscription() {
    reset();
}

void navidrome::BrowserMutationSubscription::reset() noexcept {
    if (m_registration) m_registration->active = false;
    m_registration.reset();
}

navidrome::BrowserMutationHub& navidrome::BrowserMutationHub::get() {
    static BrowserMutationHub hub;
    return hub;
}

navidrome::BrowserMutationSubscription navidrome::BrowserMutationHub::subscribe(
        Callback callback) {
    auto registration = std::make_shared<BrowserMutationSubscription::Registration>();
    registration->callback = std::move(callback);
    m_listeners.emplace_back(registration);
    return BrowserMutationSubscription(std::move(registration));
}

std::uint64_t navidrome::BrowserMutationHub::publish(BrowserMutationEvent event) {
    event.revision = ++m_revision;
    std::vector<std::shared_ptr<BrowserMutationSubscription::Registration>> listeners;
    m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(),
        [&](const auto& weak) {
            auto registration = weak.lock();
            if (!registration || !registration->active) return true;
            listeners.push_back(std::move(registration));
            return false;
        }), m_listeners.end());
    for (const auto& listener : listeners) {
        if (listener->active && listener->callback) listener->callback(event);
    }
    return event.revision;
}
