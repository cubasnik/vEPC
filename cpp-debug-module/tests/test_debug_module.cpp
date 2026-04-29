#include "debug_module/debug_module.h"
#include <gtest/gtest.h>

TEST(DebugModuleTest, LogMessage) {
    // Arrange
    std::string message = "Test log message";
    
    // Act
    logMessage(message);
    
    // Assert
    // Here you would typically check the output of the logMessage function
    // For example, if it writes to a file or console, you would verify that.
}

TEST(DebugModuleTest, SetDebugLevel) {
    // Arrange
    int level = 2;
    
    // Act
    setDebugLevel(level);
    
    // Assert
    // Here you would check if the debug level was set correctly
    // For example, you might have a function to get the current debug level.
}