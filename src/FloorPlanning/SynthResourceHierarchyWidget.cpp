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

#include "SynthResourceHierarchyWidget.h"
#include "SynthResourceExtractor.h"

#include "Utils/FileUtils.h"

#include <QVBoxLayout>
#include <QHeaderView>

namespace FOEDAG {
	
SynthResourceHierarchyWidget::SynthResourceHierarchyWidget(QWidget* parent)
    : QWidget(parent),
      m_view(new QTreeView(this)),
      m_model(new QStandardItemModel(this))
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    m_model->setHorizontalHeaderLabels(QList<QString>() << QStringLiteral("Resource"));
    m_view->setModel(m_model);

    // Multi-selection
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);

    // UX polish
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->header()->setStretchLastSection(true);


    QObject::connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](){
        emit selectionChanged(selectedItems());
    });
}

QStandardItem* SynthResourceHierarchyWidget::buildCategory(const QString& title,
                                                 const std::set<std::string>& items)
{
    QStandardItem* parentItem = new QStandardItem(title);
    const Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    parentItem->setFlags(flags);

    for (const std::string& item: items) {
        QStandardItem* child = new QStandardItem(QString::fromStdString(item));
        child->setFlags(flags);
        parentItem->appendRow(child);
    }
    return parentItem;
}

void SynthResourceHierarchyWidget::loadPostSynthNetFile(const std::filesystem::path& post_synth_net_filepath)
{
    SynthResourceExtractor resourceExtractor;
    resourceExtractor.parseNetFileContent(FileUtils::GetFileContent(post_synth_net_filepath));
    const SynthResources& resources = resourceExtractor.resources();
    resources.print();

    m_model->clear();
    m_model->setHorizontalHeaderLabels(QList<QString>() << QStringLiteral("Resource"));

    // Build categories (always present; even if empty)
    QStandardItem* clbRoot  = buildCategory(QStringLiteral("CLBs"),  resources.clbs);
    QStandardItem* bramRoot = buildCategory(QStringLiteral("BRAMs"), resources.brams);
    QStandardItem* dspRoot  = buildCategory(QStringLiteral("DSPs"),  resources.dsps);

    QStandardItem* root = m_model->invisibleRootItem();
    root->appendRow(clbRoot);
    root->appendRow(bramRoot);
    root->appendRow(dspRoot);

    m_view->expandAll();
}

std::set<std::string> SynthResourceHierarchyWidget::selectedItems(bool leavesOnly) const
{
    std::set<std::string> items;
    for (const QModelIndex& index: m_view->selectionModel()->selectedRows(0)) {
        QStandardItem* item = m_model->itemFromIndex(index);
        if (item == nullptr) {
            continue;
        }
        if (leavesOnly && item->hasChildren()) {
            continue;
        }
        items.insert(item->text().toStdString());
    }

    return items;
}

void SynthResourceHierarchyWidget::setSelectedItems(const std::set<std::string>& items)
{
    QItemSelectionModel* sel = m_view->selectionModel();
    if (!sel) {
        return;
    }

    QSignalBlocker blockSelection(sel);
    {
    sel->clearSelection();

    // Helper: recursively walk the model and select matches by text
    std::function<void(QStandardItem*)> visit = [&](QStandardItem* parent) {
        if (!parent) return;

        const int rows = parent->rowCount();
        for (int r = 0; r < rows; ++r) {
            QStandardItem* item = parent->child(r, 0); // column 0 (same as selectedRows(0))
            if (!item) {
                continue;
            }

            const std::string key = item->text().toStdString();
            if (items.find(key) != items.end()) {
                const QModelIndex idx = item->index(); // column 0
                sel->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                sel->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
            }


            if (item->hasChildren()) {
                visit(item);
            }
        }
    };

    visit(m_model->invisibleRootItem());
    }
    m_view->viewport()->update(); // since we block selection model signal emiting we need refresh viewport manually
}

} // namespace FOEDAG
