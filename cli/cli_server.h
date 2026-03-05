#ifndef CLI_SERVER_H
#define CLI_SERVER_H

#include <string>

class CliServer {  // <- убедись, что имя совпадает с cpp
public:
    CliServer(const std::string& socket_path);
    void run();
private:
    std::string socketPath;
};

#endif