#include <iostream>
#include <string>
#include <vector>
#include <map>

#include "src/subscriber_config.h"

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace vepc::subscriber;

    bool ok = true;

    std::map<std::string, std::string> config;
    config["default-apn"] = "internet";
    config["apn-profile.internet.pgw-address"] = "10.10.10.1";
    config["apn-profile.corp.pgw-address"] = "10.20.30.40";
    config["imsi-group.enterprise.prefix"] = "25020";
    config["imsi-group.enterprise.apn-profile"] = "corp";
    config["imsi-group.mvno.type"] = "series";
    config["imsi-group.mvno.plmn"] = "25020";
    config["imsi-group.mvno.series"] = "99";
    config["imsi-group.mvno.apn-profile"] = "internet";
    config["imsi-group.iot.type"] = "range";
    config["imsi-group.iot.plmn"] = "25020";
    config["imsi-group.iot.range-start"] = "3000000000";
    config["imsi-group.iot.range-end"] = "3000000999";
    config["imsi-group.iot.apn-profile"] = "corp";

    const auto apns = loadApnProfiles(config);
    ok &= expect(apns.size() == 2, "loads APN profiles from config map");
    ok &= expect(apns.count("corp") == 1, "contains named APN profile");
    ok &= expect(apns.at("corp").pgwAddress == "10.20.30.40", "stores PGW address for APN profile");

    const auto groups = loadImsiGroups(config);
    ok &= expect(groups.size() == 3, "loads IMSI series/range groups from config map");

    std::map<std::string, ImsiGroup> groupsByName;
    for (const auto& group : groups) {
        groupsByName[group.name] = group;
    }

    ok &= expect(groupsByName.count("enterprise") == 1, "loads legacy IMSI group");
    ok &= expect(groupsByName.at("enterprise").type == "series", "legacy prefix is treated as series");
    ok &= expect(groupsByName.at("enterprise").prefix == "25020", "keeps legacy IMSI prefix");

    ok &= expect(groupsByName.count("mvno") == 1, "loads explicit series IMSI group");
    ok &= expect(groupsByName.at("mvno").type == "series", "keeps series type");
    ok &= expect(groupsByName.at("mvno").plmn == "25020", "keeps PLMN binding for series group");

    ok &= expect(groupsByName.count("iot") == 1, "loads explicit range IMSI group");
    ok &= expect(groupsByName.at("iot").type == "range", "keeps range type");
    ok &= expect(groupsByName.at("iot").rangeStart == "250203000000000", "normalizes range start with PLMN prefix");
    ok &= expect(groupsByName.at("iot").rangeEnd == "250203000000999", "normalizes range end with PLMN prefix");

    std::string matchedGroup;
    std::string selectedApn = resolveApnForImsi(config, "250201234567890", "", &matchedGroup);
    ok &= expect(selectedApn == "corp", "selects APN from IMSI-group mapping when APN is omitted");
    ok &= expect(matchedGroup == "enterprise", "returns matching IMSI group name");

    selectedApn = resolveApnForImsi(config, "001019999999999", "", nullptr);
    ok &= expect(selectedApn == "internet", "falls back to default APN when IMSI has no mapping");

    selectedApn = resolveApnForImsi(config, "250209912345678", "", &matchedGroup);
    ok &= expect(selectedApn == "internet", "selects APN from IMSI series bound to PLMN");
    ok &= expect(matchedGroup == "mvno", "returns matching IMSI series group name");

    selectedApn = resolveApnForImsi(config, "250203000000321", "", &matchedGroup);
    ok &= expect(selectedApn == "corp", "selects APN from IMSI range bound to PLMN");
    ok &= expect(matchedGroup == "iot", "returns matching IMSI range group name");

    selectedApn = resolveApnForImsi(config, "250201234567890", "ims", nullptr);
    ok &= expect(selectedApn == "ims", "preserves explicitly requested APN");

    ok &= expect(isValidImsiPrefix("25020"), "accepts numeric IMSI prefix");
    ok &= expect(!isValidImsiPrefix("2502A"), "rejects non-numeric IMSI prefix");
    ok &= expect(isValidPlmn("25020"), "accepts 5-digit PLMN");
    ok &= expect(isValidPlmn("310260"), "accepts 6-digit PLMN");
    ok &= expect(!isValidPlmn("31A260"), "rejects non-numeric PLMN");
    ok &= expect(isValidApnName("corp-data"), "accepts APN profile name");
    ok &= expect(!isValidApnName("corp data"), "rejects APN name with spaces");

    if (!ok) {
        return 1;
    }

    std::cout << "subscriber config tests passed\n";
    return 0;
}
