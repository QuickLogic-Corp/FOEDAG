#include "TaskStatusWatcher.h"
#include "Compiler/CompilerDefines.h"

#include <QDebug>

namespace FOEDAG {

TaskStatusWatcher* TaskStatusWatcher::Instance() {
  static TaskStatusWatcher watcher{};
  return &watcher;
}

void TaskStatusWatcher::reset()
{
  m_isSynthResultDirty = false;
  m_isDesignChangedFirstTime = true;
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
  if (!m_isDesignChangedFirstTime) {
    m_isSynthResultDirty = true;
    emit synthResultDirty();
  } else {
    m_isDesignChangedFirstTime = false;
  }
}

}  // namespace FOEDAG
