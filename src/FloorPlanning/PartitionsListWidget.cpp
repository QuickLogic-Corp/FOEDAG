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

    // [aurora2#1725 stage P7] Sits under the table and names the source of the CLB/DSP/BRAM
    // figures. Filled by updateResourceSourceLabel() on every repopulate.
    m_lbResourceSource = new QLabel;
    m_lbResourceSource->setWordWrap(true);
    m_lbResourceSource->setVisible(false);
    layout->addWidget(m_lbResourceSource);

    m_tableWidget->setColumnCount(5);
    m_tableWidget->setHorizontalHeaderLabels(
        {tr("Name"), tr("CLB"), tr("DSP"), tr("BRAM"), tr("Placed")});
    m_tableWidget->verticalHeader()->setVisible(false);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Name, QHeaderView::Stretch);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Clb, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Dsp, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Bram, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Placed, QHeaderView::ResizeToContents);

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

        // [aurora2#1725] "required/available" tiles, e.g. "180/224" -- read-only, unlike
        // the view label these are always shown, even 0/available, since a table
        // column can't disappear per-row the way free text can.
        //
        // [aurora2#1725 stage P7] A partition whose counts came from a tier-1 estimate
        // rather than from measured atoms is marked "~180/224" and says so on hover. The
        // three tiers share a shape, so without this the panel would show a pre-synthesis
        // guess in the same cell, in the same format, as a measurement (A.13.5).
        const bool estimated = partition->isEstimated();
        auto resourceItem = [estimated](int required, int available) {
            auto* item = new QTableWidgetItem(QString("%1%2/%3")
                                                  .arg(estimated ? "~" : "")
                                                  .arg(required)
                                                  .arg(available));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setToolTip(estimated
                ? QObject::tr("Tier 1 -- pre-synthesis estimate, no netlist exists yet. DSP is "
                              "exact, CLB is approximate, BRAM is not estimated at all. "
                              "Synthesize for measured figures.")
                : QObject::tr("Tier 2 -- counted from the post-synthesis netlist. The CLB "
                              "figure is still packing-density derived, so it is a sizing "
                              "hint rather than the tile count the placer will use."));
            return item;
        };
        m_tableWidget->setItem(row, Column::Clb, resourceItem(partition->clbRequiredCount(), partition->clbAvailableCount()));
        m_tableWidget->setItem(row, Column::Dsp, resourceItem(partition->dspRequiredCount(), partition->dspAvailableCount()));
        m_tableWidget->setItem(row, Column::Bram, resourceItem(partition->bramRequiredCount(), partition->bramAvailableCount()));

        // [aurora2#1725 stage P7] Tier 3: CLB tiles the placer actually used. Blank until a
        // placement exists, because there is no honest number to show before then.
        const Partition::ResourceContribution contribution = partition->resourceContribution();
        auto* placedItem = new QTableWidgetItem();
        placedItem->setFlags(placedItem->flags() & ~Qt::ItemIsEditable);
        if (contribution.hasActual) {
            // ">=" when a contributing instance shares clusters with logic outside its own
            // subtree: those tiles are counted for both, so the sum is an upper bound.
            const QString prefix = contribution.actualShared ? QString::fromUtf8("\u2265") : QString();
            placedItem->setText(prefix + QString::number(contribution.clbActual));
            QString tip = QObject::tr("Tier 3 -- CLB tiles this partition actually occupies, "
                                      "from the placement.");
            if (contribution.actualShared) {
                tip += QObject::tr(" At least one instance shares clusters with logic outside "
                                   "it, so those tiles are counted for both and this is an "
                                   "upper bound.");
            }
            if (contribution.actualPartial) {
                tip += QObject::tr(" Some elements of this partition have no measurement, so "
                                   "the total is incomplete.");
            }
            placedItem->setToolTip(tip);
        } else {
            placedItem->setText(QStringLiteral("-"));
            placedItem->setToolTip(QObject::tr(
                "No placement measurement yet. Run placement to compare what this partition "
                "needs against the tiles it actually occupies."));
        }
        m_tableWidget->setItem(row, Column::Placed, placedItem);

        m_names2ids[name] = id;
        if (m_selectedIdBackupOpt && (id == m_selectedIdBackupOpt.value())) {
            autoSelectedItem = nameItem;
        }
        ++row;
    }

    if (autoSelectedItem) {
        setSelectedItemSilently(autoSelectedItem);
    }

    updateResourceSourceLabel(partitions);
}

// [aurora2#1725 stage P7] Name the source of the numbers in the table.
//
// Deliberately reports the tier of the CLB/DSP/BRAM columns, which is not the same as the
// tier of design_resources.json. Those columns are fed by the atom-based tally, so a row is
// tier 2 whenever it has atoms; the tier-1 estimate only fills in for rows that have none.
// A tier-3 file on disk does NOT make them tier 3: claiming placed-tile accuracy for a
// packing-density estimate is exactly the confusion A.13.5 exists to prevent.
//
// The tier-3 measurement has its own column, Placed, and is named separately below --
// keeping the two apart is the whole reason it is a column rather than a third number
// folded into CLB.
void PartitionsListWidget::updateResourceSourceLabel(const std::map<int, PartitionPtr>& partitions)
{
    int estimatedRows = 0;
    for (const auto& [id, partition]: partitions) {
        if (partition->isEstimated()) ++estimatedRows;
    }
    const int total = static_cast<int>(partitions.size());

    if (total == 0) {
        m_lbResourceSource->setVisible(false);
        return;
    }

    QString text;
    if (estimatedRows == 0) {
        text = tr("Resources: tier 2 — counted from the post-synthesis netlist.");
    } else if (estimatedRows == total) {
        text = tr("Resources: tier 1 — pre-synthesis estimate (~). "
                  "DSP exact, CLB approximate, BRAM not estimated. Synthesize to measure.");
    } else {
        text = tr("Resources: mixed — %1 of %2 partition(s) are a pre-synthesis estimate (~); "
                  "the rest are counted from the post-synthesis netlist.")
                   .arg(estimatedRows).arg(total);
    }

    // Only ever a hint, at any tier: CLB required is atoms divided by packing density, not
    // the tile count the placer will settle on. "Placed" is where the real figure lives.
    text += tr("  CLB is a sizing hint in tiles, not a placement result.");
    if (Partition::designResources().tier >= 3) {
        text += tr("  Placed shows the tier-3 measurement from the placement.");
    }

    m_lbResourceSource->setText(text);
    m_lbResourceSource->setVisible(true);
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
