#include "SynthResourceHierarchyWidget.h"
#include "HierarhyElement.h"
#include "Utils/StringUtils.h"

#include <QStringListModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QHeaderView>
#include <QCheckBox>
#include <QCompleter>

#include <QDebug>

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

SynthResourceHierarchyWidget::SynthResourceHierarchyWidget(QWidget* parent)
    : QWidget(parent),
      m_view(new QTreeView(this)),
      m_model(new QStandardItemModel(this))
{
    int ls = 1;

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(ls,ls,ls,ls);

    // tool bar
    QCheckBox* chShowChecked = new QCheckBox("show only checked items");
    QObject::connect(chShowChecked, &QCheckBox::stateChanged, this, [this](int state){
        if (state == Qt::Checked) {
            showOnlyCheckedItems();
        } else {
            showAllItems();
        }
    });

    m_leFilter = new QLineEdit;

    m_bnFilter = new QCheckBox("Filter");

    QHBoxLayout* toolbarLayout = new QHBoxLayout;
    toolbarLayout->setContentsMargins(ls,ls,ls,ls);
    layout->addLayout(toolbarLayout);
    toolbarLayout->addWidget(chShowChecked);
    toolbarLayout->addWidget(m_leFilter);
    toolbarLayout->addWidget(m_bnFilter);

    QObject::connect(m_leFilter, &QLineEdit::textChanged, this, [this](const QString& pattern) {
        if (m_bnFilter->isChecked()) {
            showFilteredItems(pattern.toStdString());
        }
    });
    QObject::connect(m_bnFilter, &QCheckBox::stateChanged, this, [this](int state){
        if (state == Qt::Checked) {
            showFilteredItems(m_leFilter->text().toStdString());
        } else {
            showAllItems();
        }
    });
    //

    layout->addWidget(m_view);

    m_model->setHorizontalHeaderLabels(QList<QString>() << QStringLiteral("Resource"));
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
    m_model->setHorizontalHeaderLabels(QList<QString>() << "Resource" << "Regions");

    QHeaderView* header = m_view->header();
    header->setStretchLastSection(true);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);

    QSignalBlocker blocker(m_model); // prevent itemChanged spam
    for (const std::string& atom: elements) {
        addPath(atom);
    }
    blocker.unblock();

    // completer
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
    //

    m_view->viewport()->update();

    m_view->expandAll();
}

void SynthResourceHierarchyWidget::onRegionsChanged(const std::map<int, RegionPtr>& regions)
{
    // intermediate data be captured in lambda
    std::map<std::string, std::string> data;
    for (const auto& [id, region]: regions) {
        if (!region->elements()) {
            continue;
        }
        for (const HierarhyElement& element: *region->elements()) {
            if (data.find(element.path) == data.end()) {
                data[element.path] = std::to_string(id);
            } else {
                data[element.path] += "," + std::to_string(id);
            }
        }
    }

    std::function<void(QStandardItem*, const std::string&)> setRegionRecursive =
        [&data, &setRegionRecursive](QStandardItem* item, const std::string& prefix)
    {
        if (!item) {
            return;
        }

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, COLUMN::NETLIST);
            if (!child) {
                continue;
            }

            const std::string name = child->text().toStdString();
            const std::string fullPath = prefix.empty() ? name : (prefix + "." + name);

            QStandardItem* regionItem = item->child(row, COLUMN::REGION);
            if (regionItem) {
                if (auto it = data.find(fullPath); it != data.end()) {
                    regionItem->setText(QString::fromStdString(it->second));
                } else {
                    regionItem->setText(QString());
                }
            }

            setRegionRecursive(child, fullPath);
        }
    };

    QSignalBlocker blockModel(m_model);     // Block model signals to avoid massive itemChanged cascades
    setRegionRecursive(m_model->invisibleRootItem(), "");
    blockModel.unblock();

    m_view->viewport()->update();
}


void SynthResourceHierarchyWidget::clearSelectedElements()
{
    QSignalBlocker blocker(m_model); // prevent itemChanged spam
    uncheckAllRecursive(m_model->invisibleRootItem(), COLUMN::NETLIST);
    blocker.unblock();

    m_view->viewport()->update();
}

void SynthResourceHierarchyWidget::onRegionSelected(const RegionPtr& region)
{
    if (!region->elements()) {
        qInfo() << "elements are nullptr";
        return;
    }

    if (region->elements()->empty()) {
        qInfo() << "elements are empty";
        return;
    }

    region->elements()->print("SynthResourceHierarchyWidget::setSelectedElements ensure elements are checked");

    // check only requested elements + set region label
    std::function<void(QStandardItem*, const std::string&)> checkAndRegionRecursive =
        [&](QStandardItem* item, const std::string& prefix)
    {
        if (!item) {
            return;
        }

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, COLUMN::NETLIST);
            if (!child) {
                continue;
            }

            const std::string name = child->text().toStdString();
            const std::string fullPath = prefix.empty() ? name : (prefix + "." + name);

            if (region->elements()->contains(fullPath)) {
                child->setCheckState(Qt::Checked);
                onItemChanged(child, /*reportChanges*/false); // update ancestor and descendant item states

                // ensure it's visible
                m_view->expand(child->index());
            }

            checkAndRegionRecursive(child, fullPath);
        }
    };

    // Block model signals to avoid massive itemChanged cascades
    QSignalBlocker blockModel(m_model);
    uncheckAllRecursive(m_model->invisibleRootItem(), COLUMN::NETLIST);
    checkAndRegionRecursive(m_model->invisibleRootItem(), "");
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
                    QStandardItem* child = item->child(row, COLUMN::NETLIST);
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
                        collectSelectedElementsRecursive(item->child(row, COLUMN::NETLIST), path, out);
                    }
                }
            } else {
                // collect deeper
                for (int row = 0; row < rows; ++row) {
                    collectSelectedElementsRecursive(item->child(row, COLUMN::NETLIST), path, out);
                }
            }
        }
    };

    HierarhyElementsPtr selectedElements = std::make_shared<HierarhyElements>();
    QStandardItem* root = m_model->invisibleRootItem();
    int rows = root->rowCount();
    for (int row=0; row<rows; ++row) {
        QStandardItem* child = root->child(row, COLUMN::NETLIST);
        if (child) {
            collectSelectedElementsRecursive(child, "", *selectedElements);
        }
    }

    return selectedElements;
}

void SynthResourceHierarchyWidget::addPath(const std::string& dottedPath)
{
    static std::function<QStandardItem*(QStandardItem*, const QString&)>
        findOrCreateChild = [](QStandardItem* parent, const QString& text)
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
            QStandardItem* child = parent->child(row, COLUMN::NETLIST);
            if (child && (child->text() == text)) {
                applyFlags(child);
                return child;
            }
        }

        QStandardItem* item = new QStandardItem(text);
        applyFlags(item);
        QStandardItem* regionItem = new QStandardItem("");
        parent->appendRow({item, regionItem});
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
            QStandardItem* child = item->child(row, COLUMN::NETLIST);
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
            QStandardItem* child = item->child(row, COLUMN::NETLIST);
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
    if (isParent && st != Qt::PartiallyChecked) {
        setChildrenStateRecursive(item, st);
    }

    // walk upward and update parent partial/checked state
    for (QStandardItem* parent = item->parent(); parent; parent = parent->parent()) {
        if (!parent->isCheckable()) {
            continue;
        }
        parent->setCheckState(computeParentState(parent));
    }
    blocker.unblock();

    m_view->viewport()->update();

    if (reportChanges) {
        HierarhyElementsPtr elements = collectSelectedElements();
        elements->print("SynthResourceHierarchyWidget::onItemChanged");
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

            QStandardItem* child = item->child(row, COLUMN::NETLIST);
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
            QStandardItem* child = item->child(row, COLUMN::NETLIST);
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
            QStandardItem* child = item->child(row, COLUMN::NETLIST);
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
