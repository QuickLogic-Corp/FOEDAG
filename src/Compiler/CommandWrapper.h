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

struct MParameter {
  std::string parameter;
  std::string value;
};
struct DiffParameter: public MParameter {
  DiffParameter(const std::string& param, const std::string& fromValue, const std::string& toValue):
  MParameter{param, toValue}, 
  prevValue(fromValue)
  {

  }
  std::string prevValue;
};

struct DiffFile {
  std::string file;
  std::string criteria;
  std::string prevValue;
  std::string value;
};

struct DiffCommand {
public:
  const std::vector<MParameter>& added() const { return m_added; }
  const std::vector<MParameter>& removed() const { return m_removed; }
  const std::vector<DiffParameter>& changed() const { return m_changed; }
  const std::vector<DiffFile>& files() const { return m_files; }

  bool empty() const { return m_added.empty() && m_removed.empty() && m_changed.empty() && m_files.empty(); }

  void addAdded(const std::string& param, const std::string& value) {
    m_added.emplace_back(MParameter{param, value});
  }
  void addRemoved(const std::string& param, const std::string& value) {
    m_removed.emplace_back(MParameter{param, value});
  }
  void addChanged(const std::string& param, const std::string& fromValue, const std::string& toValue) {
    m_changed.emplace_back(DiffParameter{param, fromValue, toValue});
  }
  void addFile(const std::string& file, const std::string& criteria, const std::string& oldValue, const std::string& newValue) {
    m_files.emplace_back(DiffFile{file, criteria, oldValue, newValue});
  }

  //messages.push_back("file role:" + m_role + "content hash changed from " + old.contentHash() + " to " + contentHash());
  //messages.push_back("filepath: " + m_filePath.string() + " datetime changed from " + old.modifiedDateTime() + " to " + m_modifiedDateTime);
  //messages.push_back("value for parameter [" + keyNew + "], changed from [" + valOld + "] to [" + valNew + "]");
  //messages.push_back("new parameter has been added [" + keyNew + "]=" + valNew);
  //messages.push_back("parameter [" + keyOld + "]=" + valOld + " was deleted");

private:
  std::vector<MParameter> m_added;
  std::vector<MParameter> m_removed;
  std::vector<DiffParameter> m_changed;
  std::vector<DiffFile> m_files;
};
using DiffCommandPtr = std::shared_ptr<DiffCommand>;

class FileIdentity {
public:
  FileIdentity()=default;
  FileIdentity(const std::filesystem::path& filePath, const std::string& role): m_filePath(filePath), m_role(role) {
    if (std::filesystem::exists(filePath)) {
      if (role.empty()) {
        m_modifiedDateTime = std::to_string(FileUtils::Mtime(filePath));
      } else {
        m_contentHash = FileUtils::calcHashFileContent(filePath);
      }
    }
  }

  const std::string& contentHash() const { return m_contentHash; }
  const std::string& modifiedDateTime() const { return m_modifiedDateTime; }

  bool compare(const FileIdentity& old, const DiffCommandPtr& diff) const {
    bool isDirty = false;
    if (m_role.empty()) {
      if (m_modifiedDateTime != old.modifiedDateTime()) {
        diff->addFile(m_filePath.string(), "datetime", old.modifiedDateTime(), m_modifiedDateTime);
        isDirty = true;
      }
    } else {
      // file role is used as a stable file id, where the filepath could be changed (for instance for vpt.xml)
      // for such files we cannot check file modification time, and needs to rely on the content hash, which is slower but robust.
      if (m_contentHash != old.contentHash()) {
        diff->addFile(m_role, "hash", old.contentHash(), contentHash());
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

  DiffCommandPtr compare(const CommandWrapper& old) {
    DiffCommandPtr diff = std::make_shared<DiffCommand>();
    compareArguments(old.arguments(), arguments(), diff);
    compareFiles(old.files(), files(), diff);
    return diff;
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

  void compareArguments(
    const std::unordered_map<std::string, std::string>& argsOld, 
    const std::unordered_map<std::string, std::string>& argsNew, 
    const DiffCommandPtr& diff) const 
  {
    for (const auto& [keyNew, valNew]: argsNew) {
      auto itOld = argsOld.find(keyNew);
      if (itOld != argsOld.end()) {
        // detect changed parameters
        const std::string& valOld = itOld->second;
        if (valNew != valOld) {
          diff->addChanged(keyNew, valOld, valNew);
        }
      } else {
        // detect added parameters
        diff->addAdded(keyNew, valNew);
      }
    }

    // detect deleted parameters
    for (const auto& [keyOld, valOld]: argsOld) {
      auto itNew = argsNew.find(keyOld);
      if (itNew == argsNew.end()) {
        diff->addRemoved(keyOld, valOld);
      }
    }
  }

  void compareFiles(
    const std::unordered_map<std::string, FileIdentity>& filesOld, 
    const std::unordered_map<std::string, FileIdentity>& filesNew, 
    const DiffCommandPtr& diff) 
  {
    for (const auto& [keyNew, fileIdentityNew]: filesNew) {
      auto itOld = filesOld.find(keyNew);
      if (itOld != filesOld.end()) {
        const FileIdentity& fileIdentityOld = itOld->second;
        fileIdentityNew.compare(fileIdentityOld, diff);
      }
    }
  }

};
using CommandWrapperPtr = std::shared_ptr<CommandWrapper>;

}  // namespace FOEDAG
