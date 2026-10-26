/*
Copyright 2026 The Foedag team

GPL License

Copyright (c) 2026 The Open-Source FPGA Foundation

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

#include <QHash>
#include <QObject>
#include <QStringList>

namespace FOEDAG {

// Counts named milestones raised by the application in replay mode, so that
// gui_wait_event can wait for asynchronous work instead of sleeping.
class GuiTestHub : public QObject {
  Q_OBJECT

 public:
  enum class Kind { Sync, Info };

  static GuiTestHub *instance();
  static Kind kindOf(const QString &name);

  void enable();
  void disable();

  // Thread-safe; occurrences from other threads are counted once they reach
  // the GUI thread.
  void raise(const QString &name, const QString &arg = {});
  void bridge(QObject *object);

  void takeSnapshot();
  // An empty arg matches any argument.
  bool reachedSinceSnapshot(const QString &name, const QString &arg) const;
  int count(const QString &name, const QString &arg) const;
  QStringList recent() const { return m_recent; }

  // The override cursor raises no event, so waits call this between checks.
  void pollBusy();

 signals:
  // Every counted milestone, in the GUI thread; the recorder turns them into
  // waits.
  void counted(const QString &name, const QString &arg);

 protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

 private:
  GuiTestHub() = default;
  void record(const QString &name, const QString &arg);
  void watchProcess(QObject *child);

  QHash<QString, int> m_count;
  QHash<QString, int> m_snapshot;
  QStringList m_recent;
  bool m_busy{false};
};

}  // namespace FOEDAG
