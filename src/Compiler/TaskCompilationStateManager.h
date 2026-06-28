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

#include <Compiler/Compiler.h>

#include "nlohmann_json/json.hpp"
#include "Utils/SerializationUtils.h"

#include "CommandWrapper.h"
#include "Utils/FileUtils.h"

#include <unordered_map>
#include <iostream>
#include <cstdlib>

namespace FOEDAG {

constexpr const char* COMPILATION_CACHE_FILENAME = "compilation_cache.json";
class TaskCompilationStateManager {
public:
  TaskCompilationStateManager(Compiler* compiler): m_compiler(compiler) {
    // Read the developer log toggle once and propagate it to CommandWrapper, so
    // verbose logging can be enabled at runtime via the environment variable
    // without recompiling the app.
    static const bool logEnabled =
        std::getenv("AURORA_INCR_COMPILATOR_LOG") != nullptr;
    CommandWrapper::setLogEnabled(logEnabled);
  }

  bool isCompilationRequired(int taskId, const CommandWrapperPtr& command) const {
    return isCompilationRequired(key(taskId), command);
  }

  bool isCompilationRequired(int taskId, const std::string& profile, const CommandWrapperPtr& command) const {
    return isCompilationRequired(key(taskId, profile), command);
  }

  void storeTaskCommand(int taskId, const CommandWrapperPtr& command) {
    command->actualize(); // to take into account date time and hash changes after task execution
    m_taskCommandsMap[key(taskId)] = command;
    save();
  }

  void storeTaskCommand(int taskId, const std::string& profile, const CommandWrapperPtr& command) {
    command->actualize(); // to take into account date time and hash changes after task execution
    m_taskCommandsMap[key(taskId, profile)] = command;
    save();
  }
  
  bool isEmpty() const { return m_taskCommandsMap.empty(); }

  void clear() {
    m_taskCommandsMap.clear();
    if (!FileUtils::removeFile(m_filePath)) {
      if (CommandWrapper::projectPath().empty()) {
        m_compiler->ErrorMessage(logPrefix() + "Cannot remove " + m_filePath.string() + " because the project path is not yet known. "
"If you are attempting to run clear_compilation_cache from Tcl, make sure you run it "
"immediately before the compilation task, after the project folder has been instantiated.");
      }
    }
  }

  void load() {
    if (FileUtils::IsExistedRegularFile(m_filePath)) {
      std::string content = FileUtils::GetFileContent(m_filePath);
      nlohmann::json json = nlohmann::json::parse(content);
      if (!json.is_null()) {
        m_taskCommandsMap = json.get<std::unordered_map<std::string, CommandWrapperPtr>>();
      }
    }
  }

  void setProjectPath(const std::filesystem::path& path) {
    m_filePath = path / COMPILATION_CACHE_FILENAME;
    ScriptRenderer::setProjectPath(path);
    CommandWrapper::setProjectPath(path);
  }

private:
  Compiler* m_compiler = nullptr; // used for log messages
  std::unordered_map<std::string, CommandWrapperPtr> m_taskCommandsMap;
  std::filesystem::path m_filePath{COMPILATION_CACHE_FILENAME};
  std::string m_logPrefix = "task_comp_manager: ";

  std::string key(int taskId) const {
    return std::to_string(taskId);
  }
  std::string key(int taskId, const std::string& profile) const {
    if (profile.empty()) {
      return std::to_string(taskId);
    } else {
      return std::to_string(taskId) + "_" + profile;
    }
  }

  bool isCompilationRequired(const std::string& id, const CommandWrapperPtr& command) const {
    if (!command) {
      // if in some reason current command is nullptr, means we cannot reconstruct current state and we do mark task as needed to compile.
      // the user will get error message about why the command reconstruction failed. After user sort the issue, by correcting settings or so, 
      // invalidation task will be re-triggered.
      return true;
    }

    auto it = m_taskCommandsMap.find(id);
    if (it == m_taskCommandsMap.end()) {
      if (CommandWrapper::isLogEnabled()) {
        m_compiler->Message(logPrefix() + "task [" + id + "] wasn't previously compiled");
      }
      return true;
    }
    const CommandWrapper& commandOld = *it->second;
    DiffCommandPtr diff = command->collectDiff(commandOld);
    if (!diff->isEmpty()) {
      if (CommandWrapper::isLogEnabled()) {
        for (const std::string& msg: diff->messages()) {
          m_compiler->Message(logPrefix() + "task(" + id + "), detects " + msg);
        }
      }
      return true;
    }

    return false;
  }

  void save() {
    nlohmann::json json = m_taskCommandsMap;
    if (!json.is_null()) {
      FileUtils::WriteToFile(m_filePath, json.dump());
    }
  }

  const std::string& logPrefix() const { return m_logPrefix; }
};

}  // namespace FOEDAG
