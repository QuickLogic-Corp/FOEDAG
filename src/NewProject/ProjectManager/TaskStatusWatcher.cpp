#include "TaskStatusWatcher.h"
#include "Compiler/CompilerDefines.h"

#include <QDebug>

#include "GuiTestFramework/GuiMilestone.h"

namespace FOEDAG {

TaskStatusWatcher::TaskStatusWatcher() { GUI_TEST_BRIDGE(this); }

TaskStatusWatcher* TaskStatusWatcher::Instance() {
  static TaskStatusWatcher watcher{};
  return &watcher;
}

void TaskStatusWatcher::onTaskDone(uint taskId, TaskStatus status)
{
  if (taskId == SYNTHESIS) {
    if (status == TaskStatus::Success) {
      m_isSynthResultDirty = false;
      emit synthSucceeded();
    } else if (status == TaskStatus::Fail) {
      emit synthFailed();
    }
  }
}

void TaskStatusWatcher::onDesignFilesChanged()
{
  m_isSynthResultDirty = true;
  emit synthResultDirty();
}

}  // namespace FOEDAG
