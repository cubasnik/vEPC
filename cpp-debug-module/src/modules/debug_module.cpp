#include "debug_module/debug_module.h"
#include <iostream>

void logMessage(const std::string& message) {
    std::cout << "[DEBUG] " << message << std::endl;
}

void setDebugLevel(int level) {
    // Здесь можно добавить логику для установки уровня отладки
    std::cout << "[DEBUG] Уровень отладки установлен на: " << level << std::endl;
}