#pragma once

#include <QTableWidget>
#include <QLabel>

#include "Partition.h"

#include <set>
#include <unordered_map>
#include <optional>

namespace fp {

class PartitionsListWidget final : public QWidget {
    Q_OBJECT
public:
    PartitionsListWidget(QWidget* parent = nullptr);

    // [aurora2#1725] Wide enough for every column, so the pane opens showing all of them.
    // A preference, not a floor: the splitter may hand it less and the table scrolls.
    QSize sizeHint() const override;

signals:
    void selectionChanged(int);
    void partitionRenamed(int partitionId, QString newName);
    void notify(QString, QString);

    // [aurora2#1725] Create/delete live on the rows they act on, rather than on the device
    // toolbar where the target was whatever happened to be selected.
    void newPartitionRequested();
    void partitionRemoveRequested(int partitionId);

public slots:
    void onPartitionsChanged(const std::map<int, PartitionPtr>& partitions);
    void onPartitionSelectedOutside(const PartitionPtr& partition);
    void unselectPartition();

private:
    // [aurora2#1725] Name (editable, drives rename/selection) + required/available
    // resource columns, read-only, one row per partition. Column::Name must stay 0:
    // rename/selection/lookup all key off the item in that column.
    // Placed is the tier-2 measurement -- CLB tiles the placer actually used -- kept in its
    // own column rather than folded into Clb, because required and placed are different
    // questions and A.13.5 forbids rendering an estimate and a measurement identically.
    // Remove is last so the existing indices are untouched; it holds one delete button per
    // partition row.
    enum Column { Name = 0, Clb = 1, Dsp = 2, Bram = 3, Placed = 4, Remove = 5 };

    QTableWidget* m_tableWidget{nullptr};

    // [aurora2#1725 stage P7] Says where the numbers above it came from. A.13.5 requires the
    // tier be surfaced: the two tiers are indistinguishable by shape, so without this a
    // packing-density estimate and a measured tile count render identically.
    QLabel* m_lbResourceSource{nullptr};
    void updateResourceSourceLabel(const std::map<int, PartitionPtr>& partitions);

    // [aurora2#1725] Keeps the pane from being handed less width than its columns need,
    // which is what hid the rightmost ones until the window was widened. Feeds sizeHint()
    // rather than a minimum, so the pane stays draggable.
    void updateTablePreferredWidth();
    int m_preferredWidth = 0;

    // [aurora2#1725] Set when the "+" row asks for a partition, so the next repopulate can
    // put the new row's name straight into edit mode instead of a modal name prompt. The ids
    // present beforehand are what identifies the new one.
    bool m_editNewRowOnRefresh = false;
    std::set<int> m_idsBeforeCreate;

    std::optional<int> m_selectedIdBackupOpt;
    int getId(const QString& name);

    std::unordered_map<std::string, int> m_names2ids;
    bool isPartitionNameUnique(const QString& candidate) const;
    void setSelectedItemSilently(QTableWidgetItem* item);

    // The trailing row that only carries the "+" button; never a partition.
    int newPartitionRow() const { return m_tableWidget->rowCount() - 1; }
    void buildNewPartitionRow(int row);
    QWidget* buildRemoveButton(int partitionId, const QString& name, bool wellFormed);
};

}  // namespace fp
