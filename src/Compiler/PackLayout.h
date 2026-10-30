/*
Copyright 2026 QuickLogic team

GPL License

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "nlohmann_json/json.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>

namespace FOEDAG {

// The layout pack resolved on an AUTO/CUSTOM device, saved as pack_layout.json
// so a skipped pack can restore it (aurora2 docs/specs/aurora-flow-engine,
// row 1.10; REQ-121). Paths are project-relative.
struct PackLayout {
  static constexpr const char* FILE_NAME = "pack_layout.json";

  bool autoMode = false;
  bool customMode = false;
  std::string layoutName;
  std::filesystem::path vprXml;  // the generated architecture, encrypted
  std::filesystem::path sbMaps;  // the generated SB maps, maybe encrypted; may be empty
  struct Digests {
    std::string plaintext;  // "sha256:<hex>" of the content before encryption
    std::string written;    // "sha256:<hex>" of the file pack left on disk
  };
  std::map<std::string, Digests> files;  // by project-relative path

  nlohmann::json toJson() const;
  static std::optional<PackLayout> fromJson(const nlohmann::json& json);

  bool write(const std::filesystem::path& project) const;
  static std::optional<PackLayout> read(const std::filesystem::path& project);

  // Plaintext digests of the generated files still on disk as pack wrote them;
  // a file changed since is left for the engine to digest as it is.
  std::map<std::string, std::string> outputHostDigests(
      const std::filesystem::path& project) const;
};

}  // namespace FOEDAG
