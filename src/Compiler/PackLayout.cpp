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
#include "PackLayout.h"

#include "FlowEngineClient.h"
#include "Utils/FileUtils.h"

namespace fs = std::filesystem;

namespace FOEDAG {

namespace {
constexpr int VERSION = 1;
}

nlohmann::json PackLayout::toJson() const {
  nlohmann::json files_json = nlohmann::json::object();
  for (const auto& [path, digests] : files) {
    files_json[path] = {{"plaintext", digests.plaintext}, {"written", digests.written}};
  }
  return {{"v", VERSION},
          {"mode", autoMode ? "auto" : "custom"},
          {"layout_name", layoutName},
          {"vpr_xml", vprXml.generic_u8string()},
          {"sb_maps", sbMaps.generic_u8string()},
          {"files", files_json}};
}

std::optional<PackLayout> PackLayout::fromJson(const nlohmann::json& json) try {
  if (!json.is_object() || json.value("v", 0) != VERSION) return std::nullopt;
  PackLayout layout;
  const std::string mode = json.at("mode").get<std::string>();
  if (mode != "auto" && mode != "custom") return std::nullopt;
  layout.autoMode = mode == "auto";
  layout.customMode = !layout.autoMode;
  layout.layoutName = json.at("layout_name").get<std::string>();
  layout.vprXml = fs::u8path(json.at("vpr_xml").get<std::string>());
  layout.sbMaps = fs::u8path(json.at("sb_maps").get<std::string>());
  if (layout.layoutName.empty() || layout.vprXml.empty() || layout.vprXml.is_absolute() ||
      layout.sbMaps.is_absolute()) {
    return std::nullopt;
  }
  for (const auto& [path, digests] : json.at("files").items()) {
    layout.files[path] = {digests.at("plaintext").get<std::string>(),
                          digests.at("written").get<std::string>()};
  }
  return layout;
} catch (const nlohmann::json::exception&) {
  return std::nullopt;
}

bool PackLayout::write(const fs::path& project) const {
  return FileUtils::WriteToFile(project / FILE_NAME, toJson().dump(2) + "\n");
}

std::optional<PackLayout> PackLayout::read(const fs::path& project) {
  const fs::path file = project / FILE_NAME;
  if (!FileUtils::FileExists(file)) return std::nullopt;
  return fromJson(nlohmann::json::parse(FileUtils::GetFileContent(file), nullptr, false));
}

std::map<std::string, std::string> PackLayout::outputHostDigests(const fs::path& project) const {
  std::map<std::string, std::string> result;
  for (const auto& [path, digests] : files) {
    if (!digests.written.empty() &&
        FlowEngineClient::sha256File(project / fs::u8path(path)) == digests.written) {
      result[path] = digests.plaintext;
    }
  }
  return result;
}

}  // namespace FOEDAG
