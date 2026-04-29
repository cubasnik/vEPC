#include <iostream>
#include "debug_module/debug_module.h"

int main() {
    // Установка уровня отладки
    setDebugLevel(1);

    // Логирование сообщения
    logMessage("Программа запущена.");

    // Здесь может быть основной код приложения

    logMessage("Программа завершена.");
    return 0;
}