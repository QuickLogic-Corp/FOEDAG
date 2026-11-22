#pragma once

#include "DeviceGrid.h"
#include "TileDescriptor.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fp {

// One set_region command as written. Element paths and the partition name are unquoted.
struct SetRegionCommand {
    std::vector<std::string> elements;
    std::vector<std::string> regions;
    std::string partitionName;
    std::string comment;
};

class QdcSerializer {
public:
    // Words are separated by whitespace, except next to a comma, inside parentheses or inside
    // double quotes, so `a, b` is one list and `"dut.x y#1"` one name. A '#' outside
    // quotes starts the trailing comment. Nullopt unless the line is a set_region with
    // elements, regions and at most a partition name.
    static std::optional<SetRegionCommand> parseSetRegion(const std::string& line);

    void setFilePath(const std::filesystem::path& path) { m_path = path; }
    bool save(const DeviceGrid& device, const std::filesystem::path& overrideFilePath = "");
    std::string serialize(const DeviceGrid& device);
    void load(DeviceGrid& device, const std::filesystem::path& overrideFilePath = "");
    void load(DeviceGrid& device, const std::vector<std::string>& lines);

    static std::vector<std::string> readCommands(const std::filesystem::path& filePath);

    static std::optional<TileDescriptor> extractGridCoord(const std::string& data);

private:
    std::filesystem::path m_path = "floorplanning.qdc";
    std::string m_reservedContent;

    static constexpr std::string_view lfCommandDelimiter() { return "\\\n"; }
};

}  // namespace fp
