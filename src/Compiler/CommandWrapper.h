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
#include <regex>


namespace FOEDAG {

class CommandWrapper {
public:
  bool compareIgnoringTempPath(const std::string& rhs) {
      static std::regex tmpRegex(R"(\/tmp\/\S+)");
      std::string thisClean = std::regex_replace(string(), tmpRegex, "/tmp/PLACEHOLDER");
      std::string rhsClean = std::regex_replace(rhs, tmpRegex, "/tmp/PLACEHOLDER");
      return thisClean == rhsClean;
  }

  const std::string& string() const { return m_string; }

  void append(const std::string& parameter, const std::string& value) {
    appendArgument(parameter + " " + value);
  }

  void append(const std::string& parameter) {
    appendArgument(parameter);
  }

  void appendFile(const std::string& file)=delete;
  void appendFile(const std::filesystem::path& file) {
    m_files.push_back(file);
    appendArgument(file.string());
  }

  void appendFile(const std::string& parameter, const std::string& file)=delete;
  void appendFile(const std::string& parameter, const std::filesystem::path& file) {
    m_files.push_back(file);
    appendArgument(parameter + " "  + file.string());
  }

  void prepend(const std::string& parameter, const std::string& value) {
    prependArgument(parameter + " " + value);
  }
  
  void prepend(const std::string& parameter) {
    prependArgument(parameter);
  }

  void prependFile(const std::string& file)=delete;
  void prependFile(const std::filesystem::path& file) {
    m_files.push_back(file);
    prependArgument(file.string());
  }

  void prependFile(const std::string& parameter, const std::string& file)=delete;
  void prependFile(const std::string& parameter, const std::filesystem::path& file) {
    m_files.push_back(file);
    prependArgument(parameter + "_"  + file.string());
  }

  bool isValid() const { return !m_string.empty(); }

private:
  std::vector<std::filesystem::path> m_files;
  std::string m_string;

  void appendArgument(const std::string& arg) {
    if (!m_string.empty() && m_string.back() != ' ') {
        m_string += ' ';
    }
    m_string += arg;
  }

  void prependArgument(const std::string& arg) {
    if (!m_string.empty() && m_string.front() != ' ') {
        m_string = arg + " " + m_string;
    } else {
        m_string = arg + m_string;
    }
  }
};

}  // namespace FOEDAG
