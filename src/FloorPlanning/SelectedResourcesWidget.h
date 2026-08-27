#pragma once

#include "AtomSets.h"

#include <QWidget>

class QLabel;
class QTableWidget;

namespace fp {

// [aurora2#1725] "Selected RTL Resources": what the instances selected in the RTL hierarchy
// tree would cost, in the same units a partition's row reports.
//
// Sits under that tree and answers the question a user asks before drawing a region -- "how
// big does this have to be?" -- without them having to create a partition, check the
// instances into it and read the number back off the partitions table. Selection, not the
// checkboxes: checking an instance assigns it to a partition, which is a change to the
// floorplan; selecting is just pointing at something.
//
// Three rows, CLB/DSP/BRAM, one value column. The figures come from tallyResources(), the
// same arithmetic behind the partitions table, so the two cannot disagree.
class SelectedResourcesWidget final : public QWidget {
    Q_OBJECT
public:
    explicit SelectedResourcesWidget(QWidget* parent = nullptr);

    // `instances` is how many tree rows the tally covers -- shown alongside it, because "0
    // CLB" means something different for an empty selection than for a selected instance
    // that synthesis emptied.
    void setTally(const ResourceTally& tally, int instances);

    // Nothing selected: zeroes, not a stale answer from the previous selection.
    void clear();

private:
    enum Row { Clb = 0, Dsp = 1, Bram = 2, RowCount = 3 };

    QLabel* m_lbSelection{nullptr};
    QTableWidget* m_table{nullptr};

    void setValue(Row row, int value, const QString& tip);
};

}  // namespace fp
