#pragma once

#include "AtomSets.h"

#include <QWidget>

#include <map>
#include <string>

class QLabel;
class QTabWidget;
class QTableWidget;

namespace fp {

// [aurora2#1725] "Selected RTL Resources": what the instances selected in the RTL hierarchy
// tree would cost, in the same units a partition's row reports. Sits under that tree and
// answers "how big does this have to be?" without the user having to create a partition,
// check the instances into it and read the number back off the partitions table. Driven by
// selection, not the checkboxes: checking an instance assigns it to a partition and changes
// the floorplan, whereas selecting is just pointing at something.
//
// Two tabs, because the answer has two units and neither substitutes for the other. Atoms
// opens first, since it is the exact figure; Tiles is the estimate:
//
//   Tiles  what the floorplan is drawn in -- CLB/DSP/BRAM. The CLB figure is an estimate
//          (atoms over a packing density), and it is the only estimate in this widget.
//   Atoms  what the netlist actually holds -- $lut, sdffre, adder_carry, TDP_ECC36K_...,
//          named as synthesis named them, and exact.
class SelectedResourcesWidget final : public QWidget {
    Q_OBJECT
public:
    explicit SelectedResourcesWidget(QWidget* parent = nullptr);

    // `instances` is how many tree rows the tally covers -- shown alongside it, because "0
    // CLB" means something different for an empty selection than for a selected instance
    // that synthesis emptied.
    void setSelectedResources(const ResourceTally& tally,
                              const std::map<std::string, int>& atomResources,
                              int instances);

    // Nothing selected: zeroes, not a stale answer from the previous selection.
    void clear();

private:
    enum TileRow { Clb = 0, Dsp = 1, Bram = 2, TileRowCount = 3 };
    enum AtomColumn { Type = 0, Count = 1 };

    QLabel* m_lbSelection{nullptr};
    QTabWidget* m_tabs{nullptr};
    QTableWidget* m_tiles{nullptr};
    QTableWidget* m_atoms{nullptr};

    void setTileValue(TileRow row, int value, const QString& tip);
    void setAtomRows(const std::map<std::string, int>& atomResources);
};

}  // namespace fp
