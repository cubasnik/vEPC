# C++ Debug Module

## Overview
This project implements a debugging module for C++ applications. It provides functions for logging, setting debug levels, and other utilities to facilitate debugging during development.

## Project Structure
```
cpp-debug-module
├── src
│   ├── main.cpp               # Entry point of the application
│   ├── modules
│   │   └── debug_module.cpp   # Implementation of debugging functions
│   └── CMakeLists.txt         # CMake configuration for source files
├── include
│   └── debug_module
│       └── debug_module.h     # Header file with function declarations
├── tests
│   └── test_debug_module.cpp   # Unit tests for the debug module
├── CMakeLists.txt             # Root CMake configuration
├── .gitignore                  # Files and directories to ignore in Git
└── README.md                   # Project documentation
```

## Building the Project
To build the project, follow these steps:

1. Ensure you have CMake installed on your system.
2. Open a terminal and navigate to the root directory of the project.
3. Create a build directory:
   ```
   mkdir build
   cd build
   ```
4. Run CMake to configure the project:
   ```
   cmake ..
   ```
5. Compile the project:
   ```
   make
   ```

## Usage
After building the project, you can run the application. The debugging module can be utilized by including the `debug_module.h` header in your source files and calling the provided functions.

## Testing
To run the tests for the debugging module, ensure you have built the project and then execute the test binary generated in the build directory.

## Contributing
Contributions are welcome! Please feel free to submit a pull request or open an issue for any suggestions or improvements.