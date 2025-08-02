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

#include "nlohmann_json/json.hpp"
#include "Utils/SerializationUtils.h"
#include "Utils/FileUtils.h"
#include "Utils/StringUtils.h"


#include <string>
#include <unordered_set>
#include <map>
#include <filesystem>
#include <optional>
#include <regex>
#include <memory>

// #define DEBUG_ENABLE_PARANOIC_CHECK

namespace FOEDAG {

constexpr const char* FILES_INFO_PLACEHOLDER = "${FILES_INFO}";

class ScriptRenderer {
  static std::filesystem::path s_projectPath;

public:
ScriptRenderer()=default;
  ScriptRenderer(const std::string& scriptTemplate)
  : m_scriptTemplate(scriptTemplate)
  {}

  static void setProjectPath(const std::filesystem::path& path) { s_projectPath = path; }

  void apply(const std::string& placeholder, const std::string& name) {
    m_parameters[placeholder] = name;
  }

  void applyFile(const std::string& placeholder, const std::filesystem::path& filePath, const std::string& mask = "") {
    m_filePathes.push_back(filePath);
    m_parameters[placeholder] = filePath.string();
    if (!mask.empty()) {
      m_maskedFiles[filePath.string()] = mask;
    }
  }

  void addFile(const std::filesystem::path& filePath) {
    m_filePathes.push_back(filePath);
  }

  bool hasErrors() const { return !m_errors.empty(); }

  std::vector<std::string> takeErrors() {
    std::vector<std::string> result;
    std::swap(m_errors, result);
    return result;
  }

  std::string render() {
    std::string script{m_scriptTemplate};

    for (const auto& [placeholder, value]: m_parameters) {
      checkPlaceholder(script, placeholder);
      std::string resolvedValue{value};
      if (m_isMaskingEnabled) {
        auto it = m_maskedFiles.find(value);
        if (it != m_maskedFiles.end()) {
          resolvedValue = it->second;
        }
      }

      script = StringUtils::replaceAll(script, placeholder, resolvedValue);
    }

    // Insert the required FILES_INFO_PLACEHOLDER if it is missing from the template.
    // Without it, incremental compilation may report an incorrect status.
    if (script.find(FILES_INFO_PLACEHOLDER) == std::string::npos) {
      script += "\n\n\n";
      script += "###################################################################";
      script += "# AUTO-GENERATED - DO NOT EDIT";
      script += "# Contains <relative-path> <MD5> pairs used to detect stale files.";
      script += "###################################################################";
      script += FILES_INFO_PLACEHOLDER;
      script += "###################################################################";
    }
    
    script = StringUtils::replaceAll(script, FILES_INFO_PLACEHOLDER, collectFileInfosStr(m_isMaskingEnabled));

    if (script.find("${") != std::string::npos) {
      m_errors.push_back("script is not fully parameterized, still contains pattern ${}");
    }
    return script;
  }

  std::string calcHash() {
    m_isMaskingEnabled = true;
    std::string script = render();
    m_isMaskingEnabled = false;
    return FileUtils::calcHash(script);
  }

private:
  std::string m_scriptTemplate;
  std::vector<std::filesystem::path> m_filePathes;
  std::unordered_map<std::string, std::string> m_parameters;
  std::unordered_map<std::string, std::string> m_maskedFiles; // special case to mask variation of file
  std::string m_commentStr = "#";

  bool m_isMaskingEnabled = false;

  std::vector<std::string> m_errors;

  std::string collectFileInfosStr(bool applyMask) {
    std::string result;
    for (const std::filesystem::path filePath: m_filePathes) {
      std::string name{filePath.string()};
      auto it = m_maskedFiles.find(name);
      if (applyMask) {
        if (it != m_maskedFiles.end()) {
          name = it->second; // use mask instead of path
        }
      }
      std::filesystem::path resolvedFilePath(filePath);
      bool exists = std::filesystem::exists(resolvedFilePath);
      if (!exists) {
        if (filePath.is_relative() && !s_projectPath.empty()) {
          resolvedFilePath = s_projectPath / filePath;
          exists = std::filesystem::exists(resolvedFilePath);
        }  
      }
      if (exists) {
        result += m_commentStr + " " + name + " " + FileUtils::calcFileContentHash(resolvedFilePath) + "\n";
      } else {
#ifdef DEBUG_ENABLE_PARANOIC_CHECK
        m_errors.push_back(resolvedFilePath.string() + " doesn't exist, cannot calc hash for it");
#endif // DEBUG_ENABLE_PARANOIC_CHECK
      }
    }

    return StringUtils::rtrim(result);
  }

  void checkPlaceholder(const std::string& script, const std::string& placeholder) {
#ifdef DEBUG_ENABLE_PARANOIC_CHECK
    if (script.find(placeholder) == std::string::npos) {
      m_errors.push_back("script doesn't contain required placeholder:" + placeholder);
    }
#endif // DEBUG_ENABLE_PARANOIC_CHECK
  }
};
using ScriptRendererPtr = std::shared_ptr<ScriptRenderer>;

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

  bool isEmpty() const { 
    return m_genericMsg.empty() &&
    m_addedParameters.empty() &&
    m_removedParameters.empty() &&
    m_changedParameters.empty() &&
    m_diffFiles.empty(); 
  }

  void addGenericMsg(const std::string& msg) { m_genericMsg.push_back(msg); }

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
    for (const std::string& msg: m_genericMsg) {
      messages.push_back(msg);
    }
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
      messages.push_back("filepath [" + file.file + "], " + file.criteria + " changed from [" + file.prevValue + "] to [" + file.value + "]");
    }

    return messages;
  }

private:
  std::vector<std::string> m_genericMsg;
  std::vector<MParameter> m_addedParameters;
  std::vector<MParameter> m_removedParameters;
  std::vector<DiffParameter> m_changedParameters;
  std::vector<DiffFile> m_diffFiles;
};
using DiffCommandPtr = std::shared_ptr<DiffCommand>;

class FileIdentity {
public:
  FileIdentity()=default;
  FileIdentity(const std::filesystem::path& filePath, const std::string& mask, bool skipHashCheck)
  : m_filePath(filePath), m_mask(mask) {
    if (skipHashCheck) {
      m_modifiedDateTime = FileUtils::ModifiedTimeStr(filePath);
    } else {
      m_contentHash = FileUtils::calcFileContentHash(filePath);
    }
  }

  const std::filesystem::path& filePath() const { return m_filePath; }
  const std::string& mask() const { return m_mask; }

  const std::string& contentHash() const { return m_contentHash; }
  const std::string& modifiedDateTime() const { return m_modifiedDateTime; }

  bool compare(const FileIdentity& old, const DiffCommandPtr& diff) const {
    bool isDirty = false;
    
    bool hasHash = (!m_contentHash.empty() && !old.contentHash().empty());
    bool hasModDateTime = (!m_modifiedDateTime.empty() && !old.modifiedDateTime().empty());

    if (hasModDateTime) {
      if (m_modifiedDateTime != old.modifiedDateTime()) {
        diff->addDiffFile(m_mask.empty()? m_filePath.string(): m_mask, "datetime", old.modifiedDateTime(), m_modifiedDateTime);
        isDirty = true;
      }
    } 

    if (hasHash) {
      if (m_contentHash != old.contentHash()) {
        diff->addDiffFile(m_mask.empty()? m_filePath.string(): m_mask, "hash", old.contentHash(), contentHash());
        isDirty = true;
      }
    }

    if (!hasModDateTime && !hasHash) {
      diff->addDiffFile(m_mask.empty()? m_filePath.string(): m_mask, "no hash and no moddatetime", "", "");
      isDirty = true;
    }

    return !isDirty;
  }

private:
  friend void to_json(nlohmann::json& j, const FileIdentity& obj) {
    j = nlohmann::json{
      {"file_path", obj.m_filePath.string()},
      {"mask", obj.m_mask},
      {"modified_datetime", obj.m_modifiedDateTime},
      {"content_hash", obj.m_contentHash}
    };
  }

  friend void from_json(const nlohmann::json& j, FileIdentity& obj) {
    std::string pathStr;
    j.at("file_path").get_to(pathStr);
    obj.m_filePath = std::filesystem::path{pathStr};

    j.at("mask").get_to(obj.m_mask);
    j.at("modified_datetime").get_to(obj.m_modifiedDateTime);
    j.at("content_hash").get_to(obj.m_contentHash);
  }

  std::filesystem::path m_filePath;
  std::string m_mask;
  std::string m_modifiedDateTime;
  std::string m_contentHash;
};

class CommandWrapper {
  static std::filesystem::path s_projectPath;
  static std::unordered_set<std::string> s_bigFilesSet;

public:
  CommandWrapper()=default;
  
  CommandWrapper(const std::string& command) {
    append(command);
  }

  static void setProjectPath(const std::filesystem::path& path) { s_projectPath = path; }
  static const std::filesystem::path& projectPath() { return s_projectPath; }
  static void addBigFileName(const std::string& fileName) { s_bigFilesSet.insert(fileName); }

  DiffCommandPtr collectDiff(const CommandWrapper& old) {
    DiffCommandPtr diff = std::make_shared<DiffCommand>();
    compareArguments(old.arguments(), arguments(), diff);
    compareFiles(old.files(), files(), diff);
    if (old.scriptHash() != scriptHash()) {
      diff->addGenericMsg("hashes of scripts are different");
    }
    return diff;
  }

  bool isEmpty() const { return m_string.empty(); }

  const std::string& string() const { return m_string; }
  const std::unordered_map<std::string, FileIdentity>& files() const { return m_files; }
  const std::unordered_map<std::string, std::string>& arguments() const { return m_arguments; }

  void setScriptHash(const std::string& scriptHash) { m_scriptHash = scriptHash; }

  const std::string& scriptHash() const { return m_scriptHash; }

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
  std::unordered_map<std::string, std::string> m_arguments;
  std::unordered_map<std::string, FileIdentity> m_files;
  std::string m_scriptHash;
  std::string m_string;

  friend void to_json(nlohmann::json& j, const CommandWrapper& obj) {
    j = nlohmann::json{
      {"arguments", obj.m_arguments},
      {"files", obj.m_files},
      {"string", obj.m_string},
      {"script_hash", obj.m_scriptHash}
    };
  }

  friend void from_json(const nlohmann::json& j, CommandWrapper& obj) {
    j.at("arguments").get_to(obj.m_arguments);
    j.at("files").get_to(obj.m_files);
    j.at("string").get_to(obj.m_string);
    j.at("script_hash").get_to(obj.m_scriptHash);
  }

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
    std::filesystem::path resolvedFilePath(file);
    bool exists = std::filesystem::exists(file);
    if (!exists) {
      if (file.is_relative() && !s_projectPath.empty()) {
        resolvedFilePath = s_projectPath / file;
        exists = std::filesystem::exists(resolvedFilePath);
      }  
    }

    if (!exists) {
      return;
    }

    bool skipHashCheck = false;
    auto it = s_bigFilesSet.find(resolvedFilePath.filename());
    if (it != s_bigFilesSet.end()) {
      skipHashCheck = true;
    }
    if (resolvedFilePath.extension() == ".bin") {
      skipHashCheck = true; // calc md5 sum for big bin files takes a lot of time
    }

    std::string key = mask.empty()? resolvedFilePath.string(): mask;
    m_files[key] = FileIdentity{resolvedFilePath, mask, skipHashCheck};
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

}  // namespace FOEDAG
