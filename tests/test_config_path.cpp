#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#define main vepc_embedded_main
#include "../main.cpp"
#undef main

int main() {
#ifndef _WIN32
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path() / "vepc-config-path-test";
    std::error_code ec;
    std::filesystem::create_directories(tempDir, ec);
    std::ofstream(tempDir / "interfaces.conf") << "S1-MME | SCTP | 0.0.0.0:36412 | eNodeB\n";
    std::ofstream(tempDir / "vepc.config") << "mcc = 001\n";

    setenv("CONFIG_PATH", tempDir.string().c_str(), 1);

    const std::string resolvedRead = resolveConfigPath("config/interfaces.conf");
    const std::string resolvedWrite = resolveWritableConfigPath("config/vepc.config");

    if (resolvedRead != (tempDir / "interfaces.conf").string()) {
        std::cerr << "Expected resolveConfigPath to honor CONFIG_PATH for interfaces.conf\n";
        return 1;
    }
    if (resolvedWrite != (tempDir / "vepc.config").string()) {
        std::cerr << "Expected resolveWritableConfigPath to honor CONFIG_PATH for vepc.config\n";
        return 1;
    }

    std::map<std::string, std::string> config;
    config["s1ap-port"] = "36412";
    config["gtp-c-port"] = "2123";
    config["gtp-u-port"] = "2152";
    config["s11-port"] = "2123";

    InterfaceConfigEntry s1ap{"S1-MME", "S1AP", "10.0.0.1:36412", "10.0.0.1", "36412", "eNodeB"};
    InterfaceConfigEntry diameter{"S6a", "Diameter", "10.0.0.2:3868", "10.0.0.2", "3868", "HSS"};
    InterfaceConfigEntry gtpc{"S11", "GTP-C", "10.0.0.3:2123", "10.0.0.3", "2123", "S-GW"};

    if (!isConnectionOrientedProtocol(s1ap)) {
        std::cerr << "Expected S1AP to be treated as connection-oriented\n";
        return 1;
    }
    if (!isConnectionOrientedProtocol(diameter)) {
        std::cerr << "Expected Diameter to be treated as connection-oriented\n";
        return 1;
    }
    if (!isGenericEndpointProtocol(diameter, config)) {
        std::cerr << "Expected Diameter alias to be treated as a generic endpoint\n";
        return 1;
    }
    if (!isGenericEndpointProtocol(gtpc, config)) {
        std::cerr << "Expected GTP-C alias to be treated as a generic endpoint\n";
        return 1;
    }
#endif

    return 0;
}
