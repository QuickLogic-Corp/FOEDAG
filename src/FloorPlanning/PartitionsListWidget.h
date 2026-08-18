#pragma once

#include <QTableWidget>
#include <QLabel>

#include "Partition.h"

#include <unordered_map>
#include <optional>

namespace fp {

class PartitionsListWidget final : public QWidget {
    Q_OBJECT
public:
    PartitionsListWidget(QWidget* parent = nullptr);

signals:
    void selectionChanged(int);
    void partitionRenamed(int partitionId, QString newName);
    void notify(QString, QString);

public slots:
    void onPartitionsChanged(const std::map<int, PartitionPtr>& partitions);
    void onPartitionSelectedOutside(const PartitionPtr& partition);
    void unselectPartition();

private:
    // [aurora2#1725] Name (editable, drives rename/selection) + required/available
    // resource columns, read-only, one row per partition. Column::Name must stay 0:
    // rename/selection/lookup all key off the item in that column.
    enum Column { Name = 0, Clb = 1, Dsp = 2, Bram = 3 };

    QTableWidget* m_tableWidget{nullptr};

    // [aurora2#1725 stage P7] Says where the numbers above it came from. A.13.5 requires the
    // tier be surfaced: the three tiers are indistinguishable by shape, so without this a
    // pre-synthesis guess and a measured count render identically.
    QLabel* m_lbResourceSource{nullptr};
    void updateResourceSourceLabel(const std::map<int, PartitionPtr>& partitions);

    std::optional<int> m_selectedIdBackupOpt;
    int getId(const QString& name);

    std::unordered_map<std::string, int> m_names2ids;
    bool isPartitionNameUnique(const QString& candidate) const;
    void setSelectedItemSilently(QTableWidgetItem* item);
};

}  // namespace fp
