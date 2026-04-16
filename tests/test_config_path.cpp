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
#endif

    return 0;
}
