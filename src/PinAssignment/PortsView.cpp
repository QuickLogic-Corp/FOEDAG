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
#include "PortsView.h"

#include <QCompleter>
#include <QHeaderView>
#include <QStringListModel>

#include "LazyComboBox.h"
#include "PinsBaseModel.h"

namespace FOEDAG {

constexpr uint PortName{0};
constexpr uint DirCol{1};
constexpr uint PackagePinCol{2};
constexpr uint ModeCol{3};
constexpr uint InternalPinsCol{4};
constexpr uint TypeCol{5};

PortsView::PortsView(PinsBaseModel *model, QWidget *parent)
    : PinAssignmentBaseView(model, parent) {
  setHeaderLabels(model->portsModel()->headerList());
  header()->resizeSections(QHeaderView::ResizeToContents);

  m_topLevel = new QTreeWidgetItem(this);
  m_topLevel->setText(0, "Design ports");
  addTopLevelItem(m_topLevel);

  refreshContentFromModel();

  connect(model, &PinsBaseModel::pinAssignmentRemoved, this,
          &PortsView::pinAssignmentRemoved);
  connect(model, &PinsBaseModel::portAssignmentChanged, this,
          &PortsView::portAssignmentChanged);
  expandItem(m_topLevel);
  setAlternatingRowColors(true);
  setColumnWidth(PortName, 120);
  setColumnWidth(ModeCol, 180);
  setColumnWidth(InternalPinsCol, 150);
  resizeColumnToContents(PackagePinCol);
  hideColumn(ModeCol);
  hideColumn(InternalPinsCol);
}

void PortsView::refreshContentFromModel()
{
  cleanTable();
  auto portsModel = m_model->portsModel();
  for (const auto &group : portsModel->ports()) {
    for (const auto &p : group.ports) {
      if (p.isBus) {
        auto item = new QTreeWidgetItem;
        item->setText(PortName, p.name);
        m_topLevel->addChild(item);
        for (const auto &subPort : p.ports) insertTableItem(item, subPort);
      } else {
        insertTableItem(m_topLevel, p);
      }
    }
  }
}

void PortsView::SetPin(const QString &port, const QString &pin) {
  QModelIndexList indexes{match(port)};
  if (!indexes.isEmpty()) {
    auto index = indexes.first();
    setComboData(index, PackagePinCol, pin);
  }
}

void PortsView::cleanTable() {
  // the combo widget will be automatically deleted along with the QTreeWidgetItem
  m_allCombo.clear();

  QSignalBlocker blocker{this};
  while (m_topLevel->childCount() > 0) {
    QTreeWidgetItem* childItem = m_topLevel->takeChild(0);
    removeItemWidget(childItem, PackagePinCol);
    delete childItem;
  }
}

void PortsView::packagePinSelectionHasChanged(const QModelIndex &index) {
  // update here Mode selection
  auto item = itemFromIndex(index);
  if (item) {
    auto combo = GetCombo(item, PackagePinCol);
    if (combo) {
      auto pin = combo->currentText();

      auto port = item->text(PortName);
      int index = m_model->getIndex(pin);

      m_blockUpdate = true;

      m_model->update(port, pin, index);
      removeDuplications(pin, combo);
      m_blockUpdate = false;
      emit selectionHasChanged();
    }
  }
}

void PortsView::insertTableItem(QTreeWidgetItem *parent, const IOPort &port) {
  auto it = new QTreeWidgetItem{parent};
  it->setText(PortName, port.name);
  it->setText(DirCol, port.dir);
  it->setText(TypeCol, port.type);

  auto combo = new LazyComboBox{this};

  QString direction{port.dir};
  if (m_initializedDirections.contains(direction)) {
    combo->setDelayedModel(m_model->packagePinModel()->listModel(direction));
  } else {
    // We must set at least one model in a straightforward way to ensure the correct column width in the table is applied.
    // This does not impact performance but solves many issues related to incorrect table width (column with comboboxes) and combobox dropdown content width.
    combo->setModel(m_model->packagePinModel()->listModel(direction));
    m_initializedDirections.insert(direction);
  }

  combo->setAutoFillBackground(true);
  combo->setEditable(true);

  auto completer{new QCompleter{m_model->packagePinModel()->listModel(port.dir)}};

  completer->setFilterMode(Qt::MatchContains);
  combo->setCompleter(completer);
  combo->setInsertPolicy(QComboBox::NoInsert);
  connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [=]() {
            packagePinSelectionHasChanged(indexFromItem(it, PackagePinCol));
          });
  connect(combo, &QComboBox::currentTextChanged, this,
          [=]() {
            packagePinSelectionHasChanged(indexFromItem(it, PackagePinCol));
          });

  setItemWidget(it, PackagePinCol, combo);
  m_allCombo.insert(combo, indexFromItem(it));
}

QString PortsView::getPinSelection(const QModelIndex &index) const {
  auto pinIndex = model()->index(index.row(), PackagePinCol, index.parent());
  QComboBox *pinCombo{GetCombo(pinIndex, PackagePinCol)};
  return pinCombo ? pinCombo->currentText() : QString{};
}

void PortsView::portAssignmentChanged(const QString &port, const QString &pin,
                                      int /* unused */) {
  if (m_blockUpdate) return;
  if (m_model->exists(port, pin))
    SetPin(port, pin);
  else
    SetPin(port, QString{});
}

void PortsView::pinAssignmentRemoved(const QString &pin) {
  for (auto it{m_allCombo.cbegin()}; it != m_allCombo.cend(); it++) {
    if (it.key()->currentText() == pin) {
      it.key()->setCurrentIndex(0);
      break;
    }
  }
}

}  // namespace FOEDAG
