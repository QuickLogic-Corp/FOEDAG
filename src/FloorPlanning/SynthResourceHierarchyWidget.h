#pragma once

#include "Partition.h"
#include "PostSynthVerilogNameBridge.h"

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QLabel>

#include <memory>
#include <set>
#include <unordered_set>
#include <string>

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
    void build(const PostSynthVerilogNameBridge::NaturalStringSet& elements);
    void setNameBridge(std::shared_ptr<PostSynthVerilogNameBridge> bridge) { m_nameBridge = std::move(bridge); }

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
    std::shared_ptr<PostSynthVerilogNameBridge> m_nameBridge;

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
