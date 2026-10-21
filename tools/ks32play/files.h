#pragma once
// The only file I/O of the ks32play app (the player library in src/player never touches files).
#include <cstdint>
#include <string>
#include <vector>

// Whole file into `bytes`. On failure returns false with err = "cannot open" or "cannot read".
bool readFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& err);

// Whole buffer to `path` (binary, truncating). On failure returns false with err = "cannot write".
bool writeFile(const std::string& path, const std::vector<uint8_t>& bytes, std::string& err);
