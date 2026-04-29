// simple test for imsi_module
#include "imsi_module.h"
#include <iostream>
#include <vector>
#include <cassert>

int main() {
    vepc::ImsiModule m;
    std::string err;
    bool ok = m.addRange("25001", 0, 9, err);
    if (!ok) { std::cerr << "addRange failed: " << err << "\n"; return 2; }
    auto specs = m.list("25001");
    assert(specs.size() == 1);
    std::vector<std::string> out;
    m.generateForSpec(specs[0], [&](const std::string& imsi){ out.push_back(imsi); });
    assert(out.size() == 10);

    // multiple PLMN example
    bool ok2 = m.addRange("25001,310260", 0, 1, err);
    if (!ok2) { std::cerr << "addRange multi failed: " << err << "\n"; return 3; }
    auto specs2 = m.list("25001");
    // should include the new multi-plmn spec as well
    assert(specs2.size() >= 1);
    std::vector<std::string> out2;
    // find the spec we added (range 0..1)
    for (const auto &sp : m.list("25001")) {
        if (sp.kind == vepc::ImsiKind::Range && sp.start_msin == 0 && sp.end_msin == 1) {
            m.generateForSpec(sp, [&](const std::string& imsi){ out2.push_back(imsi); });
            break;
        }
    }
    // 2 PLMNs * 2 values each = 4
    assert(out2.size() == 4);
    for (auto &s : out) std::cout << s << "\n";
    return 0;
}
