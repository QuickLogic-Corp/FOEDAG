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
#include "PackagePinsView.h"

#include <QCompleter>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QStringListModel>
#include <QToolButton>

#include "LazyComboBox.h"

namespace FOEDAG {

constexpr int NameCol{0};
constexpr int AvailCol{1};
constexpr int PortsCol{2};
constexpr int ModeCol{3};
constexpr int InternalPinCol{4};

PackagePinsView::PackagePinsView(PinsBaseModel *model, QWidget *parent)
    : PinAssignmentBaseView(model, parent)
    , m_iconAdd(QIcon{":/images/add.png"})
{
  header()->resizeSections(QHeaderView::ResizeToContents);
  setColumnCount(model->packagePinModel()->header().count() + 1);
  for (auto &h : model->packagePinModel()->header()) {
    headerItem()->setText(h.id, h.name);
    headerItem()->setToolTip(h.id, h.description);
  }
  QTreeWidgetItem *topLevelPackagePin = new QTreeWidgetItem(this);
  topLevelPackagePin->setText(NameCol, "All Pins");
  const auto banks = model->packagePinModel()->pinData();
  const bool useBallId{model->packagePinModel()->useBallId()};
  for (const auto &b : banks) {
    QTreeWidgetItem *bank = new QTreeWidgetItem(topLevelPackagePin);
    bank->setText(NameCol, b.name);
    bank->setText(AvailCol, QString::number(b.pinData.count()));
    const auto pins = b.pinData;
    for (auto &p : pins) {
      uint col = PortsCol + 1;
      QTreeWidgetItem *pinItem = new QTreeWidgetItem(bank);
      m_pinItems.append(pinItem);
      insertData(p.data, useBallId ? BallId : BallName, NameCol, pinItem);
      insertData(p.data, RefClock, col++, pinItem);
      insertData(p.data, Bank, col++, pinItem);
      insertData(p.data, ALT, col++, pinItem);
      insertData(p.data, DebugMode, col++, pinItem);
      insertData(p.data, ScanMode, col++, pinItem);
      insertData(p.data, MbistMode, col++, pinItem);
      insertData(p.data, Type, col++, pinItem);

      m_directionItemColumn = col;
      insertData(p.data, Direction, col++, pinItem);

      insertData(p.data, Voltage, col++, pinItem);
      insertData(p.data, PowerPad, col++, pinItem);
      insertData(p.data, Discription, col++, pinItem);
      insertData(p.data, Voltage2, col++, pinItem);

      initLine(pinItem);

      auto [widget, button] =
          prepareButtonWithLabel(pinItem->text(0), m_iconAdd);

      button->hide();

      setItemWidget(pinItem, NameCol, widget);
    }
    expandItem(bank);
  }

  connect(model, &PinsBaseModel::portAssignmentRemoved, this,
          &PackagePinsView::portAssignmentRemoved);
  connect(model, &PinsBaseModel::portAssignmentChanged, this,
          &PackagePinsView::portAssignmentChanged);
  connect(model->packagePinModel(), &PackagePinsModel::pinNameChanged, this,
          &PackagePinsView::updatePinNames);
  expandItem(topLevelPackagePin);
  setAlternatingRowColors(true);
  setColumnWidth(NameCol, 200);
  setColumnWidth(ModeCol, 180);
  setColumnWidth(InternalPinCol, 170);
  // temporary hide columns since no data available
  headerItem()->setText(InternalPinCol + 1, QString{});
  headerItem()->setToolTip(InternalPinCol + 1, QString{});
  for (int i = ModeCol; i < columnCount() - 1; i++) hideColumn(i);

  const int lastCol = columnCount() - 1;
  resizeColumnToContents(lastCol);
  header()->setSectionResizeMode(lastCol, QHeaderView::Fixed);
  headerItem()->setText(lastCol, QString{});
  headerItem()->setToolTip(lastCol, QString{});
}

void PackagePinsView::SetPort(const QString &pin, const QString &port) {
  if (pin.isEmpty()) return;

  QModelIndexList indexes{match(pin)};

  if (!indexes.empty()) {
    auto index = indexes.at(0);
    setComboData(index, PortsCol, port);
  }
}

void PackagePinsView::cleanTable() {
  for (auto it{m_allCombo.cbegin()}; it != m_allCombo.cend(); it++) {
    it.key()->setCurrentIndex(0);
  }
  for (const auto &item : m_pinItems) {
    while (item->childCount() != 0) {
      auto child = item->child(0);
      removeItem(item, child);
    }
  }
}

void PackagePinsView::ioPortsSelectionHasChanged(const QModelIndex &index) {
  // update here Mode selection
  auto item = itemFromIndex(index);
  auto combo = item ? GetCombo(item, PortsCol) : nullptr;

  if (combo) {
    auto port = combo->currentText();
    removeDuplications(port, combo);

    auto pin = item->text(NameCol);

    m_blockUpdate = true;

    m_model->update(port, pin);
    m_blockUpdate = false;
    emit selectionHasChanged();
  }
}

void PackagePinsView::insertData(const QStringList &data, int index, int column,
                                 QTreeWidgetItem *item) {
  if (data.count() > index) item->setText(column, data.at(index));
}

std::pair<QWidget *, QToolButton *> PackagePinsView::prepareButtonWithLabel(
    const QString &text, const QIcon &icon) {
  QWidget *w = new QWidget;
  w->setLayout(new QHBoxLayout);
  w->layout()->setContentsMargins(0, 0, 0, 0);
  w->layout()->addWidget(new QLabel{text});
  auto btn = new QToolButton{};
  btn->setIcon(icon);
  w->layout()->addWidget(btn);
  w->setAutoFillBackground(true);
  return std::make_pair(w, btn);
}

void PackagePinsView::initLine(QTreeWidgetItem *item) {
  auto combo = new LazyComboBox;

  QString direction = item->text(m_directionItemColumn);
  if (m_initializedDirections.contains(direction)) {
    combo->setDelayedModel(m_model->portsModel()->listModel(direction));
  } else {
    // We must set at least one model in a straightforward way to ensure the correct column width in the table is applied.
    // This does not impact performance but solves many issues related to incorrect table width (column with comboboxes) and combobox dropdown content width.
    combo->setModel(m_model->portsModel()->listModel(direction));
    m_initializedDirections.insert(direction);
  }

  combo->setAutoFillBackground(true);
  combo->setEditable(true);
  auto completer{new QCompleter{m_model->portsModel()->listModel(direction)}};
  completer->setFilterMode(Qt::MatchContains);
  combo->setCompleter(completer);
  combo->setInsertPolicy(QComboBox::NoInsert);
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [=]() { ioPortsSelectionHasChanged(indexFromItem(item, PortsCol)); });
  connect(combo, &QComboBox::currentTextChanged, this,
          [=]() { ioPortsSelectionHasChanged(indexFromItem(item, PortsCol)); });
  connect(combo, &QComboBox::destroyed, this,
          [=]() { m_allCombo.remove(combo); });
  setItemWidget(item, PortsCol, combo);
  m_allCombo.insert(combo, indexFromItem(item));
}

void PackagePinsView::copyData(QTreeWidgetItem *from, QTreeWidgetItem *to) {
  int portIndex{0};

  for (auto column : {PortsCol, ModeCol, InternalPinCol})
    removeItemWidget(from, column);

  auto toCombo = GetCombo(to, PortsCol);
  if (toCombo) toCombo->setCurrentIndex(portIndex);
}

void PackagePinsView::resetItem(QTreeWidgetItem *item) {
  auto combo = GetCombo(item, InternalPinCol);
  if (combo) combo->setCurrentIndex(0);
  combo = GetCombo(item, ModeCol);
  if (combo) combo->setCurrentIndex(0);
  combo = GetCombo(item, PortsCol);
  if (combo) combo->setCurrentIndex(0);
}

void PackagePinsView::removeItem(QTreeWidgetItem *parent,
                                 QTreeWidgetItem *child) {
  if (parent->childCount() == 1) {
    initLine(parent);
    copyData(child, parent);
  } 
  parent->removeChild(child);
}

QString PackagePinsView::GetPort(const QModelIndex &index) const {
  auto portIndex = model()->index(index.row(), PortsCol, index.parent());
  QComboBox *portCombo{GetCombo(portIndex, PortsCol)};
  return (portCombo) ? portCombo->currentText() : QString{};
}

void PackagePinsView::portAssignmentChanged(const QString &port,
                                            const QString &pin) {
  if (m_blockUpdate) return;
  auto ports = m_model->getPort(pin);
  if (ports.contains(port))
    SetPort(pin, port);
  else
    SetPort(pin, QString{});
}

void PackagePinsView::portAssignmentRemoved(const QString& port) {
  for (auto it{m_allCombo.cbegin()}; it != m_allCombo.cend(); it++) {
    if (it.key()->currentText() == port) {
      it.key()->setCurrentIndex(0);
      break;
    }
  }
}

void PackagePinsView::updatePinNames() {
  for (auto &pinItem : m_pinItems) {
    auto widget = itemWidget(pinItem, NameCol);
    QLabel *label = widget->findChild<QLabel *>();
    if (label) {
      auto current{label->text()};
      auto convertedName =
          m_model->packagePinModel()->convertPinNameUsage(current);
      label->setText(convertedName);
      pinItem->setText(NameCol, convertedName);
    }
  }
}

}  // namespace FOEDAG
