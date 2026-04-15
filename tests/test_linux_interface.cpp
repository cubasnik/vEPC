#include <iostream>
#include <string>

#include "cli/linux_interface.h"

int main() {
    if (!isValidLinuxInterfaceName("eno2")) {
        std::cerr << "Expected eno2 to be a valid interface name\n";
        return 1;
    }

    if (!isValidLinuxInterfaceName("eno2.100")) {
        std::cerr << "Expected eno2.100 to be a valid interface name\n";
        return 1;
    }

    if (isValidLinuxInterfaceName("eno2; rm -rf /")) {
        std::cerr << "Unsafe interface name was accepted\n";
        return 1;
    }

    const std::string hint = interfaceManagementPrivilegeHint();
    if (hint.find("CAP_NET_ADMIN") == std::string::npos ||
        hint.find("root") == std::string::npos) {
        std::cerr << "Privilege hint must mention root and CAP_NET_ADMIN\n";
        return 1;
    }

    return 0;
}
