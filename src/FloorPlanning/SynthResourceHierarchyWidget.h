#pragma once

#include "HierarhyElement.h"
#include "Region.h"

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

    enum COLUMN {
      NETLIST = 0,
      REGION
    };
public:
    explicit SynthResourceHierarchyWidget(QWidget* parent = nullptr);

    void build(const std::set<std::string>& elements);

    void onRegionsChanged(const std::map<int, RegionPtr>& regions);
    void onRegionSelected(const RegionPtr&);
    void clearSelectedElements();

signals:
    void selectedElementsChanged(HierarhyElementsPtr);

private:
    QLineEdit* m_leFilter{nullptr};
    QCheckBox* m_bnFilter{nullptr};
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
};

}  // namespace fp
