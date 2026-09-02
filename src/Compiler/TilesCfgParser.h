#pragma once

#include <map>
#include <string>
#include <filesystem>

namespace FOEDAG {

struct TilesCfgResult {
    std::map<std::string, std::pair<int, int>> tiles_cfg;
    std::string error;

    bool contains(const std::string& tile) {
        auto it = tiles_cfg.find(tile);
        return it != tiles_cfg.end();
    }
};

TilesCfgResult parseTilesCfg(const std::filesystem::path& xmlPath);

}  // namespace FOEDAG
