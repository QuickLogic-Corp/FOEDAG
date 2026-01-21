#pragma once

#include <QListWidget>

#include "Partition.h"

#include <unordered_map>
#include <optional>

namespace fp {

class PartitionsListWidget final : public QListWidget {
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

private:
    std::optional<int> m_selectedIdBackupOpt;
    int getId(const QString& name);

    std::unordered_map<std::string, int> m_names2ids;
    bool isPartitionNameUnique(const QString& candidate) const;
    void setSelectedItemSilently(QListWidgetItem* item);
};

}  // namespace fp
