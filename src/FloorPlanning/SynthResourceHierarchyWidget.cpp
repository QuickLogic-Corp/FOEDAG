#include "SynthResourceHierarchyWidget.h"
#include "HierarhyElement.h"
#include "Utils/StringUtils.h"

#include <QStringListModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QCompleter>

#include <QDebug>

#define DISABLE_SHOW_ONLY_CHECKED_ITEMS_UI

namespace fp {

namespace {

void uncheckAllRecursive(QStandardItem* item, int col) {
    if (!item) {
        return;
    }

    const int rows = item->rowCount();
    for (int row = 0; row < rows; ++row) {
        QStandardItem* child = item->child(row, col);
        if (!child) {
            continue;
        }

        if (child->checkState() != Qt::Unchecked) {
            child->setCheckState(Qt::Unchecked);
        }

        uncheckAllRecursive(child, col);
    }
};

}

SynthResourceHierarchyWidget::SynthResourceHierarchyWidget(int flags, QWidget* parent)
    : QWidget(parent),
      m_flags(flags),
      m_view(new QTreeView(this)),
      m_model(new QStandardItemModel(this))
{
    int ls = 0;

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ls,ls,ls,ls);

    // tool bar
    QCheckBox* chShowChecked{nullptr};
    if (!isShowOnlyCheckedItems()) {
#ifndef DISABLE_SHOW_ONLY_CHECKED_ITEMS_UI
        chShowChecked = new QCheckBox("show only checked items");
        QObject::connect(chShowChecked, &QCheckBox::stateChanged, this, [this](int state){
            if (state == Qt::Checked) {
                showOnlyCheckedItems();
            } else {
                showAllItems();
            }
        });
#endif // DISABLE_SHOW_ONLY_CHECKED_ITEMS_UI
        m_leFilter = new QLineEdit;
    }

    QHBoxLayout* toolbarLayout = new QHBoxLayout;
    toolbarLayout->setContentsMargins(ls,ls,ls,ls);
    layout->addLayout(toolbarLayout);
    if (chShowChecked) {
        toolbarLayout->addWidget(chShowChecked);
    }
    if (m_leFilter) {
        toolbarLayout->addWidget(new QLabel("Filter"));
        toolbarLayout->addWidget(m_leFilter);

        QObject::connect(m_leFilter, &QLineEdit::textChanged, this, [this](const QString& pattern) {
            if (pattern.isEmpty()) {
                showAllItems();
            } else {
                showFilteredItems(pattern.toStdString());
            }
        });
    }
    //

    layout->addWidget(m_view);

    m_view->setModel(m_model);

    // UX polish
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);

    connect(m_model, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item) {
        onItemChanged(item, /*reportChanges*/true);
    });
}

void SynthResourceHierarchyWidget::build(const std::set<std::string>& elements)
{
    m_model->clear();
    if (!isPartitionsColumnHidden()) {
        m_model->setHorizontalHeaderLabels(QList<QString>() << "Netlist" << "Partitions");
    } else {
        m_model->setHorizontalHeaderLabels(QList<QString>() << "Partition netlist");
    }

    QHeaderView* header = m_view->header();
    header->setStretchLastSection(true);
    header->setSectionResizeMode(Column::Netlist, QHeaderView::ResizeToContents);
    if (!isPartitionsColumnHidden()) {
        header->setSectionResizeMode(Column::Partitions, QHeaderView::Stretch);
    }

    QSignalBlocker blocker(m_model); // prevent itemChanged spam
    for (const std::string& atom: elements) {
        addPath(atom);
    }
    blocker.unblock();

    // completer
    if (!isShowOnlyCheckedItems()) {
        QList<QString> qtElements;
        for (const std::string& atom: elements) {
            qtElements.append(QString::fromStdString(atom));
        }
        QStringListModel* model = new QStringListModel(qtElements, m_leFilter);

        QCompleter* completer = new QCompleter(model, m_leFilter);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);      // or Qt::MatchStartsWith
        completer->setCompletionMode(QCompleter::PopupCompletion);
        m_leFilter->setCompleter(completer);
    }
    //

    if (isShowOnlyCheckedItems()) {
        showOnlyCheckedItems();
    }
    m_view->viewport()->update();

    m_view->expandAll();
}

void SynthResourceHierarchyWidget::onPartitionsChanged(const std::map<int, PartitionPtr>& partitions)
{
    // intermediate data be captured in lambda
    std::map<std::string, std::string> data;
    for (const auto& [id, partition]: partitions) {
        if (!partition->elements()) {
            continue;
        }
        const std::string partitionName{partition->name()};
        for (const HierarhyElement& element: *partition->elements()) {
            if (data.find(element.path) == data.end()) {
                data[element.path] = partitionName;
            } else {
                data[element.path] += "," + partitionName;
            }
        }
    }

    const bool isPartitionsColumnVisible = !isPartitionsColumnHidden();
    std::function<void(QStandardItem*, const std::string&)> setPartitionRecursive =
        [&data, &setPartitionRecursive, isPartitionsColumnVisible](QStandardItem* item, const std::string& prefix)
    {
        if (!item) {
            return;
        }

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child) {
                continue;
            }

            const std::string name = child->text().toStdString();
            const std::string fullPath = prefix.empty() ? name : (prefix + "." + name);

            if (isPartitionsColumnVisible) {
                QStandardItem* partitionItem = item->child(row, Column::Partitions);
                if (partitionItem) {
                    if (auto it = data.find(fullPath); it != data.end()) {
                        partitionItem->setText(QString::fromStdString(it->second));
                    } else {
                        partitionItem->setText(QString());
                    }
                }
            }

            setPartitionRecursive(child, fullPath);
        }
    };

    QSignalBlocker blockModel(m_model);     // Block model signals to avoid massive itemChanged cascades
    setPartitionRecursive(m_model->invisibleRootItem(), "");
    blockModel.unblock();

    m_view->viewport()->update();
}


void SynthResourceHierarchyWidget::clearSelectedElements()
{
    QSignalBlocker blocker(m_model); // prevent itemChanged spam
    uncheckAllRecursive(m_model->invisibleRootItem(), Column::Netlist);

    if (isShowOnlyCheckedItems()) {
        showOnlyCheckedItems();
    }

    blocker.unblock();

    m_view->viewport()->update();
}

void SynthResourceHierarchyWidget::onPartitionSelected(const PartitionPtr& partition)
{
    if (!partition->elements()) {
#ifdef DEBUG_SELECTION_ELEMENTS
        qDebug() << "elements are nullptr";
#endif
        return;
    }

#ifdef DEBUG_SELECTION_ELEMENTS
    partition->elements()->print("SynthResourceHierarchyWidget::setSelectedElements ensure elements are checked");
#endif // DEBUG_SELECTION_ELEMENTS

    // check only requested elements + set partition label
    std::function<void(QStandardItem*, const std::string&)> checkAndPartitionRecursive =
        [&](QStandardItem* item, const std::string& prefix)
    {
        if (!item) {
            return;
        }

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child) {
                continue;
            }

            const std::string name = child->text().toStdString();
            const std::string fullPath = prefix.empty() ? name : (prefix + "." + name);

            if (partition->elements()->contains(fullPath)) {
                child->setCheckState(Qt::Checked);
                onItemChanged(child, /*reportChanges*/false); // update ancestor and descendant item states

                // ensure it's visible
                m_view->expand(child->index());
            }

            checkAndPartitionRecursive(child, fullPath);
        }
    };

    // Block model signals to avoid massive itemChanged cascades
    QSignalBlocker blockModel(m_model);
    uncheckAllRecursive(m_model->invisibleRootItem(), Column::Netlist);
    checkAndPartitionRecursive(m_model->invisibleRootItem(), "");

    if (isShowOnlyCheckedItems()) {
        showOnlyCheckedItems();
    }

    blockModel.unblock();

    m_view->viewport()->update();
}

HierarhyElementsPtr SynthResourceHierarchyWidget::collectSelectedElements() const
{
    static std::function<void(QStandardItem*, const std::string&, HierarhyElements&)> collectSelectedElementsRecursive =
        [](QStandardItem* item, const std::string& prefix, HierarhyElements& out)
    {
        if (!item) {
            return;
        }

        const std::string name = item->text().toStdString();
        const std::string path = prefix.empty() ? name : (prefix + "." + name);

        const Qt::CheckState st = item->isCheckable() ? item->checkState() : Qt::Unchecked;

        const int rows = item->rowCount();
        const bool isLeaf = (rows == 0);

        if (isLeaf) {
            if (st == Qt::Checked) {
                out.insert(HierarhyElement{path, true});
            }
        } else {
            if (st == Qt::Checked) {
                // if parent is checked, check whether all children are checked
                bool allChildrenChecked = true;

                for (int row = 0; row < rows; ++row) {
                    QStandardItem* child = item->child(row, Column::Netlist);
                    if (child && child->isCheckable()) {
                        if (child->checkState() != Qt::Checked) {
                            allChildrenChecked = false;
                            break;
                        }
                    }
                }

                if (allChildrenChecked) {
                    // collapse to parent path with wildcard
                    out.insert(HierarhyElement{path, false});
                } else {
                    // not all children checked -> collect deeper
                    for (int row = 0; row < rows; ++row) {
                        collectSelectedElementsRecursive(item->child(row, Column::Netlist), path, out);
                    }
                }
            } else {
                // collect deeper
                for (int row = 0; row < rows; ++row) {
                    collectSelectedElementsRecursive(item->child(row, Column::Netlist), path, out);
                }
            }
        }
    };

    HierarhyElementsPtr selectedElements = std::make_shared<HierarhyElements>();
    QStandardItem* root = m_model->invisibleRootItem();
    int rows = root->rowCount();
    for (int row=0; row<rows; ++row) {
        QStandardItem* child = root->child(row, Column::Netlist);
        if (child) {
            collectSelectedElementsRecursive(child, "", *selectedElements);
        }
    }

    return selectedElements;
}

void SynthResourceHierarchyWidget::addPath(const std::string& dottedPath)
{
    const bool isPartitionColumnVisible = !isPartitionsColumnHidden();
    std::function<QStandardItem*(QStandardItem*, const QString&)>
        findOrCreateChild = [isPartitionColumnVisible](QStandardItem* parent, const QString& text)
    {
        auto applyFlags = [](QStandardItem* item) {
            Qt::ItemFlags flags = item->flags()
            | Qt::ItemIsEnabled
            | Qt::ItemIsSelectable
            | Qt::ItemIsUserCheckable;

            item->setFlags(flags);
            if (!item->data(Qt::CheckStateRole).isValid()) {
                item->setCheckState(Qt::Unchecked);
            }
        };

        for (int row = 0; row < parent->rowCount(); ++row) {
            QStandardItem* child = parent->child(row, Column::Netlist);
            if (child && (child->text() == text)) {
                applyFlags(child);
                return child;
            }
        }

        QStandardItem* item = new QStandardItem(text);
        applyFlags(item);
        if (isPartitionColumnVisible) {
            QStandardItem* partitionItem = new QStandardItem("");
            parent->appendRow({item, partitionItem});
        } else {
            parent->appendRow(item);
        }
        return item;
    };

    QStringList parts = QString::fromStdString(dottedPath).split('.');
    if (parts.isEmpty()) {
        return;
    }

    QStandardItem* root = m_model->invisibleRootItem();
    QStandardItem* parent = root;

    for (int i = 0; i < parts.size(); ++i) {
        parent = findOrCreateChild(parent, parts[i]);
    }

    m_rawElements.insert(dottedPath);
}

void SynthResourceHierarchyWidget::onItemChanged(QStandardItem* item, bool reportChanges)
{
    static std::function<Qt::CheckState(QStandardItem*)>
        computeParentState = [](QStandardItem* item)
    {
        if (!item || item->rowCount() == 0) {
            return item ? item->checkState() : Qt::Unchecked;
        }

        bool anyChildChecked = false;
        bool anyChildUnchecked = false;

        for (int row = 0; row < item->rowCount(); ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child || !child->isCheckable()) {
                continue;
            }

            const Qt::CheckState st = child->checkState();
            if (st == Qt::PartiallyChecked) {
                return Qt::PartiallyChecked;
            }
            if (st == Qt::Checked) {
                anyChildChecked = true;
            }
            if (st == Qt::Unchecked) {
                anyChildUnchecked = true;
            }

            if (anyChildChecked && anyChildUnchecked) {
                return Qt::PartiallyChecked;
            }
        }

        if (anyChildChecked && !anyChildUnchecked) {
            return Qt::Checked;
        }
        return Qt::Unchecked;
    };

    static std::function<void(QStandardItem*, Qt::CheckState)>
        setChildrenStateRecursive = [](QStandardItem* item, Qt::CheckState st)
    {
        if (!item) {
            return;
        }

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child) {
                continue;
            }
            if (child->isCheckable()) {
                child->setCheckState(st);
            }
            setChildrenStateRecursive(child, st);
        }
    };

    if (!item || !item->isCheckable()) {
        return;
    }

    const bool isParent = (item->rowCount() > 0);
    const Qt::CheckState st = item->checkState();

    QSignalBlocker blocker(m_model);
    // if user clicked a parent checkbox -> force all descendants
    if (isParent && (st != Qt::PartiallyChecked)) {
        setChildrenStateRecursive(item, st);
    }

    // walk upward and update parent partial/checked state
    for (QStandardItem* parent = item->parent(); parent; parent = parent->parent()) {
        if (!parent->isCheckable()) {
            continue;
        }
        parent->setCheckState(computeParentState(parent));
    }

    if (isShowOnlyCheckedItems()) {
        showOnlyCheckedItems();
    }

    blocker.unblock();

    m_view->viewport()->update();

    if (reportChanges) {
        HierarhyElementsPtr elements = collectSelectedElements();
#ifdef DEBUG_SELECTION_ELEMENTS
        elements->print("SynthResourceHierarchyWidget::onItemChanged");
#endif // DEBUG_SELECTION_ELEMENTS
        emit selectedElementsChanged(elements);
    }
}

void SynthResourceHierarchyWidget::showAllItems()
{
    static std::function<void(QTreeView*, QStandardItem*, const QModelIndex&)>
        showAllRecursive = [](QTreeView* view, QStandardItem* item, const QModelIndex& parentIdx)
    {
        if (!item) {
            return;
        }

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            view->setRowHidden(row, parentIdx, false);

            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child) {
                continue;
            }

            showAllRecursive(view, child, child->index());
        }
    };

    showAllRecursive(m_view, m_model->invisibleRootItem(), QModelIndex());

    m_view->expandAll();
}

void SynthResourceHierarchyWidget::showOnlyCheckedItems()
{
    // returns true if anything under parentItem should remain visible
    static std::function<bool(QTreeView*, QStandardItem*, const QModelIndex&)>
        showOnlyCheckedRecursive = [](QTreeView* view, QStandardItem* item, const QModelIndex& parentIdx)
    {
        static std::function<bool(QStandardItem*)> isMarkedVisible = [](QStandardItem* item) {
            if (!item || !item->isCheckable()) {
                return false;
            }
            const Qt::CheckState st = item->checkState();
            // include Qt::PartiallyChecked so parent paths remain visible
            return st == Qt::Checked || st == Qt::PartiallyChecked;
        };

        if (!item) {
            return false;
        }

        bool anyVisible = false;

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child) {
                continue;
            }

            const bool descVisible = showOnlyCheckedRecursive(view, child, child->index());
            const bool selfVisible = isMarkedVisible(child);

            const bool visible = selfVisible || descVisible;

            view->setRowHidden(row, parentIdx, !visible);

            anyVisible |= visible;
        }

        return anyVisible;
    };

    showOnlyCheckedRecursive(m_view, m_model->invisibleRootItem(), QModelIndex());

    m_view->expandAll();
}

void SynthResourceHierarchyWidget::showFilteredItems(const std::string& pattern)
{
    std::unordered_set<std::string> matches;
    for (const std::string& element: m_rawElements) {
        if (FOEDAG::StringUtils::matchesWildcardPattern(element, pattern)) {
            matches.insert(element);
        }
    }

    // returns true if anything under parentItem should remain visible
    std::function<bool(QStandardItem*, const std::string&)> updateVisibility =
        [&](QStandardItem* item, const std::string& prefix) -> bool
    {
        if (!item) {
            return false;
        }

        const bool isRoot = (item == m_model->invisibleRootItem());
        const std::string name = item->text().toStdString();
        const std::string itemPath = prefix.empty() ? name : (prefix + "." + name);

        bool anyChildVisible = false;

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child) {
                continue;
            }

            // child full path
            const std::string childName = child->text().toStdString();
            const std::string childPath = itemPath.empty() ? childName : (itemPath + "." + childName);

            const bool childDescVisible = updateVisibility(child, itemPath);
            const bool childSelfVisible = matches.contains(childPath);
            const bool childVisible = childSelfVisible || childDescVisible;

            m_view->setRowHidden(row, isRoot? QModelIndex() : item->index(), !childVisible);

            anyChildVisible |= childVisible;
        }

        const bool selfVisible = (!itemPath.empty() && matches.contains(itemPath));
        return selfVisible || anyChildVisible;
    };

    QSignalBlocker blockModel(m_view);
    updateVisibility(m_model->invisibleRootItem(), "");
    blockModel.unblock();

    m_view->viewport()->update();
}

} // namespace fp
