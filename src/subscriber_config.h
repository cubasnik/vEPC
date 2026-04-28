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
    std::string type;
    std::string plmn;
    std::string series;
    std::string rangeStart;
    std::string rangeEnd;
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

inline bool isValidPlmn(const std::string& value) {
    return (value.size() == 5 || value.size() == 6) && isDigitsOnly(value);
}

inline bool isValidImsiRangeBoundary(const std::string& value) {
    return value.size() >= 5 && value.size() <= 15 && isDigitsOnly(value);
}

inline bool compareImsiNumeric(const std::string& lhs, const std::string& rhs, bool& lessThan) {
    if (lhs.size() != rhs.size() || !isDigitsOnly(lhs) || !isDigitsOnly(rhs)) {
        return false;
    }
    lessThan = lhs < rhs;
    return true;
}

inline std::string normalizeImsiSeriesPrefix(const ImsiGroup& group) {
    std::string series = group.series;
    if (series.empty()) {
        series = group.prefix;
    }
    if (series.empty()) {
        return "";
    }

    if (!group.plmn.empty() && series.rfind(group.plmn, 0) != 0) {
        return group.plmn + series;
    }
    return series;
}

inline bool normalizeImsiRangeBoundaries(const ImsiGroup& group,
                                         std::string& startOut,
                                         std::string& endOut) {
    startOut = group.rangeStart;
    endOut = group.rangeEnd;

    if (startOut.empty() || endOut.empty()) {
        return false;
    }

    if (!group.plmn.empty()) {
        if (startOut.rfind(group.plmn, 0) != 0) {
            startOut = group.plmn + startOut;
        }
        if (endOut.rfind(group.plmn, 0) != 0) {
            endOut = group.plmn + endOut;
        }
    }

    return true;
}

inline bool imsiMatchesGroup(const ImsiGroup& group, const std::string& imsi) {
    if (imsi.empty() || !isDigitsOnly(imsi)) {
        return false;
    }
    if (!group.plmn.empty() && imsi.rfind(group.plmn, 0) != 0) {
        return false;
    }

    if (group.type == "range") {
        std::string rangeStart;
        std::string rangeEnd;
        if (!normalizeImsiRangeBoundaries(group, rangeStart, rangeEnd)) {
            return false;
        }
        if (!isValidImsiRangeBoundary(rangeStart) || !isValidImsiRangeBoundary(rangeEnd)) {
            return false;
        }
        if (rangeStart.size() != rangeEnd.size() || imsi.size() != rangeStart.size()) {
            return false;
        }

        bool startLessThanEnd = false;
        if (!compareImsiNumeric(rangeStart, rangeEnd, startLessThanEnd)) {
            return false;
        }
        if (!startLessThanEnd && rangeStart != rangeEnd) {
            return false;
        }

        return imsi >= rangeStart && imsi <= rangeEnd;
    }

    const std::string seriesPrefix = normalizeImsiSeriesPrefix(group);
    if (seriesPrefix.empty()) {
        return false;
    }
    return imsi.rfind(seriesPrefix, 0) == 0;
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
    constexpr const char* suffixType = ".type";
    constexpr const char* suffixPlmn = ".plmn";
    constexpr const char* suffixSeries = ".series";
    constexpr const char* suffixRangeStart = ".range-start";
    constexpr const char* suffixRangeEnd = ".range-end";
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
        if (field == suffixType) {
            group.type = value;
        } else if (field == suffixPlmn) {
            group.plmn = value;
        } else if (field == suffixSeries) {
            group.series = value;
        } else if (field == suffixRangeStart) {
            group.rangeStart = value;
        } else if (field == suffixRangeEnd) {
            group.rangeEnd = value;
        } else if (field == suffixPrefix) {
            group.prefix = value;
        } else if (field == suffixApn) {
            group.apnProfile = value;
        }
    }

    std::vector<ImsiGroup> result;
    for (const auto& [name, group] : indexed) {
        ImsiGroup normalized = group;

        if (normalized.type.empty()) {
            normalized.type = (!normalized.rangeStart.empty() || !normalized.rangeEnd.empty())
                ? "range"
                : "series";
        }

        if (normalized.type == "series") {
            if (normalized.series.empty()) {
                normalized.series = normalized.prefix;
            }

            const std::string effectiveSeries = normalizeImsiSeriesPrefix(normalized);
            if (!effectiveSeries.empty() && isValidImsiPrefix(effectiveSeries)
                && (normalized.plmn.empty() || isValidPlmn(normalized.plmn))) {
                result.push_back(normalized);
            }
            continue;
        }

        if (normalized.type == "range") {
            std::string normalizedStart;
            std::string normalizedEnd;
            if (!normalizeImsiRangeBoundaries(normalized, normalizedStart, normalizedEnd)) {
                continue;
            }
            if (!isValidImsiRangeBoundary(normalizedStart) || !isValidImsiRangeBoundary(normalizedEnd)) {
                continue;
            }
            bool startLessThanEnd = false;
            if (!compareImsiNumeric(normalizedStart, normalizedEnd, startLessThanEnd)) {
                continue;
            }
            if (!startLessThanEnd && normalizedStart != normalizedEnd) {
                continue;
            }
            if (!normalized.plmn.empty() && !isValidPlmn(normalized.plmn)) {
                continue;
            }
            normalized.rangeStart = normalizedStart;
            normalized.rangeEnd = normalizedEnd;
            result.push_back(normalized);
        }
    }

    std::sort(result.begin(), result.end(), [](const ImsiGroup& a, const ImsiGroup& b) {
        const auto score = [](const ImsiGroup& group) {
            if (group.type == "range") {
                return group.rangeStart.size();
            }
            return normalizeImsiSeriesPrefix(group).size();
        };

        const size_t lhsScore = score(a);
        const size_t rhsScore = score(b);
        if (lhsScore != rhsScore) {
            return lhsScore > rhsScore;
        }

        if (a.type != b.type) {
            return a.type == "range";
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
        if (imsiMatchesGroup(group, imsi)) {
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
        if (!group.plmn.empty()) {
            oss << "    plmn " << group.plmn << "\n";
        }
        if (group.type == "range") {
            oss << "    range " << group.rangeStart << " " << group.rangeEnd << "\n";
        } else {
            const std::string seriesPrefix = normalizeImsiSeriesPrefix(group);
            oss << "    series " << (seriesPrefix.empty() ? group.series : seriesPrefix) << "\n";
        }
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
