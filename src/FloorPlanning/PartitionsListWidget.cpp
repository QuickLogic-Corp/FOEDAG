#include "PartitionsListWidget.h"

namespace fp {

PartitionsListWidget::PartitionsListWidget(QWidget* parent)
    : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::SingleSelection);

    setEditTriggers(
        QAbstractItemView::DoubleClicked |
        QAbstractItemView::EditKeyPressed
        );

    connect(this, &QListWidget::itemSelectionChanged,
            this, [this]{
                QList<QListWidgetItem*> items = selectedItems();
                if (!items.isEmpty()) {
                    QListWidgetItem* item = items.first();
                    const int id = getId(item->text());
                    m_selectedIdBackupOpt = id;
                    emit selectionChanged(id);
                }
            });

    connect(this, &QListWidget::itemChanged,
            this, [this](QListWidgetItem* item) {
                QString oldText = item->data(Qt::UserRole).toString();
                QString candidate = item->text();

                if (oldText != candidate) {
                    if (isPartitionNameUnique(candidate)) {
                        item->setData(Qt::UserRole, candidate);
                        const int id = getId(oldText);
                        m_selectedIdBackupOpt = id;
                        emit partitionRenamed(id, candidate);
                    } else {
                        item->setText(oldText);
                        emit notify(QString("Suggested new partition name declined"),
                                    QString("The partition name ‘%1’ is already in use and cannot be applied. Please choose a different name.").arg(candidate));
                    }
                }
            });
}

bool PartitionsListWidget::isPartitionNameUnique(const QString& candidate) const
{
    auto it = m_names2ids.find(candidate.toStdString());
    return it == m_names2ids.end();
}

void PartitionsListWidget::onPartitionsChanged(const std::map<int, PartitionPtr>& partitions)
{
    clear();
    m_names2ids.clear();

    QListWidgetItem* autoSelectedItem{nullptr};
    for (const auto& [id, partition]: partitions) {
        std::string name = partition->name();
        QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(name));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(Qt::UserRole, QString::fromStdString(name));
        addItem(item);

        m_names2ids[name] = id;
        if (m_selectedIdBackupOpt && (id == m_selectedIdBackupOpt.value())) {
            autoSelectedItem = item;
        }
    }

    if (autoSelectedItem) {
        setSelectedItemSilently(autoSelectedItem);
    }
}

void PartitionsListWidget::onPartitionSelectedOutside(const PartitionPtr& partition)
{
    m_selectedIdBackupOpt = partition->id();
    QList<QListWidgetItem*> items = findItems(QString::fromStdString(partition->name()), Qt::MatchExactly);
    if (!items.isEmpty()) {
        QListWidgetItem* item = items.first();
        setSelectedItemSilently(item);
        scrollToItem(item);
    }
}

void PartitionsListWidget::setSelectedItemSilently(QListWidgetItem* item)
{
    blockSignals(true);
    setCurrentItem(item);
    item->setSelected(true);
    blockSignals(false);
}

int PartitionsListWidget::getId(const QString& name)
{
    auto it = m_names2ids.find(name.toStdString());
    if (it != m_names2ids.end()) {
       return it->second;
    }
    return -1;
}


} // namespace fp
