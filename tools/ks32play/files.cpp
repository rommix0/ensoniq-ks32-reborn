#include "files.h"

#include <fstream>
#include <iterator>

bool readFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& err) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        err = "cannot open";
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (f.bad()) {
        err = "cannot read";
        return false;
    }
    return true;
}

bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes, std::string& err) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) {
        err = "cannot write";
        return false;
    }
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    f.flush();
    if (!f) {
        err = "cannot write";
        return false;
    }
    return true;
}
