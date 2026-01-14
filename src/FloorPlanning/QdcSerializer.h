#pragma once

#include <Device.h>
#include "TileDescriptor.h"

#include <filesystem>

namespace fp {

class QdcSerializer {
public:
    void save(const Device& device, const std::filesystem::path& filePath = "floorplanning.qdc");
    std::string serialize(const Device& device);
    void load(Device& device, const std::filesystem::path& filePath = "floorplanning.qdc");
    void load(Device& device, const std::vector<std::string>& lines);

    std::vector<std::string> readLines(const std::filesystem::path& filePath = "floorplanning.qdc") const;
    constexpr std::string_view lineDelimiter() const { return "\\\n"; }

    static std::optional<TileDescriptor> extractGridCoord(const std::string& data);
};

}  // namespace fp
