#include "PartitionsListWidget.h"

#include <QVBoxLayout>
#include <QLabel>

namespace fp {

PartitionsListWidget::PartitionsListWidget(QWidget* parent)
    : QWidget(parent),
    m_list(new QListWidget)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    const int m = FP_MARGIN;
    layout->setContentsMargins(m,m,m,m);
    layout->setSpacing(m);
    layout->addWidget(new QLabel(tr("Partitions:")));
    layout->addWidget(m_list);

    m_list->setSelectionMode(QAbstractItemView::SingleSelection);

    m_list->setEditTriggers(
        QAbstractItemView::DoubleClicked |
        QAbstractItemView::EditKeyPressed
        );

    connect(m_list, &QListWidget::itemSelectionChanged,
            this, [this]{
                QList<QListWidgetItem*> items = m_list->selectedItems();
                if (!items.isEmpty()) {
                    QListWidgetItem* item = items.first();
                    const int id = getId(item->text());
                    m_selectedIdBackupOpt = id;
                    emit selectionChanged(id);
                }
            });

    connect(m_list, &QListWidget::itemChanged,
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
    m_list->clear();
    m_names2ids.clear();

    QListWidgetItem* autoSelectedItem{nullptr};
    for (const auto& [id, partition]: partitions) {
        std::string name = partition->name();
        QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(name));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        item->setData(Qt::UserRole, QString::fromStdString(name));
        m_list->addItem(item);

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
    QList<QListWidgetItem*> items = m_list->findItems(QString::fromStdString(partition->name()), Qt::MatchExactly);
    if (!items.isEmpty()) {
        QListWidgetItem* item = items.first();
        setSelectedItemSilently(item);
        m_list->scrollToItem(item);
    }
}

void PartitionsListWidget::setSelectedItemSilently(QListWidgetItem* item)
{
    blockSignals(true);
    m_list->setCurrentItem(item);
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
