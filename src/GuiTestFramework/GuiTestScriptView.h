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

#include <QTreeWidget>
#include <QVector>

namespace FOEDAG {

// The replayed script in its own window (display mode): finished commands in
// gray, running ones highlighted, the next one marked, and beside each line
// the screenshots it took, opened in the system viewer on click. Not in any
// screenshot.
class GuiTestScriptView : public QTreeWidget {
 public:
  struct Span {
    int line;   // 1-based, where the command starts
    int lines;  // how many lines it takes
  };
  static constexpr int kScriptColumn = 0;
  static constexpr int kScreenshotColumn = 1;

  GuiTestScriptView(const QString &file, const QString &text);

  // running: indexes into commands, the innermost last; next: the index of the
  // command to run next, or commands.size() once all have started.
  void showProgress(const QVector<Span> &commands, const QVector<int> &running,
                    int next);
  // A screenshot saved by the command starting at this 1-based line.
  void addScreenshot(int line, const QString &path);
  QStringList screenshots(int line) const;

 private:
  void open(QTreeWidgetItem *item, int column);
};

}  // namespace FOEDAG
