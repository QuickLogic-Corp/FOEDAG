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

#include <map>

namespace FOEDAG {

class TaskCompilationStateManager {
  DiffCommandPtr matches(int taskId, const CommandWrapperPtr& command) const {
    DiffCommandPtr diff = std::make_shared<DiffCommand>();
    auto it = m_taskCommandsMap.find(taskId);
    if (it == m_taskCommandsMap.end()) {
      return diff;
    }
    const CommandWrapper& commandOld = *it->second;
    command->compareWithOld(commandOld, diff);
    return diff;
  }

  void storeTaskCommand(int taskId, const CommandWrapperPtr& command) {
    m_taskCommandsMap[taskId] = command;
  }

private:
  std::map<int, CommandWrapperPtr> m_taskCommandsMap;
};

}  // namespace FOEDAG
