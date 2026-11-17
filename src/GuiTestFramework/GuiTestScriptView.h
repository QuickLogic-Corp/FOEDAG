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

#include <QSet>
#include <QVector>
#include <QWidget>

class QLabel;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace FOEDAG {

// The replayed script in its own window (display mode): finished commands in
// gray, running ones highlighted, the next one marked, and beside each line
// the screenshots it took, opened in the system viewer on click. A double
// click on a line sets or clears a breakpoint on its command; Run lets the
// script go on to the next breakpoint. Selected lines copy as script text.
// Not in any screenshot.
class GuiTestScriptView : public QWidget {
  Q_OBJECT

 public:
  struct Span {
    int line;   // 1-based, where the command starts
    int lines;  // how many lines it takes
  };
  static constexpr int kScriptColumn = 0;
  static constexpr int kScreenshotColumn = 1;

  GuiTestScriptView(const QString &file, const QString &text,
                    const QVector<Span> &commands);

  // running: indexes into commands, the innermost last; next: the index of the
  // command to run next, or the count once all have started.
  void showProgress(const QVector<int> &running, int next);
  // A screenshot saved by the command starting at this 1-based line.
  void addScreenshot(int line, const QString &path);
  QStringList screenshots(int line) const;
  // Whether the script is held before its next line.
  void setPaused(bool paused);

  // The end of the run: failed (the command, or -1 if none ran) with the
  // message, or passed when the message is empty.
  void showResult(int command, const QString &message);

  void toggleBreakpoint(int line);  // any 1-based line of the command
  bool hasBreakpoint(int command) const;

  // The selected lines as written in the script, in script order; Ctrl+C or
  // the context menu puts them on the clipboard.
  QString selectedScript() const;

  QTreeWidget *tree() const { return m_tree; }
  QToolButton *runButton() const { return m_run; }

 signals:
  void runRequested();
  void closed();

 protected:
  void closeEvent(QCloseEvent *event) override;

 private:
  void open(QTreeWidgetItem *item, int column);
  int commandAt(int line) const;

  QVector<Span> m_commands;
  QSet<int> m_breakpoints;  // command indexes
  QTreeWidget *m_tree;
  QToolButton *m_run;
  QLabel *m_state;
  bool m_paused{false};
};

}  // namespace FOEDAG
