#ifndef LINUX_INTERFACE_H
#define LINUX_INTERFACE_H

#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>
#include <iostream>

#ifdef _WIN32
    #error "Linux interface operations are not supported on Windows"
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

/**
 * Создать VLAN интерфейс (sub-интерфейс)
 * @param parent физический интерфейс (eth0, eth1 и т.д.)
 * @param vlan_id VLAN ID (1-4094)
 * @return успешно ли создано
 */
inline bool createVlanInterface(const std::string& parent, int vlan_id) {
    if (vlan_id < 1 || vlan_id > 4094) {
        std::cerr << "Error: VLAN ID must be between 1 and 4094\n";
        return false;
    }
    
    std::string subif_name = parent + "." + std::to_string(vlan_id);
    
    // Проверить, существует ли интерфейс
    std::string check_cmd = "ip link show " + subif_name + " > /dev/null 2>&1";
    if (system(check_cmd.c_str()) == 0) {
        std::cerr << "Error: Interface " << subif_name << " already exists\n";
        return false;
    }
    
    // Создать sub-интерфейс
    std::string create_cmd = "ip link add link " + parent + " name " + subif_name + " type vlan id " + std::to_string(vlan_id);
    if (system(create_cmd.c_str()) != 0) {
        std::cerr << "Error: Failed to create interface " << subif_name << "\n";
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
