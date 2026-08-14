#pragma once

#include "../SubsonicTypes.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace navidrome {

enum class BrowserMutationKind {
    FavoriteChanged,
    RatingChanged,
    PlaylistCatalogChanged,
};

struct BrowserMutationEvent {
    std::string identity;
    BrowserMutationKind kind = BrowserMutationKind::FavoriteChanged;
    FavoriteKind entityKind = FavoriteKind::Song;
    std::string entityId;
    bool favorite = false;
    int rating = 0;
    std::uint64_t revision = 0;
};

bool shouldApplyBrowserMutation(const BrowserMutationEvent& event,
                                const std::string& currentIdentity,
                                std::uint64_t lastRevision) noexcept;

class BrowserMutationSubscription {
public:
    BrowserMutationSubscription() = default;
    BrowserMutationSubscription(BrowserMutationSubscription&& other) noexcept;
    BrowserMutationSubscription& operator=(BrowserMutationSubscription&& other) noexcept;
    ~BrowserMutationSubscription();

    BrowserMutationSubscription(const BrowserMutationSubscription&) = delete;
    BrowserMutationSubscription& operator=(const BrowserMutationSubscription&) = delete;
    void reset() noexcept;

private:
    struct Registration;
    explicit BrowserMutationSubscription(std::shared_ptr<Registration> registration);
    std::shared_ptr<Registration> m_registration;
    friend class BrowserMutationHub;
};

class BrowserMutationHub {
public:
    using Callback = std::function<void(const BrowserMutationEvent&)>;

    static BrowserMutationHub& get();
    BrowserMutationSubscription subscribe(Callback callback);
    std::uint64_t publish(BrowserMutationEvent event);

private:
    std::uint64_t m_revision = 0;
    std::vector<std::weak_ptr<BrowserMutationSubscription::Registration>> m_listeners;
};

} // namespace navidrome
