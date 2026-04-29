# My C++ Project

This project is designed to demonstrate a modular approach to C++ programming, focusing on debugging and testing.

## Project Structure

```
my-cpp-project
├── src
│   ├── main.cpp               # Entry point of the application
│   └── modules
│       └── debug_module.cpp   # Implementation of debugging functions
├── include
│   └── modules
│       └── debug_module.h     # Header file for debugging functions
├── tests
│   └── test_debug_module.cpp   # Unit tests for debugging functions
├── CMakeLists.txt             # CMake configuration file
├── Makefile                    # Makefile for building the project
├── .vscode
│   └── launch.json             # Debugging configuration for the project
└── README.md                   # Project documentation
```

## Building the Project

To build the project, you can use either CMake or Make. 

### Using CMake

1. Navigate to the project directory:
   ```
   cd my-cpp-project
   ```
2. Create a build directory:
   ```
   mkdir build
   cd build
   ```
3. Run CMake to configure the project:
   ```
   cmake ..
   ```
4. Build the project:
   ```
   make
   ```

### Using Makefile

Simply run:
```
make
```

## Running the Application

After building the project, you can run the application by executing the generated binary in the `build` directory.

## Testing

To run the tests, ensure that you have built the project first. Then, execute the test binary, which is typically located in the `tests` directory.

## Contributing

Feel free to contribute to this project by submitting issues or pull requests. Your feedback and contributions are welcome!