#ifndef DEBUG_MODULE_H
#define DEBUG_MODULE_H

#include <string>

// Функция для логирования сообщений
void logMessage(const std::string& message);

// Функция для установки уровня логирования
void setLogLevel(int level);

// Функция для инициализации модуля отладки
void initDebugModule();

#endif // DEBUG_MODULE_H