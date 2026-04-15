#ifndef LINUX_INTERFACE_H
#define LINUX_INTERFACE_H

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
    #error "Linux interface operations are not supported on Windows"
#else
    #include <unistd.h>
#endif

/**
 * @brief Управление Linux интерфейсами (создание sub-интерфейсов, VLAN и т.д.)
 */

struct LinuxInterface {
    std::string name;
    std::string parent;  // родительский интерфейс (e.g., eth0)
    int vlan_id;         // -1 если не VLAN
    std::string ip;
    std::string netmask;
    bool is_up;
};

inline bool isValidLinuxInterfaceName(const std::string& name) {
    if (name.empty() || name.size() > 15) {
        return false;
    }

    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
    });
}

inline bool hasEffectiveCapNetAdmin() {
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.rfind("CapEff:", 0) == 0) {
            const std::string cap_hex = line.substr(7);
            const unsigned long long caps = std::strtoull(cap_hex.c_str(), nullptr, 16);
            constexpr unsigned long long kCapNetAdminBit = 1ULL << 12;
            return (caps & kCapNetAdminBit) != 0;
        }
    }
    return false;
}

inline std::string interfaceManagementPrivilegeHint() {
    return "Interface operations require root or CAP_NET_ADMIN. In Docker, run the container as root with NET_ADMIN and host networking for access to host NICs.";
}

inline bool hasInterfaceManagementPrivileges() {
    return geteuid() == 0 || hasEffectiveCapNetAdmin();
}

inline bool ensureInterfaceManagementPrivileges(const std::string& action) {
    if (hasInterfaceManagementPrivileges()) {
        return true;
    }

    std::cerr << "Error: " << action << " requires elevated network privileges\n";
    std::cerr << "Hint: " << interfaceManagementPrivilegeHint() << "\n";
    return false;
}

inline bool interfaceExists(const std::string& iface_name) {
    if (!isValidLinuxInterfaceName(iface_name)) {
        return false;
    }
    const std::string cmd = "ip link show " + iface_name + " > /dev/null 2>&1";
    return system(cmd.c_str()) == 0;
}

/**
 * Создать VLAN интерфейс (sub-интерфейс)
 * @param parent физический интерфейс (eth0, eth1 и т.д.)
 * @param vlan_id VLAN ID (1-4094)
 * @return успешно ли создано
 */
inline bool createVlanInterface(const std::string& parent, int vlan_id) {
    if (!isValidLinuxInterfaceName(parent)) {
        std::cerr << "Error: Invalid parent interface name\n";
        return false;
    }

    if (vlan_id < 1 || vlan_id > 4094) {
        std::cerr << "Error: VLAN ID must be between 1 and 4094\n";
        return false;
    }

    if (!ensureInterfaceManagementPrivileges("create-vlan")) {
        return false;
    }

    if (!interfaceExists(parent)) {
        std::cerr << "Error: Parent interface " << parent << " was not found\n";
        return false;
    }

    const std::string subif_name = parent + "." + std::to_string(vlan_id);
    if (interfaceExists(subif_name)) {
        std::cerr << "Error: Interface " << subif_name << " already exists\n";
        return false;
    }

    const std::string create_cmd = "ip link add link " + parent + " name " + subif_name + " type vlan id " + std::to_string(vlan_id);
    if (system(create_cmd.c_str()) != 0) {
        std::cerr << "Error: Failed to create interface " << subif_name << "\n";
        std::cerr << "Hint: Verify that the parent interface is available and not managed by another subsystem\n";
        return false;
    }

    std::cout << "Interface " << subif_name << " created successfully\n";
    return true;
}

/**
 * Удалить интерфейс
 * @param iface_name имя интерфейса
 * @return успешно ли удалено
 */
inline bool deleteInterface(const std::string& iface_name) {
    if (!isValidLinuxInterfaceName(iface_name)) {
        std::cerr << "Error: Invalid interface name\n";
        return false;
    }
    if (!ensureInterfaceManagementPrivileges("delete-interface")) {
        return false;
    }

    std::string cmd = "ip link delete " + iface_name;
    if (system(cmd.c_str()) != 0) {
        std::cerr << "Error: Failed to delete interface " << iface_name << "\n";
        return false;
    }
    std::cout << "Interface " << iface_name << " deleted successfully\n";
    return true;
}

/**
 * Установить IP адрес интерфейсу
 * @param iface_name имя интерфейса
 * @param ip IP адрес в формате x.x.x.x/prefix
 * @return успешно ли установлено
 */
inline bool setInterfaceIp(const std::string& iface_name, const std::string& ip) {
    if (!isValidLinuxInterfaceName(iface_name)) {
        std::cerr << "Error: Invalid interface name\n";
        return false;
    }
    if (!ensureInterfaceManagementPrivileges("set-ip")) {
        return false;
    }

    std::string cmd = "ip addr add " + ip + " dev " + iface_name;
    if (system(cmd.c_str()) != 0) {
        std::cerr << "Error: Failed to set IP for interface " << iface_name << "\n";
        return false;
    }
    std::cout << "IP " << ip << " assigned to interface " << iface_name << "\n";
    return true;
}

/**
 * Поднять интерфейс
 * @param iface_name имя интерфейса
 * @return успешно ли поднято
 */
inline bool bringUpInterface(const std::string& iface_name) {
    if (!isValidLinuxInterfaceName(iface_name)) {
        std::cerr << "Error: Invalid interface name\n";
        return false;
    }
    if (!ensureInterfaceManagementPrivileges("up-interface")) {
        return false;
    }

    std::string cmd = "ip link set " + iface_name + " up";
    if (system(cmd.c_str()) != 0) {
        std::cerr << "Error: Failed to bring up interface " << iface_name << "\n";
        return false;
    }
    std::cout << "Interface " << iface_name << " is now up\n";
    return true;
}

/**
 * Опустить интерфейс
 * @param iface_name имя интерфейса
 * @return успешно ли опущено
 */
inline bool bringDownInterface(const std::string& iface_name) {
    if (!isValidLinuxInterfaceName(iface_name)) {
        std::cerr << "Error: Invalid interface name\n";
        return false;
    }
    if (!ensureInterfaceManagementPrivileges("down-interface")) {
        return false;
    }

    std::string cmd = "ip link set " + iface_name + " down";
    if (system(cmd.c_str()) != 0) {
        std::cerr << "Error: Failed to bring down interface " << iface_name << "\n";
        return false;
    }
    std::cout << "Interface " << iface_name << " is now down\n";
    return true;
}

/**
 * Получить статус интерфейса
 * @param iface_name имя интерфейса
 * @return true если интерфейс UP, false если DOWN
 */
inline bool isInterfaceUp(const std::string& iface_name) {
    std::string cmd = "ip link show " + iface_name + " | grep -q UP";
    return system(cmd.c_str()) == 0;
}

/**
 * Получить IP адрес интерфейса
 * @param iface_name имя интерфейса
 * @return IP адрес или пустая строка если нет
 */
inline std::string getInterfaceIp(const std::string& iface_name) {
    std::string cmd = "ip addr show " + iface_name + " | grep 'inet ' | awk '{print $2}' | head -1";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    
    char buffer[256];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    pclose(pipe);
    
    // Remove trailing newline
    if (!result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

/**
 * Список всех интерфейсов
 * @return вектор имён интерфейсов
 */
inline std::vector<std::string> listAllInterfaces() {
    std::vector<std::string> interfaces;
    FILE* pipe = popen("ip link show | grep '^[0-9]' | awk '{print $2}' | tr -d ':'", "r");
    if (!pipe) return interfaces;
    
    char buffer[256];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        std::string line(buffer);
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
        }
        if (!line.empty()) {
            interfaces.push_back(line);
        }
    }
    pclose(pipe);
    return interfaces;
}

#endif // LINUX_INTERFACE_H
