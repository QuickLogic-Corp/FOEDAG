#pragma once

#include <map>
#include <filesystem>

namespace FOEDAG {

std::map<std::string, std::pair<int, int>> parseTilesCfg(const std::filesystem::path& xmlPath);

}  // namespace FOEDAG
