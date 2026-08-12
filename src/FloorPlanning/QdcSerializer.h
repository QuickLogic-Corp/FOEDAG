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

    // [aurora2#1725 stage P1] Machine-readable form of the same intent, written beside the
    // .qdc on every save. Downstream stages read THIS, never the .qdc: that file is a
    // Tcl-flavoured DSL with line continuations, and every consumer that re-parses it is
    // another place for the dialects to drift apart. Keeping the parser here, in the one
    // component that owns the format, is the point.
    // See docs/specs/region-based-placement-synthesis-integration/pipeline.md (A.P1).
    std::string serializeFloorplanJson(const DeviceGrid& device,
                                       const std::filesystem::path& qdcPath) const;
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
