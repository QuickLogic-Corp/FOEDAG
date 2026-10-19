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

#include <QObject>
#include <QPointer>
#include <QVector>

struct Tcl_Interp;
class QWidget;

namespace FOEDAG {

// Runs a "# gui-test: sync" replay script one Tcl command at a time. It is
// driven by a QTimer rather than Tcl "after", so lines also run inside the
// nested event loop of a modal dialog opened by an action.
class GuiTestRunner : public QObject {
  Q_OBJECT

 public:
  static constexpr const char *kSyncHeader = "# gui-test: sync";

  static GuiTestRunner *instance();
  static bool isSyncScript(const std::string &file);

  // Does not return: the process exits with the script's result.
  [[noreturn]] void run(Tcl_Interp *interp, const QString &file);

  // Marks a wait that a modal dialog must not interrupt: while it is active,
  // a dialog that opens does not let the next line run.
  class StrictWait {
   public:
    StrictWait();
    ~StrictWait();
  };

  // Ends the run from anywhere, including inside a nested event loop that
  // can no longer unwind.
  [[noreturn]] void fail(const QString &message);

 protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

 private:
  struct Command {
    int line;
    QString text;
  };
  struct Frame {
    int index;  // into m_commands
    QPointer<QWidget> modal;
    int strictWaits{0};
  };

  GuiTestRunner() = default;
  static constexpr int kBlockedPollMs = 5;

  bool canRunNext() const;
  void scheduleStep(int delayMs);
  void step();
  [[noreturn]] void finish(int code);

  Tcl_Interp *m_interp{nullptr};
  QString m_file;
  QVector<Command> m_commands;
  int m_next{0};
  QVector<Frame> m_running;  // innermost last
};

}  // namespace FOEDAG
