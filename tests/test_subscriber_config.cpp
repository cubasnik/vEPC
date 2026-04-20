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

    const auto apns = loadApnProfiles(config);
    ok &= expect(apns.size() == 2, "loads APN profiles from config map");
    ok &= expect(apns.count("corp") == 1, "contains named APN profile");
    ok &= expect(apns.at("corp").pgwAddress == "10.20.30.40", "stores PGW address for APN profile");

    const auto groups = loadImsiGroups(config);
    ok &= expect(groups.size() == 1, "loads IMSI groups from config map");
    ok &= expect(groups.at(0).name == "enterprise", "keeps IMSI group name");
    ok &= expect(groups.at(0).prefix == "25020", "keeps IMSI prefix");
    ok &= expect(groups.at(0).apnProfile == "corp", "keeps APN mapping for IMSI group");

    std::string matchedGroup;
    std::string selectedApn = resolveApnForImsi(config, "250201234567890", "", &matchedGroup);
    ok &= expect(selectedApn == "corp", "selects APN from IMSI-group mapping when APN is omitted");
    ok &= expect(matchedGroup == "enterprise", "returns matching IMSI group name");

    selectedApn = resolveApnForImsi(config, "001019999999999", "", nullptr);
    ok &= expect(selectedApn == "internet", "falls back to default APN when IMSI has no mapping");

    selectedApn = resolveApnForImsi(config, "250201234567890", "ims", nullptr);
    ok &= expect(selectedApn == "ims", "preserves explicitly requested APN");

    ok &= expect(isValidImsiPrefix("25020"), "accepts numeric IMSI prefix");
    ok &= expect(!isValidImsiPrefix("2502A"), "rejects non-numeric IMSI prefix");
    ok &= expect(isValidApnName("corp-data"), "accepts APN profile name");
    ok &= expect(!isValidApnName("corp data"), "rejects APN name with spaces");

    if (!ok) {
        return 1;
    }

    std::cout << "subscriber config tests passed\n";
    return 0;
}
