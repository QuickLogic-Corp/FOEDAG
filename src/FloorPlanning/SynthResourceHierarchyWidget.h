#pragma once

#include "Partition.h"
#include "NaturalSort.h"

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QLabel>

#include <map>
#include <memory>
#include <set>
#include <unordered_set>
#include <string>
#include <vector>

class QLineEdit;
class QCheckBox;

namespace fp {

class CheckableButton;

class SynthResourceHierarchyWidget : public QWidget {
    Q_OBJECT

    enum Column {
      Netlist = 0,
      VprNames = 1,
      Partitions = 2
    };
public:
    enum Flag {
        None                 = 0,
        HidePartitionsColumn = 1 << 0,
        ShowOnlyCheckedItems = 1 << 1
    };

    explicit SynthResourceHierarchyWidget(int flags = Flag::None, QWidget* parent = nullptr);

    void setViewLabelTemplate(const QString& label) {
        m_viewLabelTemplate = label;
        m_lbView->setVisible(true);
        updateViewLabel();
    }
    void build(const NaturalStringSet& elements);

    // [aurora2#1725] Populates the "VPR Names" column: RTL path -> the atom names it
    // covers. No data source exists yet -- P3 (atomsets.json) is not implemented -- so
    // nothing calls this today and the column stays blank. This is the intended hook
    // for wiring that stage up once it exists.
    void setAtomNames(std::map<std::string, std::vector<std::string>, NaturalLess> atomNames);

    void onPartitionsChanged(const std::map<int, PartitionPtr>& partitions);
    void selectPartition(const PartitionPtr&);
    void unselectPartition();

 signals:
     void partitionElementsChanged(PartitionPtr);

private:
    int m_flags = Flag::None;
    CheckableButton* m_bnExpandCollapse{nullptr};
    QLineEdit* m_leFilter{nullptr};
    QString m_viewLabelTemplate;
    QLabel* m_lbView{nullptr};
    QTreeView* m_view{nullptr};
    QStandardItemModel* m_model{nullptr};

    PartitionPtr m_selectedPartition;
    std::unordered_set<std::string> m_rawElements;

    // Unset (m_hasAtomNames == false) means "no data source connected" -- the whole
    // column stays blank, nothing gets hidden. Once set() (even to an empty map, e.g.
    // atomsets.json legitimately found no atoms), a leaf with no entry is a real
    // "optimised away" case and gets hidden. Distinguishing these is why this isn't
    // just an empty-map check.
    bool m_hasAtomNames = false;
    std::map<std::string, std::vector<std::string>, NaturalLess> m_atomNames;

    void filterRawElemenets(const std::string& pattern);
    void fillPartitionWithSelectedElements(const PartitionPtr& partition) const;
    void populateVprNamesColumn();

    void addPath(const std::string&);
    void onItemChanged(QStandardItem*, bool reportChanges);

    void showFilteredItems(const std::string& pattern);
    void showOnlyCheckedItems();
    void showAllItems();

    bool isShowOnlyCheckedItems() const { return m_flags & Flag::ShowOnlyCheckedItems; }
    bool isPartitionsColumnHidden() const { return m_flags & Flag::HidePartitionsColumn; }

    void updateViewLabel() const;
};

}  // namespace fp
