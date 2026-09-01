#pragma once

#include <map>
#include <filesystem>

namespace FOEDAG {

struct TilesCfgResult {
    std::map<std::string, std::pair<int, int>> tiles_cfg;

    // Columns the layout gives each block type, counted from its <region>/<col>
    // entries. The architecture is the only source of this that is also right for a
    // device Aurora generated: the generated package inherits config.json's
    // BRAM_COLS/DSP_COLS from the package it was generated FROM.
    // -1 means the layout describes them in a form this parser cannot count - a
    // <col> with repeatx, or a non-numeric bound - and the caller must treat that
    // as unknown rather than as none.
    std::map<std::string, int> layout_columns;

    std::string error;

    bool contains(const std::string& tile) {
        auto it = tiles_cfg.find(tile);
        return it != tiles_cfg.end();
    }

    // 0 when the layout places none of that type.
    int columnsOf(const std::string& tile) const {
        auto it = layout_columns.find(tile);
        return (it == layout_columns.end()) ? 0 : it->second;
    }
};

// layoutName selects among several <fixed_layout> elements and is the "name"
// attribute QLDeviceManager discovered the layout by. When it matches none - a
// generated device whose layout carries a different name - the only fixed_layout
// there is stands in; several with no match is an error rather than a guess.
TilesCfgResult parseTilesCfg(const std::filesystem::path& xmlPath,
                             const std::string& layoutName = {});

}  // namespace FOEDAG
