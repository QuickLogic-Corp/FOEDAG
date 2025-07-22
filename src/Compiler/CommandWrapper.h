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

#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"

#include <string>
#include <unordered_set>
#include <filesystem>
#include <optional>
#include <regex>
#include <memory>


namespace FOEDAG {

class FileIdentity {
public:
  FileIdentity()=default;
  FileIdentity(const std::filesystem::path& filePath, const std::string& role): m_filePath(filePath), m_role(role) {
    if (std::filesystem::exists(filePath)) {
      if (role.empty()) {
        m_modifiedDateTime = FileUtils::Mtime(filePath);
      } else {
        m_md5sum = FileUtils::calcHashFileContent(filePath);
      }
    }
  }

private:
  std::filesystem::path m_filePath;
  std::string m_role;
  std::string m_modifiedDateTime;
  std::string m_md5sum;
};

class CommandWrapper {
public:
  CommandWrapper()=default;
  
  CommandWrapper(const std::string& command) {
    append(command);
  }

  bool compareIgnoringTempPath(const std::string& rhs) {
      static std::regex tmpRegex(R"(\/tmp\/\S+)");
      std::string thisClean = std::regex_replace(string(), tmpRegex, "/tmp/PLACEHOLDER");
      std::string rhsClean = std::regex_replace(rhs, tmpRegex, "/tmp/PLACEHOLDER");
      return thisClean == rhsClean;
  }

  bool matches(const CommandWrapper& rhs, std::string& msg) {
    if (string() != rhs.string()) {
      // todo: fill message of diff
      return false;
    }
    return compareFiles(files(), rhs.files(), msg);
  }

  bool empty() const { return m_string.empty(); }

  const std::string& string() const { return m_string; }
  const std::map<std::string, FileIdentity> files() const { return m_files; }

  void append(const std::string& parameter, const std::string& value) {
    appendArgument(parameter + " " + value);
  }

  void append(const std::string& parameter) {
    appendArgument(parameter);
  }

  void appendFile(const std::string& filePath)=delete;
  void appendFile(const std::filesystem::path& file, const std::string& role = "") {
    handleFile(file, role);
    appendArgument(file.string());
  }
  // void watchFiles(const std::vector<std::filesystem::path>& files) {
  //   for (const std::filesystem::path& file: files) {
  //     watchFile(file);
  //   }
  // }
  // void watchFile(const std::filesystem::path& file) {
  //   m_watchFiles.push_back(file);
  //   // Note: We do not add this file directly to the command-line arguments.
  //   // It may be indirectly used within a script file, and the script file itself 
  //   // is already included as a command-line argument.    
  // }

  void appendFile(const std::string& parameter, const std::string& file)=delete;
  void appendFile(const std::string& parameter, const std::filesystem::path& file, const std::string& role = "") {
    handleFile(file, role);
    appendArgument(parameter + " "  + file.string());
  }

  void prepend(const std::string& parameter, const std::string& value) {
    prependArgument(parameter + " " + value);
  }
  
  void prepend(const std::string& parameter) {
    prependArgument(parameter);
  }

  void prependFile(const std::string& file)=delete;
  void prependFile(const std::filesystem::path& file, const std::string& role = "") {
    handleFile(file, role);
    prependArgument(file.string());
  }

  void prependFile(const std::string& parameter, const std::string& file)=delete;
  void prependFile(const std::string& parameter, const std::filesystem::path& file, const std::string& role) {
    handleFile(file, role);
    prependArgument(parameter + "_"  + file.string());
  }

  bool isValid() const { return !m_string.empty(); }

private:
  std::map<std::string, FileIdentity> m_files;
  //std::set<std::filesystem::path> m_watchFiles;
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

  void handleFile(const std::filesystem::path& file, const std::string& role) {
    std::string key = role.empty()? file.string(): role;
    m_files[key] = FileIdentity{file, role};
  }

  bool compareFiles(const std::map<std::string, FileIdentity>& filesMap1, const std::map<std::string, FileIdentity>& filesMap2, std::string& msg) {
    std::unordered_set<std::string> keyDiff = getSymmetricKeyDifference(filesMap1, filesMap2);
    if (!keyDiff.empty()) {
      msg = "changes in following files: " + StringUtils::toString(keyDiff);
      return false;
    }

    // for ()
  }

std::unordered_set<std::string> getSymmetricKeyDifference(const std::map<std::string, FileIdentity>& a, const std::map<std::string, FileIdentity>& b) 
{
  std::unordered_set<std::string> diff;

  for (const auto& pair : a) {
    if (b.find(pair.first) == b.end()) {
      diff.insert(pair.first);
    }
  }

  for (const auto& pair : b) {
    if (a.find(pair.first) == a.end()) {
      diff.insert(pair.first);
    }
  }

  return diff;
}

};
using CommandWrapperPtr = std::shared_ptr<CommandWrapper>;

}  // namespace FOEDAG
