/*
Copyright 2022 The Foedag team

GPL License

Copyright (c) 2022 The Open-Source FPGA Foundation

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

#include "PinAssignment/PinAssignmentBaseView.h"

#include <QComboBox>
#include <QStringListModel>

#include "gtest/gtest.h"

using namespace FOEDAG;

class PinAssignmentBaseViewTest : public PinAssignmentBaseView {
 public:
  static constexpr int COL0{0};
  static constexpr int COL1{1};
  static constexpr int COL2{2};
  QComboBox *combo0{nullptr};
  QComboBox *combo1{nullptr};
  QTreeWidgetItem *item{nullptr};
  PinAssignmentBaseViewTest() : PinAssignmentBaseView{nullptr} {
    setColumnCount(3);
    item = new QTreeWidgetItem(this);
    QStringListModel *model = new QStringListModel;
    model->setStringList({"item0", "item1", "item2"});
    combo0 = new QComboBox;
    combo0->setModel(model);
    setItemWidget(item, COL0, combo0);

    combo1 = new QComboBox;
    combo1->setModel(model);
    setItemWidget(item, COL1, combo1);

    item->setText(COL2, "text");
  }

  void setComboDataTest(const QModelIndex &index, const QString &data) {
    setComboData(index, data);
  }
  void setComboDataTest(const QModelIndex &index, int col,
                        const QString &data) {
    setComboData(index, col, data);
  }
  QComboBox *GetComboTest(const QModelIndex &index) { return GetCombo(index); }
  QComboBox *GetComboTest(const QModelIndex &index, int col) {
    return GetCombo(index, col);
  }
  QComboBox *GetComboTest(QTreeWidgetItem *item, int col) {
    return GetCombo(item, col);
  }
};

#ifdef UPSTREAM_PINPLANNER
TEST(PinAssignmentBaseView, setComboDataIndexOnlyValid) {
  PinAssignmentBaseViewTest view{};
  auto index = view.model()->index(0, PinAssignmentBaseViewTest::COL0);
  view.setComboDataTest(index, "item2");

  EXPECT_EQ(view.combo0->currentIndex(), 2);
  EXPECT_EQ(view.combo1->currentIndex(), 0);
}
#endif // UPSTREAM_PINPLANNER

TEST(PinAssignmentBaseView, setComboDataIndexOnlyInvalid) {
  PinAssignmentBaseViewTest view{};
  auto index = view.model()->index(0, PinAssignmentBaseViewTest::COL0);
  view.setComboDataTest(index, "anything");

  EXPECT_EQ(view.combo0->currentIndex(), 0);
  EXPECT_EQ(view.combo1->currentIndex(), 0);
}

#ifdef UPSTREAM_PINPLANNER
TEST(PinAssignmentBaseView, setComboDataColumnOnlyValid) {
  PinAssignmentBaseViewTest view{};
  auto index = view.model()->index(0, PinAssignmentBaseViewTest::COL0);
  view.setComboDataTest(index, PinAssignmentBaseViewTest::COL1, "item2");

  EXPECT_EQ(view.combo0->currentIndex(), 0);
  EXPECT_EQ(view.combo1->currentIndex(), 2);
}
#endif // UPSTREAM_PINPLANNER

TEST(PinAssignmentBaseView, setComboDataColumnOnlyInvalid) {
  PinAssignmentBaseViewTest view{};
  auto index = view.model()->index(0, PinAssignmentBaseViewTest::COL0);
  view.setComboDataTest(index, PinAssignmentBaseViewTest::COL1, "anything");

  EXPECT_EQ(view.combo0->currentIndex(), 0);
  EXPECT_EQ(view.combo1->currentIndex(), 0);
}

#ifdef UPSTREAM_PINPLANNER
TEST(PinAssignmentBaseView, GetComboIndexValid) {
  PinAssignmentBaseViewTest view{};
  auto index = view.model()->index(0, PinAssignmentBaseViewTest::COL0);
  EXPECT_EQ(view.GetComboTest(index), view.combo0);
}
#endif // UPSTREAM_PINPLANNER

TEST(PinAssignmentBaseView, GetComboIndexInvalid) {
  PinAssignmentBaseViewTest view{};
  auto index = view.model()->index(0, PinAssignmentBaseViewTest::COL2);
  EXPECT_EQ(index.isValid(), true);
  EXPECT_EQ(view.GetComboTest(index), nullptr);
}

#ifdef UPSTREAM_PINPLANNER
TEST(PinAssignmentBaseView, GetComboColumnValid) {
  PinAssignmentBaseViewTest view{};
  auto index = view.model()->index(0, PinAssignmentBaseViewTest::COL0);
  EXPECT_EQ(view.GetComboTest(index, PinAssignmentBaseViewTest::COL1),
            view.combo1);
}
#endif // UPSTREAM_PINPLANNER

TEST(PinAssignmentBaseView, GetComboColumnInvalid) {
  PinAssignmentBaseViewTest view{};
  auto index = view.model()->index(0, PinAssignmentBaseViewTest::COL0);
  EXPECT_EQ(index.isValid(), true);
  EXPECT_EQ(view.GetComboTest(index, PinAssignmentBaseViewTest::COL2), nullptr);
}

#ifdef UPSTREAM_PINPLANNER
TEST(PinAssignmentBaseView, GetComboItemValid) {
  PinAssignmentBaseViewTest view{};
  auto item = view.item;
  EXPECT_EQ(view.GetComboTest(item, PinAssignmentBaseViewTest::COL1),
            view.combo1);
}
#endif // UPSTREAM_PINPLANNER

TEST(PinAssignmentBaseView, GetComboItemInvalid) {
  PinAssignmentBaseViewTest view{};
  auto item = view.item;
  EXPECT_EQ(view.GetComboTest(item, PinAssignmentBaseViewTest::COL2), nullptr);
}

