#pragma once

#include "DeviceGrid.h"

#include <filesystem>
#include <string>

namespace fp {

// [aurora2#1725 REQ-004] The .qdc validation the FloorPlanning panel performs, with no panel.
// Batch compiles never construct the UI, so until now nothing checked whether the regions a
// user wrote could actually hold what they constrain -- the flow ran and the packer dealt
// with it silently. The rules are not reimplemented here: this assembles the same DeviceGrid
// the panel drives and calls the same DeviceGrid::checkIssues(), so a rule added to one is
// present in both; what this class owns is the assembly, which the panel otherwise does
// across three widgets.
//
// Classifies; the caller decides. Errors mean the floorplan cannot be made sense of as
// written -- an empty partition, overlapping regions, a region too small or too tight to
// pack -- and GenerateIOFloorPlanConstraints() stops the compile on them, as the panel
// refuses to save such a .qdc. Advisory findings (an instance synthesis deleted, a parent
// whose own logic no partition takes) are reported and the flow proceeds.
class FloorplanChecker {
public:
    // projectPath holds the flow's artifacts; qdcPath is the file the flow is
    // about to consume. Returns false when there is nothing to check (no .qdc, or no device
    // description), leaving `issues` empty -- that is not a failure.
    static bool check(const std::filesystem::path& projectPath,
                      // Names the flow's artifacts: they are "<projectName>_floorplanning_*",
                      // not bare files in projectPath.
                      const std::string& projectName,
                      const std::filesystem::path& qdcPath,
                      const std::filesystem::path& deviceLayoutFile,
                      DeviceGrid::Issues& issues,
                      std::string& error);
};

}  // namespace fp
