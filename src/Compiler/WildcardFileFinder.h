/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

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

#include <set>
#include <map>
#include <filesystem>

namespace FOEDAG {

class WildcardFileFinder {
public:
  WildcardFileFinder(const std::filesystem::path& path, const std::string& patternFileName);

  std::map<std::string, std::string> profileToFileNameMap() { return m_profileToFileNameMap; }
  std::set<std::string> profiles() {
    std::set<std::string> keys;
    for (const auto& [key, _]: m_profileToFileNameMap) {
      keys.insert(key);
    }
    return keys;
  }

  std::filesystem::path baseFilePath() const { return m_path / m_baseFileName; }
  std::string baseFileName() const { return m_baseFileName; }
  bool hasProfiles() const { return !m_profileToFileNameMap.empty(); }
  bool isBaseFileNameOnlyAvailable() const { return !m_baseFileName.empty() && !hasProfiles(); }

  std::string defaultProfile() const;

private:
  std::filesystem::path m_path;
  std::string m_baseFileName;

  std::string m_firstProfile;

  std::map<std::string, std::string> m_profileToFileNameMap;
};

}  // namespace FOEDAG
