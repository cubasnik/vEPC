#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define main vepc_cli_embedded_main
#include "../cli/vepc-cli.cpp"
#undef main

namespace {

bool contains(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

bool containsDigitSuggestion(const std::vector<std::string>& values) {
    return std::any_of(values.begin(), values.end(), [](const std::string& value) {
        return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isdigit(ch) != 0;
        });
    });
}

} // namespace

int main() {
#ifndef _WIN32
    setenv("VEPC_TRAFFIC_PORTS", "eno3,eno4", 1);
#endif

    const auto vlanPorts = completionCandidates(CliMode::Exec, "create-vlan ", "");
    if (!contains(vlanPorts, "eno3") || !contains(vlanPorts, "eno4")) {
        std::cerr << "Expected create-vlan completion to suggest allowed traffic interfaces\n";
        return 1;
    }

    const auto vlanIds = completionCandidates(CliMode::Exec, "create-vlan eno3 ", "");
    if (vlanIds.empty() || !containsDigitSuggestion(vlanIds)) {
        std::cerr << "Expected create-vlan completion to suggest VLAN IDs\n";
        return 1;
    }

    const auto deleteVlanPorts = completionCandidates(CliMode::Exec, "delete-vlan ", "");
    if (!contains(deleteVlanPorts, "eno3") || !contains(deleteVlanPorts, "eno4")) {
        std::cerr << "Expected delete-vlan completion to suggest allowed traffic interfaces\n";
        return 1;
    }

    const auto deleteVlanIds = completionCandidates(CliMode::Exec, "delete-vlan eno3 ", "");
    if (deleteVlanIds.empty() || !containsDigitSuggestion(deleteVlanIds)) {
        std::cerr << "Expected delete-vlan completion to suggest VLAN IDs\n";
        return 1;
    }

    const auto hostnameHints = completionCandidates(CliMode::Config, "hostname ", "");
    if (hostnameHints.empty()) {
        std::cerr << "Expected hostname completion to provide an example value\n";
        return 1;
    }

    const auto bindHints = completionCandidates(CliMode::InterfaceConfig, "bind ", "S1-MME");
    if (!contains(bindHints, "eno3") || !contains(bindHints, "eno4")) {
        std::cerr << "Expected bind completion to suggest allowed traffic interfaces\n";
        return 1;
    }

    return 0;
}
