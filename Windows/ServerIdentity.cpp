#include "ServerIdentity.h"

#include <algorithm>
#include <cctype>

std::string navidrome::normalizeServerUrl(const std::string& url) {
    std::string result = url;
    while (!result.empty() && (result.front() == ' ' || result.front() == '\t'))
        result.erase(result.begin());
    while (!result.empty() && (result.back() == '/' || result.back() == ' ' ||
                               result.back() == '\t')) result.pop_back();
    const auto schemeEnd = result.find("://");
    if (schemeEnd == std::string::npos) return result;
    const auto authorityEnd = result.find_first_of("/?#", schemeEnd + 3);
    const auto caseInsensitiveEnd = authorityEnd == std::string::npos
        ? result.size() : authorityEnd;
    std::transform(result.begin(), result.begin() + caseInsensitiveEnd,
                   result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

std::string navidrome::serverAccountIdentity(const std::string& serverUrl,
                                              const std::string& username) {
    return normalizeServerUrl(serverUrl) + "\n" + username;
}
