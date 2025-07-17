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


namespace FOEDAG {

class CommandWrapper {
public:
  const std::string& command() const { return m_command; }

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
    appendArgument(parameter + "_"  + file.string());
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

  bool isValid() const { return !m_command.empty(); }

private:
  std::vector<std::filesystem::path> m_files;
  std::string m_command;

  void appendArgument(const std::string& arg) {
    if (!m_command.empty() && m_command.back() != ' ') {
        m_command += ' ';
    }
    m_command += arg;
  }

  void prependArgument(const std::string& arg) {
    if (!m_command.empty() && m_command.front() != ' ') {
        m_command = arg + " " + m_command;
    } else {
        m_command = arg + m_command;
    }
  }
};

}  // namespace FOEDAG
