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

#include <QDesktopServices>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QUrl>

#include "GuiTestHub.h"

namespace FOEDAG {

namespace {
constexpr int kPathsRole = Qt::UserRole;
}  // namespace

GuiTestScriptView::GuiTestScriptView(const QString &file, const QString &text) {
  setWindowTitle("Replay " + QFileInfo(file).fileName());
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_QuitOnClose, false);
  GuiTestHub::markInternal(this);
  setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  setRootIsDecorated(false);
  setUniformRowHeights(true);
  setSelectionMode(QAbstractItemView::NoSelection);
  setHeaderLabels({"Script", "Screenshot"});
  // The script column takes the rest, so the screenshots stay in view; a long
  // line is cut short and shown whole in its tooltip.
  header()->setStretchLastSection(false);
  header()->setSectionResizeMode(kScriptColumn, QHeaderView::Stretch);
  header()->setSectionResizeMode(kScreenshotColumn,
                                 QHeaderView::ResizeToContents);

  const QStringList lines = text.split('\n');
  const int width = QString::number(lines.size()).size();
  for (int i = 0; i < lines.size(); ++i) {
    auto item = new QTreeWidgetItem(
        this, {QString("%1  %2").arg(i + 1, width).arg(lines.at(i))});
    item->setToolTip(kScriptColumn, lines.at(i));
  }
  connect(this, &QTreeWidget::itemClicked, this, &GuiTestScriptView::open);
  resize(800, 480);
}

void GuiTestScriptView::showProgress(const QVector<Span> &commands,
                                     const QVector<int> &running, int next) {
  for (int i = 0; i < topLevelItemCount(); ++i) {
    QTreeWidgetItem *item = topLevelItem(i);
    item->setData(kScriptColumn, Qt::ForegroundRole, {});
    item->setData(kScriptColumn, Qt::BackgroundRole, {});
    item->setData(kScreenshotColumn, Qt::BackgroundRole, {});
  }
  auto mark = [&](int index, auto &&apply) {
    const Span &span = commands.at(index);
    for (int i = 0; i < span.lines; ++i)
      if (QTreeWidgetItem *item = topLevelItem(span.line - 1 + i)) apply(item);
  };
  auto fill = [](const QColor &color) {
    return [color](QTreeWidgetItem *item) {
      item->setBackground(kScriptColumn, color);
      item->setBackground(kScreenshotColumn, color);
    };
  };
  for (int i = 0; i < next && i < commands.size(); ++i)
    if (!running.contains(i))
      mark(i, [](QTreeWidgetItem *item) {
        item->setForeground(kScriptColumn, Qt::gray);
      });
  for (int i : running) mark(i, fill(QColor{255, 236, 140}));
  if (next < commands.size()) mark(next, fill(QColor{205, 225, 255}));

  // Keep the line that runs, or else the next one, in view.
  const int focus = !running.isEmpty() ? running.last()
                    : next < commands.size() ? next
                                             : -1;
  if (focus >= 0)
    if (QTreeWidgetItem *item = topLevelItem(commands.at(focus).line - 1))
      scrollToItem(item);
  // A line may block the event loop, as a batch synthesis in the setup does.
  viewport()->repaint();
}

void GuiTestScriptView::addScreenshot(int line, const QString &path) {
  QTreeWidgetItem *item = topLevelItem(line - 1);
  if (!item) return;
  QStringList paths = item->data(kScreenshotColumn, kPathsRole).toStringList();
  paths << path;
  item->setData(kScreenshotColumn, kPathsRole, paths);
  QStringList names;
  for (const QString &p : paths) names << QFileInfo(p).completeBaseName();
  item->setText(kScreenshotColumn, names.join(", "));
  item->setToolTip(kScreenshotColumn, paths.join('\n'));
  item->setForeground(kScreenshotColumn, palette().link());
  QFont font = item->font(kScreenshotColumn);
  font.setUnderline(true);
  item->setFont(kScreenshotColumn, font);
  viewport()->repaint();
}

QStringList GuiTestScriptView::screenshots(int line) const {
  const QTreeWidgetItem *item = topLevelItem(line - 1);
  return item ? item->data(kScreenshotColumn, kPathsRole).toStringList()
              : QStringList{};
}

// A line with several screenshots opens all of them.
void GuiTestScriptView::open(QTreeWidgetItem *item, int column) {
  if (column != kScreenshotColumn) return;
  for (const QString &path : item->data(column, kPathsRole).toStringList())
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

}  // namespace FOEDAG
