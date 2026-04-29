#include "modules/debug_module.h"
#include <iostream>

void logMessage(const std::string& message) {
    std::cout << "[DEBUG] " << message << std::endl;
}

void logError(const std::string& error) {
    std::cerr << "[ERROR] " << error << std::endl;
}