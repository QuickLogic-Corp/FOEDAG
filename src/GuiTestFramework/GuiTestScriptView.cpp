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

#include <QAction>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include "GuiTestHub.h"

namespace FOEDAG {

namespace {

constexpr int kPathsRole = Qt::UserRole;
constexpr int kLineRole = Qt::UserRole + 1;  // the line without its number
constexpr int kMarkSize = 12;

// A red circle for a breakpoint; every other line gets the same space empty,
// so the text stays aligned.
QIcon markIcon(bool breakpoint) {
  QPixmap pixmap{kMarkSize, kMarkSize};
  pixmap.fill(Qt::transparent);
  if (breakpoint) {
    QPainter painter{&pixmap};
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor{220, 40, 40});
    painter.drawEllipse(1, 1, kMarkSize - 2, kMarkSize - 2);
  }
  return QIcon{pixmap};
}

QIcon runIcon() {
  QPixmap pixmap{16, 16};
  pixmap.fill(Qt::transparent);
  QPainter painter{&pixmap};
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor{30, 160, 60});
  painter.drawPolygon(QPolygon{{3, 2}, {14, 8}, {3, 14}});
  return QIcon{pixmap};
}

}  // namespace

GuiTestScriptView::GuiTestScriptView(const QString &file, const QString &text,
                                     const QVector<Span> &commands)
    : m_commands(commands) {
  setWindowTitle("Replay " + QFileInfo(file).fileName());
  setAttribute(Qt::WA_ShowWithoutActivating);
  setAttribute(Qt::WA_QuitOnClose, false);
  GuiTestHub::markInternal(this);

  auto toolbar = new QToolBar;
  m_run = new QToolButton;
  m_run->setIcon(runIcon());
  m_run->setToolTip("Run to the next breakpoint, or to the end");
  connect(m_run, &QToolButton::clicked, this,
          &GuiTestScriptView::runRequested);
  toolbar->addWidget(m_run);
  m_state = new QLabel{"Running"};
  m_state->setContentsMargins(8, 0, 0, 0);
  toolbar->addWidget(m_state);

  m_tree = new QTreeWidget;
  m_tree->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_tree->setRootIsDecorated(false);
  m_tree->setUniformRowHeights(true);
  m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_tree->setExpandsOnDoubleClick(false);
  m_tree->setHeaderLabels({"Script", "Screenshot"});
  // The script column takes the rest, so the screenshots stay in view; a long
  // line is cut short and shown whole in its tooltip.
  m_tree->header()->setStretchLastSection(false);
  m_tree->header()->setSectionResizeMode(kScriptColumn, QHeaderView::Stretch);
  m_tree->header()->setSectionResizeMode(kScreenshotColumn,
                                         QHeaderView::ResizeToContents);
  const QStringList lines = text.split('\n');
  const int width = QString::number(lines.size()).size();
  for (int i = 0; i < lines.size(); ++i) {
    auto item = new QTreeWidgetItem(
        m_tree, {QString("%1  %2").arg(i + 1, width).arg(lines.at(i))});
    item->setToolTip(kScriptColumn, lines.at(i));
    item->setData(kScriptColumn, kLineRole, lines.at(i));
    item->setIcon(kScriptColumn, markIcon(false));
  }
  auto copy = new QAction{"Copy", m_tree};
  copy->setShortcut(QKeySequence::Copy);
  copy->setShortcutContext(Qt::WidgetShortcut);
  connect(copy, &QAction::triggered, this, [this]() {
    QGuiApplication::clipboard()->setText(selectedScript());
  });
  m_tree->addAction(copy);
  m_tree->setContextMenuPolicy(Qt::ActionsContextMenu);
  connect(m_tree, &QTreeWidget::itemClicked, this, &GuiTestScriptView::open);
  connect(m_tree, &QTreeWidget::itemDoubleClicked, this,
          [this](QTreeWidgetItem *item, int column) {
            if (column == kScriptColumn)
              toggleBreakpoint(m_tree->indexOfTopLevelItem(item) + 1);
          });

  auto layout = new QVBoxLayout{this};
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(toolbar);
  layout->addWidget(m_tree);
  resize(800, 480);
}

void GuiTestScriptView::showProgress(const QVector<int> &running, int next) {
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
    QTreeWidgetItem *item = m_tree->topLevelItem(i);
    item->setData(kScriptColumn, Qt::ForegroundRole, {});
    item->setData(kScriptColumn, Qt::BackgroundRole, {});
    item->setData(kScreenshotColumn, Qt::BackgroundRole, {});
  }
  const int count = m_commands.size();
  auto mark = [&](int index, auto &&apply) {
    const Span &span = m_commands.at(index);
    for (int i = 0; i < span.lines; ++i)
      if (QTreeWidgetItem *item = m_tree->topLevelItem(span.line - 1 + i))
        apply(item);
  };
  auto fill = [](const QColor &color) {
    return [color](QTreeWidgetItem *item) {
      item->setBackground(kScriptColumn, color);
      item->setBackground(kScreenshotColumn, color);
    };
  };
  for (int i = 0; i < next && i < count; ++i)
    if (!running.contains(i))
      mark(i, [](QTreeWidgetItem *item) {
        item->setForeground(kScriptColumn, Qt::gray);
      });
  for (int i : running) mark(i, fill(QColor{255, 236, 140}));
  if (next < count) mark(next, fill(QColor{205, 225, 255}));

  // Keep the line that runs, or else the next one, in view.
  const int focus = !running.isEmpty() ? running.last()
                    : next < count     ? next
                                       : -1;
  if (focus >= 0)
    if (QTreeWidgetItem *item =
            m_tree->topLevelItem(m_commands.at(focus).line - 1))
      m_tree->scrollToItem(item);
  // A line may block the event loop, as a batch synthesis in the setup does.
  m_tree->viewport()->repaint();
}

void GuiTestScriptView::addScreenshot(int line, const QString &path) {
  QTreeWidgetItem *item = m_tree->topLevelItem(line - 1);
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
  m_tree->viewport()->repaint();
}

QStringList GuiTestScriptView::screenshots(int line) const {
  const QTreeWidgetItem *item = m_tree->topLevelItem(line - 1);
  return item ? item->data(kScreenshotColumn, kPathsRole).toStringList()
              : QStringList{};
}

void GuiTestScriptView::setPaused(bool paused) {
  if (paused == m_paused) return;
  m_paused = paused;
  m_state->setText(paused ? "Paused: Space runs the next line, Run goes on"
                          : "Running");
  m_state->repaint();
}

void GuiTestScriptView::showResult(int command, const QString &message) {
  m_run->setEnabled(false);
  if (message.isEmpty()) {
    m_state->setText("Passed. Close this window to exit.");
    return;
  }
  QString where;
  if (command >= 0) {
    const Span &span = m_commands.at(command);
    where = QString(" at line %1").arg(span.line);
    for (int i = 0; i < span.lines; ++i)
      if (QTreeWidgetItem *item = m_tree->topLevelItem(span.line - 1 + i)) {
        item->setBackground(kScriptColumn, QColor{255, 170, 170});
        item->setBackground(kScreenshotColumn, QColor{255, 170, 170});
      }
    if (QTreeWidgetItem *item = m_tree->topLevelItem(span.line - 1))
      m_tree->scrollToItem(item);
  }
  constexpr int kShownChars = 160;  // the whole message is in the tooltip
  const QString shown = message.size() > kShownChars
                            ? message.left(kShownChars) + "..."
                            : message;
  m_state->setText(
      QString("Failed%1: %2 Close this window to exit.").arg(where, shown));
  m_state->setToolTip(message);
  m_state->setStyleSheet("color: #b00000;");
}

void GuiTestScriptView::closeEvent(QCloseEvent *event) {
  QWidget::closeEvent(event);
  emit closed();
}

void GuiTestScriptView::toggleBreakpoint(int line) {
  const int command = commandAt(line);
  if (command < 0) return;
  const bool set = !m_breakpoints.contains(command);
  set ? void(m_breakpoints.insert(command))
      : void(m_breakpoints.remove(command));
  if (QTreeWidgetItem *item =
          m_tree->topLevelItem(m_commands.at(command).line - 1))
    item->setIcon(kScriptColumn, markIcon(set));
}

QString GuiTestScriptView::selectedScript() const {
  QStringList lines;
  for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
    if (m_tree->topLevelItem(i)->isSelected())
      lines << m_tree->topLevelItem(i)
                   ->data(kScriptColumn, kLineRole)
                   .toString();
  return lines.join('\n');
}

bool GuiTestScriptView::hasBreakpoint(int command) const {
  return m_breakpoints.contains(command);
}

// The command a line belongs to, or -1 for a comment or blank line.
int GuiTestScriptView::commandAt(int line) const {
  for (int i = 0; i < m_commands.size(); ++i) {
    const Span &span = m_commands.at(i);
    if (line >= span.line && line < span.line + span.lines) return i;
  }
  return -1;
}

// A line with several screenshots opens all of them.
void GuiTestScriptView::open(QTreeWidgetItem *item, int column) {
  if (column != kScreenshotColumn) return;
  for (const QString &path : item->data(column, kPathsRole).toStringList())
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

}  // namespace FOEDAG
