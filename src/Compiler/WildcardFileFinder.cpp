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

#include "WildcardFileFinder.h"

#include "Utils/StringUtils.h"
#include "Utils/FileUtils.h"

#include <QDebug>

namespace FOEDAG {

WildcardFileFinder::WildcardFileFinder(const std::filesystem::path& path, const std::string& patternFileName)
:
  m_path(path)
{
  std::string baseFileName = StringUtils::replaceAll(patternFileName, "*", ""); 
  qInfo() << "~~~ baseFileName" << baseFileName.c_str();
  
  std::vector<std::string> fileNames = FileUtils::findFileNamesByWildcard(path, patternFileName);
  for (const std::string& fileName: fileNames) {
    std::string profile;
    if (fileName != baseFileName) {
      profile = StringUtils::extractWildcardSegment(fileName, patternFileName);
      StringUtils::removePrefix(profile, "_");
      m_profileToFileNameMap[profile] = fileName;
    } else {
      m_baseFileName = baseFileName;
    }
  }
}

}  // namespace FOEDAG
