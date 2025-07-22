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
        m_contentHash = FileUtils::calcHashFileContent(filePath);
      }
    }
  }

  const std::string& contentHash() const { return m_contentHash; }
  const std::string& modifiedDateTime() const { return m_modifiedDateTime; }

  bool compareWithOld(const FileIdentity& old, std::vector<std::string>& messages) const {
    bool isDirty = false;
    if (m_role.empty()) {
      if (m_modifiedDateTime != old.modifiedDateTime()) {
        messages.push_back("filepath: " + m_filePath.string() + " datetime changed from " + old.modifiedDateTime() + " to " + m_modifiedDateTime);
        isDirty = true;
      }
    } else {
      // file role is used as a stable file id, where the filepath could be changed (for instance for vpt.xml)
      // for such files we cannot check file modification time, and needs to rely on the content hash, which is slower but robust.
      if (m_contentHash != old.contentHash()) {
        messages.push_back("file role:" + m_role + "content hash changed from " + old.contentHash() + " to " + contentHash());
        isDirty = true;
      }
    }

    return !isDirty;
  }

private:
  std::filesystem::path m_filePath;
  std::string m_role;
  std::string m_modifiedDateTime;
  std::string m_contentHash;
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

  bool compareWithOld(const CommandWrapper& old, std::vector<std::string>& messages) {
    if (!areArgumentsMatches(old.arguments(), arguments(), messages)) {
      return false;
    }
    if (!areFileContentsMatches(old.files(), files(), messages)) {
      return false;
    }
    return true;
  }

  bool empty() const { return m_string.empty(); }

  const std::string& string() const { return m_string; }
  const std::unordered_map<std::string, FileIdentity>& files() const { return m_files; }
  const std::unordered_map<std::string, std::string>& arguments() const { return m_arguments; }

  void append(const std::string& parameter, const std::string& value = "") {
    appendArgument(parameter, value);
  }

  void appendFile(const std::string& filePath)=delete;
  void appendFile(const std::filesystem::path& file, const std::string& role = "") {
    appendArgument(file.string(), "");
    handleFile(file, role);
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
    appendArgument(parameter, file.string());
    handleFile(file, role);
  }

  void prepend(const std::string& parameter, const std::string& value = "") {
    prependArgument(parameter, value);
  }
  
  void prependFile(const std::string& file)=delete;
  void prependFile(const std::filesystem::path& file, const std::string& role = "") {
    prependArgument(file.string(), "");
    handleFile(file, role);
  }

  void prependFile(const std::string& parameter, const std::string& file)=delete;
  void prependFile(const std::string& parameter, const std::filesystem::path& file, const std::string& role) {
    prependArgument(parameter, file.string());
    handleFile(file, role);
  }

  //bool isValid() const { return !m_string.empty(); }

private:
  std::unordered_map<std::string, std::string> m_arguments;
  std::unordered_map<std::string, FileIdentity> m_files;
  //std::set<std::filesystem::path> m_watchFiles;
  std::string m_string;

  void appendArgument(const std::string& param, const std::string& val) {
    handleArgument(param, val);
    if (!m_string.empty() && m_string.back() != ' ') {
        m_string += ' ';
    }
    m_string += param;
    if (!val.empty()) {
      m_string += ' ' + val;
    }
  }

  void prependArgument(const std::string& param, const std::string& val) {
    handleArgument(param, val);
    std::string arg{param};
    if (!val.empty()) {
      arg += ' ' + val;
    }
    if (!m_string.empty() && m_string.front() != ' ') {
        m_string = arg + ' ' + m_string;
    } else {
        m_string = arg + m_string;
    }
  }

  void handleArgument(const std::string& arg, const std::string& val) {
    m_arguments[arg] = val;
  }

  void handleFile(const std::filesystem::path& file, const std::string& role) {
    std::string key = role.empty()? file.string(): role;
    m_files[key] = FileIdentity{file, role};
  }

  bool areArgumentsMatches(
    const std::unordered_map<std::string, std::string>& argsOld, 
    const std::unordered_map<std::string, std::string>& argsNew, 
    std::vector<std::string>& messages) const 
  {
    bool isDirty = false;

    for (const auto& [keyNew, valNew]: argsNew) {
      auto itOld = argsOld.find(keyNew);
      if (itOld != argsOld.end()) {
        const std::string& valOld = itOld->second;
        if (valNew != valOld) {
          messages.push_back("value for parameter [" + keyNew + "], changed from [" + valOld + "] to [" + valNew + "]");
          isDirty = true;
        }
      }
    }

    return !isDirty;
  }

  bool areFileContentsMatches(
    const std::unordered_map<std::string, FileIdentity>& filesOld, 
    const std::unordered_map<std::string, FileIdentity>& filesNew, 
    std::vector<std::string>& messages) 
  {
    bool isDirty = false;

    for (const auto& [keyNew, fileIdentityNew]: filesNew) {
      auto itOld = filesOld.find(keyNew);
      if (itOld != filesOld.end()) {
        const FileIdentity& fileIdentityOld = itOld->second;
        if (!fileIdentityNew.compareWithOld(fileIdentityOld, messages)) {
          isDirty = true;
        }
      }
    }
    return !isDirty;
  }

};
using CommandWrapperPtr = std::shared_ptr<CommandWrapper>;

}  // namespace FOEDAG
