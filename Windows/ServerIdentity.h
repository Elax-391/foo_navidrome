#pragma once

#include <string>

namespace navidrome {

// Normalize only the URL portions that are case-insensitive. Paths remain
// case-sensitive so reverse-proxy installations keep distinct identities.
std::string normalizeServerUrl(const std::string& url);

// Stable partition key for account-scoped state, workers and UI events.
std::string serverAccountIdentity(const std::string& serverUrl,
                                  const std::string& username);

} // namespace navidrome
