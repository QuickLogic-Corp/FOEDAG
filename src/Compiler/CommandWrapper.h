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

#include <nlohmann_json/json.hpp>

#include <string>
#include <unordered_set>
#include <map>
#include <filesystem>
#include <optional>
#include <regex>
#include <memory>

namespace FOEDAG {

constexpr const char* VPR_ARCH_FILE_MASK = "vpr_arch_file_mask";

struct MParameter {
  std::string name;
  std::string value;
};
struct DiffParameter: public MParameter {
  DiffParameter(const std::string& name, const std::string& fromValue, const std::string& toValue):
  MParameter{name, toValue}, 
  prevValue(fromValue) {

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
  const std::vector<MParameter>& addedParameters() const { return m_addedParameters; }
  const std::vector<MParameter>& removedParameters() const { return m_removedParameters; }
  const std::vector<DiffParameter>& changedParameters() const { return m_changedParameters; }
  const std::vector<DiffFile>& diffFiles() const { return m_diffFiles; }

  bool empty() const { 
    return \
    m_addedParameters.empty() 
  && m_removedParameters.empty() 
  && m_changedParameters.empty() 
  && m_diffFiles.empty(); 
  }

  void addAddedParameter(const std::string& param, const std::string& value) {
    m_addedParameters.emplace_back(MParameter{param, value});
  }
  void addRemovedParameter(const std::string& param, const std::string& value) {
    m_removedParameters.emplace_back(MParameter{param, value});
  }
  void addChangedParameter(const std::string& param, const std::string& fromValue, const std::string& toValue) {
    m_changedParameters.emplace_back(DiffParameter{param, fromValue, toValue});
  }
  void addDiffFile(const std::string& file, const std::string& criteria, const std::string& prevValue, const std::string& newValue) {
    m_diffFiles.emplace_back(DiffFile{file, criteria, prevValue, newValue});
  }

  std::vector<std::string> messages() const {
    std::vector<std::string> messages;
    for (const MParameter& param: m_addedParameters) {
      if (param.value.empty()) {
        messages.push_back("new parameter has been added [" + param.name + "]");
      } else {
        messages.push_back("new parameter has been added [" + param.name + "]=" + param.value);
      }
    }
    for (const MParameter& param: m_removedParameters) {
      if (param.value.empty()) {
        messages.push_back("parameter [" + param.name + "] was deleted");
      } else {
        messages.push_back("parameter [" + param.name + "]=" + param.value + " was deleted");
      }
    }
    for (const DiffParameter& param: m_changedParameters) {
      messages.push_back("value for parameter [" + param.name + "], changed from [" + param.prevValue + "] to [" + param.value + "]");
    }
    for (const DiffFile& file: m_diffFiles) {
      messages.push_back("filepath: " + file.file + " " + file.criteria + " changed from " + file.prevValue + " to " + file.value);
    }

    return messages;
  }

private:
  std::vector<MParameter> m_addedParameters;
  std::vector<MParameter> m_removedParameters;
  std::vector<DiffParameter> m_changedParameters;
  std::vector<DiffFile> m_diffFiles;
};
using DiffCommandPtr = std::shared_ptr<DiffCommand>;

class FileIdentity {
public:
  FileIdentity()=default;
  FileIdentity(const std::filesystem::path& filePath, const std::string& mask): m_filePath(filePath), m_mask(mask) {
    if (std::filesystem::exists(filePath)) {
      if (mask.empty()) {
        m_modifiedDateTime = std::to_string(FileUtils::Mtime(filePath));
      } else {
        // when filepath masked we rely on context hash
        m_contentHash = FileUtils::calcHashFileContent(filePath);
      }
    }
  }

  const std::filesystem::path& filePath() const { return m_filePath; }
  const std::string& mask() const { return m_mask; }

  const std::string& contentHash() const { return m_contentHash; }
  const std::string& modifiedDateTime() const { return m_modifiedDateTime; }

  bool compare(const FileIdentity& old, const DiffCommandPtr& diff) const {
    bool isDirty = false;
    if (m_mask.empty()) {
      if (m_modifiedDateTime != old.modifiedDateTime()) {
        diff->addDiffFile(m_filePath.string(), "datetime", old.modifiedDateTime(), m_modifiedDateTime);
        isDirty = true;
      }
    } else {
      // file mask is used as a stable file id, where the filepath could be changed (for instance for vpt.xml)
      // for such files we cannot check file modification time, and needs to rely on the content hash, which is slower but robust.
      if (m_contentHash != old.contentHash()) {
        diff->addDiffFile(m_mask, "hash", old.contentHash(), contentHash());
        isDirty = true;
      }
    }

    return !isDirty;
  }

private:
  friend void to_json(nlohmann::json& json, const FileIdentity& obj) {
    json = nlohmann::json{
      {"file_path", obj.m_filePath.string()},
      {"mask", obj.m_mask},
      {"modified_datetime", obj.m_modifiedDateTime},
      {"content_hash", obj.m_contentHash}
    };
  }

  friend void from_json(const nlohmann::json& json, FileIdentity& obj) {
    std::string pathStr;
    json.at("file_path").get_to(pathStr);
    obj.m_filePath = std::filesystem::path{pathStr};

    json.at("mask").get_to(obj.m_mask);
    json.at("modified_datetime").get_to(obj.m_modifiedDateTime);
    json.at("content_hash").get_to(obj.m_contentHash);
  }

  std::filesystem::path m_filePath;
  std::string m_mask;
  std::string m_modifiedDateTime;
  std::string m_contentHash;
};

class CommandWrapper {
public:
  CommandWrapper()=default;
  
  CommandWrapper(const std::string& command) {
    append(command);
  }

  // tmp function fused while migration
  bool compareIgnoringTempPath(const std::string& rhs) {
      static std::regex tmpRegex(R"(\/tmp\/\S+)");
      std::string thisClean = std::regex_replace(string(), tmpRegex, "/tmp/PLACEHOLDER");
      std::string rhsClean = std::regex_replace(rhs, tmpRegex, "/tmp/PLACEHOLDER");
      return thisClean == rhsClean;
  }
  //

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
  void appendFile(const std::filesystem::path& file, const std::string& mask = "") {
    appendArgument(file.string(), "", mask);
    handleFile(file, mask);
  }
  void appendFile(const std::string& parameter, const std::string& file)=delete;
  void appendFile(const std::string& parameter, const std::filesystem::path& file, const std::string& mask = "") {
    appendArgument(parameter, file.string(), mask);
    handleFile(file, mask);
  }

  void prepend(const std::string& parameter, const std::string& value = "") {
    prependArgument(parameter, value);
  }
  
  void prependFile(const std::string& file)=delete;
  void prependFile(const std::filesystem::path& file, const std::string& mask = "") {
    prependArgument(file.string(), "", mask);
    handleFile(file, mask);
  }

  void prependFile(const std::string& parameter, const std::string& file)=delete;
  void prependFile(const std::string& parameter, const std::filesystem::path& file, const std::string& mask) {
    prependArgument(parameter, file.string(), mask);
    handleFile(file, mask);
  }

private:
  friend void to_json(nlohmann::json& json, const CommandWrapper& obj) {
    json = nlohmann::json{
      {"arguments", obj.m_arguments},
      {"files", obj.m_files},
      {"string", obj.m_string}
    };
  }

  friend void from_json(const nlohmann::json& json, CommandWrapper& obj) {
    json.at("arguments").get_to(obj.m_arguments);
    json.at("files").get_to(obj.m_files);
    json.at("string").get_to(obj.m_string);
  }

  std::unordered_map<std::string, std::string> m_arguments;
  std::unordered_map<std::string, FileIdentity> m_files;
  std::string m_string;

  void appendArgument(const std::string& param, const std::string& val, const std::string& mask = "") {
    handleArgument(param, val, mask);
    if (!m_string.empty() && m_string.back() != ' ') {
      m_string += ' ';
    }
    m_string += param;
    if (!val.empty()) {
      m_string += ' ' + val;
    }
  }

  void prependArgument(const std::string& param, const std::string& val, const std::string& mask = "") {
    handleArgument(param, val, mask);
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

  void handleArgument(const std::string& param, const std::string& val, const std::string& mask) {
    if (mask.empty()) {
      m_arguments[param] = val;
    } else {
      if (val.empty()) {
        //then we mask param 
        m_arguments[mask] = val;
      } else {
        // we mask value of param
        m_arguments[param] = mask;
      }
    }
  }

  void handleFile(const std::filesystem::path& file, const std::string& mask) {
    std::string key = mask.empty()? file.string(): mask;
    m_files[key] = FileIdentity{file, mask};
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
          diff->addChangedParameter(keyNew, valOld, valNew);
        }
      } else {
        // detect added parameters
        diff->addAddedParameter(keyNew, valNew);
      }
    }

    // detect deleted parameters
    for (const auto& [keyOld, valOld]: argsOld) {
      auto itNew = argsNew.find(keyOld);
      if (itNew == argsNew.end()) {
        diff->addRemovedParameter(keyOld, valOld);
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

class CommandWrapperBuilder {
  public:
    static CommandWrapperPtr fromString(const std::string& content, const std::map<std::filesystem::path, std::string>& maskedFiles = {}) {
      CommandWrapperPtr command = std::make_shared<CommandWrapper>();
      std::vector<std::string> tokens = tokenize(content);
      for (std::size_t i=0; i<tokens.size(); ++i) {
        const std::string& token = tokens[i];

        // special case when arch file is masked by permanent id VPR_ARCH_FILE_MASK
        if (StringUtils::endsWith(token, "vpr")) {
          command->append(token);
          ++i;
          if (i<tokens.size()) {
            const std::string& arch_file = tokens[i];
            command->appendFile(std::filesystem::path(arch_file), VPR_ARCH_FILE_MASK);
          } else {
            command->append(token);
          }
          continue;
        }
        // end special case

        if (StringUtils::startsWith(token, "-") || StringUtils::startsWith(token, "--")) {
          const std::string& name = token;
          ++i;
          if (i<tokens.size()) {
            const std::string& val = tokens[i];
            if (FileUtils::IsExistedRegularFile(std::filesystem::path(val))) {
              std::string mask = tryExtractMask(std::filesystem::path(val), maskedFiles);
              if (mask.empty()) {
                command->appendFile(name, std::filesystem::path(val));
              } else {
                command->appendFile(name, std::filesystem::path(val), mask);
              }
            } else {
              command->append(name, val);
            }
          } else {
            command->append(name);
          }
        } else {
          if (FileUtils::IsExistedRegularFile(std::filesystem::path(token))) {
            std::string mask = tryExtractMask(std::filesystem::path(token), maskedFiles);
            if (mask.empty()) {
              command->appendFile(std::filesystem::path(token));
            } else {
              command->appendFile(std::filesystem::path(token), mask);
            }
          } else {
            command->append(token);
          }
        }
      }

      return command;
    }

  private:
    static std::vector<std::string> tokenize(const std::string& line) {
      std::istringstream iss(line);
      std::vector<std::string> tokens;
      std::string word;
      while (iss >> word) {
          tokens.push_back(word);
      }
      return tokens;
    }

    static std::string tryExtractMask(const std::filesystem::path& candidate, const std::map<std::filesystem::path, std::string>& maskedFiles) {
      auto it = maskedFiles.find(candidate);
      if (it != maskedFiles.end()) {
        return it->second;
      }
      return "";
    }
};

}  // namespace FOEDAG
