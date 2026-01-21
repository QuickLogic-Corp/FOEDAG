#pragma once

#include "HierarhyElement.h"
#include "Partition.h"

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>

#include <set>
#include <string>

class QLineEdit;
class QCheckBox;

namespace fp {

class SynthResourceHierarchyWidget : public QWidget {
    Q_OBJECT

    enum Column {
      Netlist = 0,
      Partitions
    };
public:
    enum Flag {
        None                 = 0,
        HidePartitionsColumn    = 1 << 0,
        ShowOnlyCheckedItems = 1 << 1
    };

    explicit SynthResourceHierarchyWidget(int flags = Flag::None, QWidget* parent = nullptr);

    void build(const std::set<std::string>& elements);

    void onPartitionsChanged(const std::map<int, PartitionPtr>& partitions);
    void onPartitionSelected(const PartitionPtr&);
    void clearSelectedElements();

signals:
    void selectedElementsChanged(HierarhyElementsPtr);

private:
    int m_flags = Flag::None;
    QLineEdit* m_leFilter{nullptr};
    QTreeView* m_view{nullptr};
    QStandardItemModel* m_model{nullptr};

    std::set<std::string> m_rawElements;

    void filterRawElemenets(const std::string& pattern);

    void addPath(const std::string&);
    void onItemChanged(QStandardItem*, bool reportChanges);

    void showFilteredItems(const std::string& pattern);
    void showOnlyCheckedItems();
    void showAllItems();

    HierarhyElementsPtr collectSelectedElements() const;

    bool isShowOnlyCheckedItems() const { return m_flags & Flag::ShowOnlyCheckedItems; }
    bool isPartitionsColumnHidden() const { return m_flags & Flag::HidePartitionsColumn; }
};

}  // namespace fp
