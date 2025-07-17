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

#include <string>
#include <vector>
#include <filesystem>

#include "Utils/StringUtils.h"


namespace FOEDAG {

class CommandWrapper {
public:
  const std::string& command() const { return m_command; }

  void appendRegularArg(const std::string& arg) {
    appendArgument(argument);
  }
  void appendFilePathArg(const std::filesystem::path& path) {
    m_filePathes.push_back(path);
    appendArgument(path.string());
  }
private:
  std::vector<std::filesystem::path> m_filePathes;
  std::string m_command;

  void appendArgument(const std::string& arg) {
    if (!StringUtils::endsWith(m_command, " ")) {
      m_command += " ";
    }
    m_command += arg;
  }
};

}  // namespace FOEDAG
