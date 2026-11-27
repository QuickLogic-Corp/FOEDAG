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
#include "InteractivePathAnalysis/NCriticalPathView.h"

#include <QApplication>
#include <QFile>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTest>

#include "gtest/gtest.h"

using namespace FOEDAG;

namespace {

QModelIndex pathRow(QAbstractItemModel *model, int path) {
  const QString prefix = QString("#Path %1\n").arg(path);
  for (int row = 0; row < model->rowCount(); ++row) {
    QModelIndex index = model->index(row, 0);
    if (index.data().toString().startsWith(prefix)) return index;
  }
  return {};
}

}  // namespace

// A path is clicked, then within the double-click interval another is
// right-clicked to expand it, as gui_click does in a replay. The view must
// stay put: the click's delayed scroll to its row must not fire after the
// expand has pushed that row down.
TEST(NCriticalPathView, ExpandSoonAfterAClickDoesNotScroll) {
  NCriticalPathView view;
  QFile file(":/InteractivePathAnalysis/data/report_timing.setup.rpt.sample");
  ASSERT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
  QSignalSpy loaded(&view, &NCriticalPathView::dataLoaded);
  emit view.loadFromString(QString::fromUtf8(file.readAll()));
  ASSERT_TRUE(loaded.wait(10000));
  view.resize(800, 600);
  view.show();
  ASSERT_TRUE(QTest::qWaitForWindowExposed(&view));

  const QModelIndex first = pathRow(view.model(), 1);
  const QModelIndex sixth = pathRow(view.model(), 6);
  ASSERT_TRUE(first.isValid() && sixth.isValid());
  ASSERT_TRUE(view.model()->hasChildren(first));
  ASSERT_TRUE(view.viewport()->rect().contains(view.visualRect(sixth)));

  QTest::mouseClick(view.viewport(), Qt::LeftButton, {},
                    view.visualRect(sixth).center());
  QTest::mouseClick(view.viewport(), Qt::RightButton, {},
                    view.visualRect(first).center());
  ASSERT_TRUE(view.isExpanded(first));
  ASSERT_FALSE(view.viewport()->rect().contains(view.visualRect(sixth)))
      << "the expand pushes path 6 out of view";
  const int scrolled = view.verticalScrollBar()->value();
  QTest::qWait(QApplication::doubleClickInterval() + 400);
  EXPECT_EQ(view.verticalScrollBar()->value(), scrolled);
  EXPECT_TRUE(view.selectionModel()->isSelected(sixth));
}
