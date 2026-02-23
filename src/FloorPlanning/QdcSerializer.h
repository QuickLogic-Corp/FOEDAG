#pragma once

#include "DeviceGrid.h"
#include "TileDescriptor.h"

#include <filesystem>

namespace fp {

class QdcSerializer {
public:
    void setFilePath(const std::filesystem::path& path) { m_path = path; }
    bool save(const DeviceGrid& device, const std::filesystem::path& overrideFilePath = "");
    std::string serialize(const DeviceGrid& device);
    void load(DeviceGrid& device, const std::filesystem::path& overrideFilePath = "");
    void load(DeviceGrid& device, const std::vector<std::string>& lines);

    std::vector<std::string> readLines(const std::filesystem::path& overrideFilePath = "") const;
    static constexpr std::string_view lineDelimiter() { return "\\\n"; }

    static std::optional<TileDescriptor> extractGridCoord(const std::string& data);

private:
    std::filesystem::path m_path = "floorplanning.qdc";
    std::string m_reservedContent;
};

}  // namespace fp
