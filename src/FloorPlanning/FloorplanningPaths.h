#pragma once

#include <filesystem>
#include <string>
#include <system_error>

namespace fp {

// [aurora2#1725] Where the floorplanning flow's artifacts live, in one place.
//
// The convention is "<project>_floorplanning_<suffix>", so a project directory shows at a
// glance which files this feature owns. It lives here, rather than only on the compiler,
// because three separate readers need it: the flow stages, the FloorPlanning panel
// (MainWindow), and the batch checker. When the writers were renamed to the convention the
// panel kept opening the old bare names -- present only as leftovers from a pre-rename run,
// so the panel silently showed a stale tree or none at all.

inline std::string floorplanningPrefix(const std::string& projectName) {
    return projectName + "_floorplanning";
}

inline std::filesystem::path floorplanningArtifact(const std::filesystem::path& projectPath,
                                                  const std::string& projectName,
                                                  const std::string& suffix) {
    return projectPath / (floorplanningPrefix(projectName) + "_" + suffix);
}

// atomsets.json is the one artifact whose name the flow does not choose. It is a positional
// argument inside the device's aurora_template_script.ys --
//
//     ${CALL_TCL_ATOMSETS_SCRIPT} atomsets.json --blif ${OUTPUT_BLIF}
//
// -- written by yosys into its cwd, the project directory. A template still passing the bare
// name produces "<project>/atomsets.json", not the convention, so prefer the convention and
// fall back to the bare file: the panel and the batch stages then always read whichever one
// the device actually produced. A template can adopt the convention with no code change
// here -- "${FLOORPLANNING_PREFIX}_atomsets.json" is enough -- and once every template does,
// this fallback can go.
inline std::filesystem::path floorplanningArtifactOrBare(const std::filesystem::path& projectPath,
                                                        const std::string& projectName,
                                                        const std::string& suffix) {
    std::error_code ec;
    const std::filesystem::path preferred =
        floorplanningArtifact(projectPath, projectName, suffix);
    if (std::filesystem::exists(preferred, ec)) {
        return preferred;
    }
    const std::filesystem::path bare = projectPath / suffix;
    if (std::filesystem::exists(bare, ec)) {
        return bare;
    }
    // Neither exists: name the one we want, so a "missing file" message quotes the convention.
    return preferred;
}

}  // namespace fp
