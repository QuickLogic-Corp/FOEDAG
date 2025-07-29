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

#include "CommandWrapper.h"
#include "Utils/FileUtils.h"

#include "nlohmann_json/json.hpp"

#include <unordered_map>
#include <iostream>

namespace nlohmann {
template <>
struct adl_serializer<std::shared_ptr<FOEDAG::CommandWrapper>> {
    static void to_json(json& json, const std::shared_ptr<FOEDAG::CommandWrapper>& ptr) {
      if (ptr) {
        json = *ptr;
      } else {
        json = nullptr;
      }
    }

    static void from_json(const json& json, std::shared_ptr<FOEDAG::CommandWrapper>& ptr) {
      if (json.is_null()) {
        ptr = nullptr;
      } else {
        ptr = std::make_shared<FOEDAG::CommandWrapper>(json.get<FOEDAG::CommandWrapper>());
      }
    }
};
} // namespace nlohmann

namespace FOEDAG {

constexpr const char* BUILD_STATE_FILENAME = "build_states.json";
class TaskCompilationStateManager {
public:
  bool isCompilationRequired(int taskId, const CommandWrapperPtr& command) const {
    return isCompilationRequired(key(taskId), command);
  }

  bool isCompilationRequired(int taskId, const std::string& profile, const CommandWrapperPtr& command) const {
    return isCompilationRequired(key(taskId, profile), command);
  }

  void storeTaskCommand(int taskId, const CommandWrapperPtr& command) {
    m_taskCommandsMap[key(taskId)] = command;
    save();
  }

  void storeTaskCommand(int taskId, const std::string& profile, const CommandWrapperPtr& command) {
    m_taskCommandsMap[key(taskId, profile)] = command;
    save();
  }
  
  bool isEmpty() const { return m_taskCommandsMap.empty(); }

  void clear() {
    m_taskCommandsMap.clear();
    FileUtils::removeFile(m_filePath);
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
    m_filePath = path / BUILD_STATE_FILENAME;
    CommandWrapper::setProjectPath(path);
  }

private:
  std::unordered_map<std::string, CommandWrapperPtr> m_taskCommandsMap;
  std::filesystem::path m_filePath{BUILD_STATE_FILENAME};

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
    auto it = m_taskCommandsMap.find(id);
    if (it == m_taskCommandsMap.end()) {
      return true;
    }
    const CommandWrapper& commandOld = *it->second;
    DiffCommandPtr diff = command->compare(commandOld);
    if (!diff->isEmpty()) {
      for (const std::string& msg: diff->messages()) {
        std::cout << "~~~ diff msg=" << msg << std::endl;
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
};

}  // namespace FOEDAG
