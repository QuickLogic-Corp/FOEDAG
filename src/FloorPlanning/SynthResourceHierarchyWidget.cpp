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

    QObject::connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this](){
        emit selectionChanged(selectedItems());
    });
}

QStandardItem* SynthResourceHierarchyWidget::buildCategory(const QString& title,
                                                 const std::set<std::string>& items)
{
    auto* root = new QStandardItem(title);
    const Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    root->setFlags(flags);

    for (const std::string& item : items) {
        const QString s = QString::fromStdString(item);

        const QStringList parts = s.split('.');
        if (parts.isEmpty()) {
            continue;
        }

        QStandardItem* parent = root;
        for (const QString& part: parts) {
            parent = findOrCreateChild(parent, part, flags);
        }
    }

    return root;
}

void SynthResourceHierarchyWidget::loadPostSynthNetFile(const std::filesystem::path& post_synth_net_filepath)
{
    SynthResourceExtractor resourceExtractor;
    resourceExtractor.parseNetFileContent(FileUtils::GetFileContent(post_synth_net_filepath));
    const SynthResources& resources = resourceExtractor.resources();
    //resources.print();

    m_model->clear();
    m_model->setHorizontalHeaderLabels(QList<QString>() << "Resource" << "Region");

    QHeaderView* header = m_view->header();
    header->setStretchLastSection(true);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);

    // Build categories (always present, even if empty)
    QStandardItem* clbRoot  = buildCategory(QStringLiteral("CLBs"),  resources.clbs);
    QStandardItem* bramRoot = buildCategory(QStringLiteral("BRAMs"), resources.brams);
    QStandardItem* dspRoot  = buildCategory(QStringLiteral("DSPs"),  resources.dsps);

    QStandardItem* root = m_model->invisibleRootItem();

    root->appendRow(clbRoot);
    root->appendRow(bramRoot);
    root->appendRow(dspRoot);

    m_view->setUpdatesEnabled(false);
    hideLeaves(m_view, root, QModelIndex());
    m_view->setUpdatesEnabled(true);

    m_view->expandAll();
}

std::set<std::string> SynthResourceHierarchyWidget::selectedItems() const
{
    std::set<std::string> items;

    auto buildPath = [](QStandardItem* item) -> QString {
        QStringList parts;

        QStandardItem* current = item;
        while (current) {
            parts.prepend(current->text());
            current = current->parent();
        }

        return parts.join('.');
    };

    for (const QModelIndex& index: m_view->selectionModel()->selectedRows(0)) {
        QStandardItem* item = m_model->itemFromIndex(index);
        if (item == nullptr) {
            continue;
        }
        items.insert(buildPath(item).toStdString());
    }

    return items;
}

void SynthResourceHierarchyWidget::clearSelection()
{
    QItemSelectionModel* sel = m_view->selectionModel();
    if (sel) {
        QSignalBlocker blockSelection(sel);
        {
        sel->clearSelection();
        }
        m_view->viewport()->update(); // since we block selection model signal emiting we need refresh viewport manually
    }
}

void SynthResourceHierarchyWidget::setSelectedItems(int id, const std::set<std::string>& items)
{
    QItemSelectionModel* sel = m_view->selectionModel();
    if (!sel) {
        return;
    }

    QSignalBlocker blockSelection(sel);
    {
    sel->clearSelection();

    std::function<void(QStandardItem*, const std::string&)> visit = [&](QStandardItem* parent, const std::string& prefix) {
        if (!parent) {
            return;
        }

        const int rows = parent->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* item = parent->child(row, COLUMN::NETLIST);
            if (!item) {
                continue;
            }

            const std::string name = item->text().toStdString();
            const std::string fullPath = prefix.empty() ? name : (prefix + "." + name);

            if (items.find(fullPath) != items.end()) {
                QStandardItem* regionItem = parent->child(row, COLUMN::REGION);
                if (regionItem) {
                    regionItem->setText("region "+QString::number(id));
                }

                const QModelIndex idx = item->index();
                sel->select(idx, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                sel->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
            }

            if (item->hasChildren()) {
                visit(item, fullPath);
            }
        }
    };

    visit(m_model->invisibleRootItem(), "");
    }
    m_view->viewport()->update(); // since we block selection model signal emiting we need refresh viewport manually
}

QStandardItem* SynthResourceHierarchyWidget::findOrCreateChild(QStandardItem* parent,
                                 const QString& text,
                                 const Qt::ItemFlags flags)
{
    const int rows = parent->rowCount();
    for (int row = 0; row < rows; ++row) {
        QStandardItem* child = parent->child(row, COLUMN::NETLIST);
        if (child && child->text() == text) {
            return child;
        }
    }

    QStandardItem* child = new QStandardItem(text);
    child->setFlags(flags);

    QStandardItem* regionChild = new QStandardItem();

    parent->appendRow({child, regionChild});

    return child;
}

QStandardItem* SynthResourceHierarchyWidget::findRegionItem(QStandardItem* parent, const QString& text)
{
    const int rows = parent->rowCount();
    for (int row = 0; row < rows; ++row) {
        QStandardItem* child = parent->child(row, COLUMN::NETLIST);
        if (child && child->text() == text) {
            return parent->child(row, COLUMN::REGION);
        }
    }
    return nullptr;
}

void SynthResourceHierarchyWidget::hideLeaves(QTreeView* view, QStandardItem* parentItem, const QModelIndex& parentIndex)
{
    if (!parentItem) {
        return;
    }

    const int rows = parentItem->rowCount();
    for (int row = 0; row < rows; ++row) {
        QStandardItem* child = parentItem->child(row, COLUMN::NETLIST);
        if (!child) {
            continue;
        }

        const QModelIndex idx = child->index();

        const bool isLeaf = (child->rowCount() == 0);
        view->setRowHidden(row, parentIndex, isLeaf);

        if (!isLeaf) {
            hideLeaves(view, child, idx);
        }
    }
}

} // namespace FOEDAG
