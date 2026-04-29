// imsi_module.cpp
#include "imsi_module.h"
#include <sstream>
#include <iomanip>
#include <iostream>

namespace vepc {

static bool isDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

bool ImsiSpec::valid() const {
    if (plmns.empty()) return false;
    for (const auto &p : plmns) {
        if (!isDigits(p)) return false;
        if (p.size() != 5 && p.size() != 6) return false;
    }
    // Validate by checking first plmn length; rules apply per-plmn at generation time
    size_t ml = ImsiModule::msinLengthForPlmn(plmns[0]);
    if (kind == ImsiKind::Range) {
        uint64_t max = 1;
        for (size_t i = 0; i < ml; ++i) max *= 10;
        if (start_msin > end_msin) return false;
        if (end_msin >= max) return false;
        return true;
    } else {
        if (!isDigits(prefix)) return false;
        if (prefix.size() > ml) return false;
        uint64_t max = 1;
        for (size_t i = 0; i < ml; ++i) max *= 10;
        uint64_t pref_val = std::stoull(prefix);
        uint64_t start = pref_val;
        for (size_t i = prefix.size(); i < ml; ++i) start *= 10;
        if (count == 0) return false;
        if (start + count - 1 >= max) return false;
        return true;
    }
}

size_t ImsiModule::msinLengthForPlmn(const std::string& plmn) {
    return 15 - plmn.size();
}

bool ImsiModule::validatePlmn(const std::string& plmn) {
    return isDigits(plmn) && (plmn.size() == 5 || plmn.size() == 6);
}

static std::vector<std::string> splitPlmnList(const std::string& in) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : in) {
        if (c == ',') {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else if (!std::isspace(static_cast<unsigned char>(c))) {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool ImsiModule::addRange(const std::string& plmnList, uint64_t start_msin, uint64_t end_msin, std::string& err) {
    ImsiSpec s;
    s.plmns = splitPlmnList(plmnList);
    s.kind = ImsiKind::Range; s.start_msin = start_msin; s.end_msin = end_msin;
    if (!s.valid()) { err = "invalid range spec"; return false; }
    specs_.push_back(s);
    return true;
}

bool ImsiModule::addSeries(const std::string& plmnList, const std::string& prefix, uint64_t count, std::string& err) {
    ImsiSpec s; s.plmns = splitPlmnList(plmnList); s.kind = ImsiKind::Series; s.prefix = prefix; s.count = count;
    if (!s.valid()) { err = "invalid series spec"; return false; }
    specs_.push_back(s);
    return true;
}

std::vector<ImsiSpec> ImsiModule::list(const std::string& plmn) const {
    if (plmn.empty()) return specs_;
    std::vector<ImsiSpec> out;
    for (const auto &s : specs_) {
        for (const auto &p : s.plmns) {
            if (p == plmn) { out.push_back(s); break; }
        }
    }
    return out;
}

void ImsiModule::generateForSpec(const ImsiSpec& spec, std::function<void(const std::string&)> cb) const {
    // Generate for each PLMN in the spec
    for (const auto &plmn : spec.plmns) {
        size_t msin_len = msinLengthForPlmn(plmn);
        auto make_imsi = [&](uint64_t msin_val) {
            std::ostringstream os;
            os << plmn << std::setw((int)msin_len) << std::setfill('0') << msin_val;
            cb(os.str());
        };
        if (spec.kind == ImsiKind::Range) {
            for (uint64_t v = spec.start_msin; v <= spec.end_msin; ++v) make_imsi(v);
        } else {
            uint64_t pref_val = std::stoull(spec.prefix);
            uint64_t start = pref_val;
            for (size_t i = spec.prefix.size(); i < msin_len; ++i) start *= 10;
            for (uint64_t i = 0; i < spec.count; ++i) make_imsi(start + i);
        }
    }
}

void registerImsiCliCommands() {
    std::cerr << "[imsi_module] CLI registration placeholder\n";
}

} // namespace vepc
