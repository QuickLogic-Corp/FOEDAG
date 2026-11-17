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
#include "GuiTestScriptView.h"

#include <QFileInfo>
#include <QFontDatabase>
#include <QTextBlock>

#include "GuiTestHub.h"

namespace FOEDAG {

GuiTestScriptView::GuiTestScriptView(const QString &file, const QString &text) {
  setWindowTitle("Replay " + QFileInfo(file).fileName());
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_QuitOnClose, false);
  GuiTestHub::markInternal(this);
  setReadOnly(true);
  setLineWrapMode(QPlainTextEdit::NoWrap);
  setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  QStringList lines = text.split('\n');
  const int width = QString::number(lines.size()).size();
  for (int i = 0; i < lines.size(); ++i)
    lines[i] = QString("%1  %2").arg(i + 1, width).arg(lines.at(i));
  setPlainText(lines.join('\n'));
  resize(720, 480);
}

void GuiTestScriptView::showProgress(const QVector<Span> &commands,
                                     const QVector<int> &running, int next) {
  QList<QTextEdit::ExtraSelection> marks;
  auto mark = [&](int index, const QTextCharFormat &format) {
    const Span &span = commands.at(index);
    for (int i = 0; i < span.lines; ++i) {
      QTextEdit::ExtraSelection s;
      s.cursor = QTextCursor{document()->findBlockByNumber(span.line - 1 + i)};
      s.format = format;
      // A background fills the row only without a selection; a text color
      // needs the text selected.
      if (format.hasProperty(QTextFormat::BackgroundBrush))
        s.format.setProperty(QTextFormat::FullWidthSelection, true);
      else
        s.cursor.movePosition(QTextCursor::EndOfBlock,
                              QTextCursor::KeepAnchor);
      marks << s;
    }
  };
  QTextCharFormat done;
  done.setForeground(Qt::gray);
  QTextCharFormat current;
  current.setBackground(QColor{255, 236, 140});
  QTextCharFormat upcoming;
  upcoming.setBackground(QColor{205, 225, 255});
  for (int i = 0; i < next && i < commands.size(); ++i)
    if (!running.contains(i)) mark(i, done);
  for (int i : running) mark(i, current);
  if (next < commands.size()) mark(next, upcoming);
  setExtraSelections(marks);

  // Keep the line that runs, or else the next one, in view.
  const int focus = !running.isEmpty() ? running.last()
                    : next < commands.size() ? next
                                             : -1;
  if (focus >= 0) {
    const int block = commands.at(focus).line - 1;
    setTextCursor(QTextCursor{document()->findBlockByNumber(block)});
    ensureCursorVisible();
  }
  // A line may block the event loop, as a batch synthesis in the setup does.
  viewport()->repaint();
}

}  // namespace FOEDAG
