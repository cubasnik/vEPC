#ifndef VEPC_SUBSCRIBER_CONFIG_H
#define VEPC_SUBSCRIBER_CONFIG_H

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace vepc::subscriber {

struct ApnProfile {
    std::string name;
    std::string pgwAddress;
};

struct ImsiGroup {
    std::string name;
    std::string prefix;
    std::string apnProfile;
};

inline bool isDigitsOnly(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (char ch : value) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }
    return true;
}

inline bool isValidImsiPrefix(const std::string& value) {
    return value.size() >= 5 && value.size() <= 15 && isDigitsOnly(value);
}

inline bool isValidApnName(const std::string& value) {
    if (value.empty() || value.size() > 64) {
        return false;
    }
    for (char ch : value) {
        const bool ok = std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_' || ch == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

inline std::map<std::string, ApnProfile> loadApnProfiles(const std::map<std::string, std::string>& config) {
    constexpr const char* prefix = "apn-profile.";
    constexpr const char* suffix = ".pgw-address";

    std::map<std::string, ApnProfile> result;
    for (const auto& [key, value] : config) {
        if (key.rfind(prefix, 0) != 0 || key.size() <= std::strlen(prefix) + std::strlen(suffix)) {
            continue;
        }
        if (key.size() <= std::strlen(suffix) || key.substr(key.size() - std::strlen(suffix)) != suffix) {
            continue;
        }

        const std::string name = key.substr(std::strlen(prefix), key.size() - std::strlen(prefix) - std::strlen(suffix));
        if (!isValidApnName(name)) {
            continue;
        }

        ApnProfile& profile = result[name];
        profile.name = name;
        profile.pgwAddress = value;
    }
    return result;
}

inline std::vector<ImsiGroup> loadImsiGroups(const std::map<std::string, std::string>& config) {
    constexpr const char* prefix = "imsi-group.";
    constexpr const char* suffixPrefix = ".prefix";
    constexpr const char* suffixApn = ".apn-profile";

    std::map<std::string, ImsiGroup> indexed;
    for (const auto& [key, value] : config) {
        if (key.rfind(prefix, 0) != 0) {
            continue;
        }

        const std::string remainder = key.substr(std::strlen(prefix));
        const size_t dot = remainder.find('.');
        if (dot == std::string::npos) {
            continue;
        }

        const std::string name = remainder.substr(0, dot);
        const std::string field = remainder.substr(dot);
        if (name.empty()) {
            continue;
        }

        ImsiGroup& group = indexed[name];
        group.name = name;
        if (field == suffixPrefix) {
            group.prefix = value;
        } else if (field == suffixApn) {
            group.apnProfile = value;
        }
    }

    std::vector<ImsiGroup> result;
    for (const auto& [name, group] : indexed) {
        if (!group.prefix.empty()) {
            result.push_back(group);
        }
    }

    std::sort(result.begin(), result.end(), [](const ImsiGroup& a, const ImsiGroup& b) {
        if (a.prefix.size() != b.prefix.size()) {
            return a.prefix.size() > b.prefix.size();
        }
        return a.name < b.name;
    });

    return result;
}

inline std::string resolveApnForImsi(const std::map<std::string, std::string>& config,
                                     const std::string& imsi,
                                     const std::string& requestedApn,
                                     std::string* matchedGroupName = nullptr) {
    if (matchedGroupName != nullptr) {
        matchedGroupName->clear();
    }

    if (!requestedApn.empty()) {
        return requestedApn;
    }

    for (const auto& group : loadImsiGroups(config)) {
        if (group.prefix.empty()) {
            continue;
        }
        if (imsi.rfind(group.prefix, 0) == 0) {
            if (matchedGroupName != nullptr) {
                *matchedGroupName = group.name;
            }
            if (!group.apnProfile.empty()) {
                return group.apnProfile;
            }
            break;
        }
    }

    const auto defaultIt = config.find("default-apn");
    if (defaultIt != config.end() && !defaultIt->second.empty()) {
        return defaultIt->second;
    }

    return requestedApn;
}

inline std::string formatSubscriberConfig(const std::map<std::string, std::string>& config) {
    std::ostringstream oss;
    const auto apns = loadApnProfiles(config);
    const auto groups = loadImsiGroups(config);

    const auto defaultApnIt = config.find("default-apn");
    const auto imsiPrefixIt = config.find("imsi-prefix");

    if ((defaultApnIt == config.end() || defaultApnIt->second.empty())
        && (imsiPrefixIt == config.end() || imsiPrefixIt->second.empty())
        && apns.empty() && groups.empty()) {
        return "";
    }

    oss << "subscriber-config\n";
    if (imsiPrefixIt != config.end() && !imsiPrefixIt->second.empty()) {
        oss << "  imsi-prefix " << imsiPrefixIt->second << "\n";
    }
    if (defaultApnIt != config.end() && !defaultApnIt->second.empty()) {
        oss << "  apn default-apn-profile " << defaultApnIt->second << "\n";
    }
    for (const auto& group : groups) {
        oss << "  imsi-group " << group.name << "\n";
        oss << "    prefix " << group.prefix << "\n";
        if (!group.apnProfile.empty()) {
            oss << "    apn-profile " << group.apnProfile << "\n";
        }
        oss << "  #exit\n";
    }
    for (const auto& [name, apn] : apns) {
        oss << "  apn-profile " << name << "\n";
        if (!apn.pgwAddress.empty()) {
            oss << "    pgw-address " << apn.pgwAddress << "\n";
        }
        oss << "  #exit\n";
    }

    return oss.str();
}

} // namespace vepc::subscriber

#endif
