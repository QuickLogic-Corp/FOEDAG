#pragma once

#include <DeviceGrid.h>
#include "TileDescriptor.h"

#include <filesystem>

namespace fp {

class QdcSerializer {
public:
    void save(const DeviceGrid& device, const std::filesystem::path& filePath = "floorplanning.qdc");
    std::string serialize(const DeviceGrid& device);
    void load(DeviceGrid& device, const std::filesystem::path& filePath = "floorplanning.qdc");
    void load(DeviceGrid& device, const std::vector<std::string>& lines);

    std::vector<std::string> readLines(const std::filesystem::path& filePath = "floorplanning.qdc") const;
    constexpr std::string_view lineDelimiter() const { return "\\\n"; }

    static std::optional<TileDescriptor> extractGridCoord(const std::string& data);

private:
    std::string m_reservedContent;
};

}  // namespace fp
