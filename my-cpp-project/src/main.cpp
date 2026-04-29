#include <iostream>
#include "modules/debug_module.h"

int main() {
    std::cout << "Starting the application..." << std::endl;

    // Call a function from the debug module
    debugFunction();

    std::cout << "Application finished." << std::endl;
    return 0;
}