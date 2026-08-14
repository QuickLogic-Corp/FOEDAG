#include "PartitionsListWidget.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>

namespace fp {

PartitionsListWidget::PartitionsListWidget(QWidget* parent)
    : QWidget(parent),
    m_tableWidget(new QTableWidget)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    const int m = FP_UI_MARGIN;
    layout->setContentsMargins(m,m,m,m);
    layout->setSpacing(m);
    layout->addWidget(new QLabel(tr("Partitions:")));
    layout->addWidget(m_tableWidget);

    m_tableWidget->setColumnCount(4);
    m_tableWidget->setHorizontalHeaderLabels({tr("Name"), tr("CLB"), tr("DSP"), tr("BRAM")});
    m_tableWidget->verticalHeader()->setVisible(false);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Name, QHeaderView::Stretch);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Clb, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Dsp, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Bram, QHeaderView::ResizeToContents);

    m_tableWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    m_tableWidget->setEditTriggers(
        QAbstractItemView::DoubleClicked |
        QAbstractItemView::EditKeyPressed
        );

    connect(m_tableWidget, &QTableWidget::itemSelectionChanged,
            this, [this]{
                const QList<QTableWidgetItem*> items = m_tableWidget->selectedItems();
                if (!items.isEmpty()) {
                    QTableWidgetItem* nameItem = m_tableWidget->item(items.first()->row(), Column::Name);
                    if (!nameItem) return;
                    const int id = getId(nameItem->text());
                    m_selectedIdBackupOpt = id;
                    emit selectionChanged(id);
                }
            });

    connect(m_tableWidget, &QTableWidget::itemChanged,
            this, [this](QTableWidgetItem* item) {
                if (!item || item->column() != Column::Name) return;

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

void PartitionsListWidget::unselectPartition()
{
    m_selectedIdBackupOpt.reset();
    m_tableWidget->clearSelection();
}

void PartitionsListWidget::onPartitionsChanged(const std::map<int, PartitionPtr>& partitions)
{
    m_tableWidget->clearContents();
    m_tableWidget->setRowCount(static_cast<int>(partitions.size()));
    m_names2ids.clear();

    int row = 0;
    QTableWidgetItem* autoSelectedItem{nullptr};
    for (const auto& [id, partition]: partitions) {
        std::string name = partition->name();

        auto* nameItem = new QTableWidgetItem(QString::fromStdString(name));
        nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);
        nameItem->setData(Qt::UserRole, QString::fromStdString(name));
        m_tableWidget->setItem(row, Column::Name, nameItem);

        // [aurora2#1725] "required/available", e.g. "180/224" -- read-only, unlike
        // the view label these are always shown, even 0/available, since a table
        // column can't disappear per-row the way free text can.
        auto resourceItem = [](int required, int available) {
            auto* item = new QTableWidgetItem(QString("%1/%2").arg(required).arg(available));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };
        m_tableWidget->setItem(row, Column::Clb, resourceItem(partition->clbRequiredCount(), partition->clbAvailableCount()));
        m_tableWidget->setItem(row, Column::Dsp, resourceItem(partition->dspRequiredCount(), partition->dspAvailableCount()));
        m_tableWidget->setItem(row, Column::Bram, resourceItem(partition->bramRequiredCount(), partition->bramAvailableCount()));

        m_names2ids[name] = id;
        if (m_selectedIdBackupOpt && (id == m_selectedIdBackupOpt.value())) {
            autoSelectedItem = nameItem;
        }
        ++row;
    }

    if (autoSelectedItem) {
        setSelectedItemSilently(autoSelectedItem);
    }
}

void PartitionsListWidget::onPartitionSelectedOutside(const PartitionPtr& partition)
{
    m_selectedIdBackupOpt = partition->id();
    const QList<QTableWidgetItem*> items = m_tableWidget->findItems(QString::fromStdString(partition->name()), Qt::MatchExactly);
    for (QTableWidgetItem* item : items) {
        if (item->column() != Column::Name) continue;
        setSelectedItemSilently(item);
        m_tableWidget->scrollToItem(item);
        break;
    }
}

void PartitionsListWidget::setSelectedItemSilently(QTableWidgetItem* item)
{
    blockSignals(true);
    m_tableWidget->setCurrentItem(item);
    // selectRow(), not item->setSelected(true): SelectRows behavior needs the whole
    // row highlighted, not just the one cell.
    m_tableWidget->selectRow(item->row());
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
