#pragma once

#include <QObject>

#include "Compiler/Task.h"

namespace FOEDAG {

class TaskStatusWatcher : public QObject {
  Q_OBJECT

 public:
  static TaskStatusWatcher *Instance();

  void reset() { m_isSynthResultDirty = false; }
  bool isSynthResultDirty() const { return m_isSynthResultDirty; }

signals:
  void synthResultDirty();
  void synthSucceeded();
  void synthFailed();

public slots:
  void onTaskDone(uint taskId, TaskStatus status);
  void onDesignFilesChanged();

 private:
  bool m_isSynthResultDirty = false;
};

}  // namespace FOEDAG
