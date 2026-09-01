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


// The layout geometry an architecture actually describes, in the spelling
// config.json uses for it.
//
// Read the same way the device packages' own aurora/add_layout.py reads it
// (read_vars_from_arch): the array is the layout less its io and empty rings
// (ARRAY_X = width - 4), and a block's columns are the startx of every
// <region> of that type, carried 0-indexed the way BRAM_COLS/DSP_COLS are while
// the regions themselves are written one higher.
//
// layoutName selects among several <fixed_layout> elements and falls back to the
// first, again matching add_layout.py's find_fixed_layout().
struct LayoutGeometryResult {
    int array_x = 0;
    int array_y = 0;
    std::string bram_cols;   // "12,25"
    std::string dsp_cols;    // "6,19"
    std::string error;
};

LayoutGeometryResult parseLayoutGeometry(const std::filesystem::path& xmlPath,
                                         const std::string& layoutName = {});

}  // namespace FOEDAG
