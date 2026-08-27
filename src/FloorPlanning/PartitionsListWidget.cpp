#include "PartitionsListWidget.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QStyle>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QToolButton>

#include <array>

namespace fp {

namespace {

// [aurora2#1725] Room for a name of about this length before the Name column starts
// eliding. Measured rather than written down in pixels, so it still holds under a
// different font, style or DPI.
constexpr auto kNameWidthSample = "partition_00";

// The horizontal text margin a table cell puts either side of its text.
constexpr int kCellTextMargin = 12;

}  // namespace

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

    m_tableWidget->setColumnCount(6);
    m_tableWidget->setHorizontalHeaderLabels(
        {tr("Name"), tr("CLB"), tr("DSP"), tr("BRAM"), tr("Placed"), QString()});

    // [aurora2#1725 stage P7] The cells explain their own source, but only once you know
    // which cell to hover. Say what each column IS on the header, so the difference between
    // the three "required/available" columns and the measured one is readable at a glance.
    const std::array<std::pair<int, QString>, 4> headerTips{{
        {Column::Clb, tr("Required / available CLB tiles.\n\n"
                         "Required is a sizing hint, not a placement result: clb atoms are\n"
                         "luts and flops that pack many to a tile, so the figure is an atom\n"
                         "count divided by a packing density.")},
        {Column::Dsp, tr("Required / available DSP tiles.\n\n"
                         "One DSP atom is one tile, so unlike CLB this needs no packing\n"
                         "estimate.")},
        {Column::Bram, tr("Required / available BRAM tiles.\n\n"
                          "One BRAM atom is one tile.")},
        {Column::Placed, tr("CLB tiles this partition ACTUALLY occupies, from the placement.\n\n"
                            "The measured counterpart to the CLB column's estimate — compare the\n"
                            "two to see how well a region was sized.\n\n"
                            "≥ means at least one instance shares clusters with logic outside it,\n"
                            "so those tiles are counted for both and the total is an upper bound.\n"
                            "A dash means placement has not run yet: it is not shown as 0,\n"
                            "because \"not measured\" is not \"measured zero\".")},
    }};
    for (const auto& [column, tip]: headerTips) {
        if (auto* header = m_tableWidget->horizontalHeaderItem(column)) {
            header->setToolTip(tip);
        }
    }
    m_tableWidget->verticalHeader()->setVisible(false);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Name, QHeaderView::Stretch);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Clb, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Dsp, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Bram, QHeaderView::ResizeToContents);
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Placed, QHeaderView::ResizeToContents);
    // Fixed, not ResizeToContents: the cell holds a widget, and section auto-sizing measures
    // items rather than cell widgets, so the button would be given a column too narrow for it.
    m_tableWidget->horizontalHeader()->setSectionResizeMode(Column::Remove, QHeaderView::Fixed);
    m_tableWidget->horizontalHeader()->resizeSection(
        Column::Remove, m_tableWidget->style()->pixelMetric(QStyle::PM_SmallIconSize) + 16);

    // [aurora2#1725] Before any partitions exist the header text alone decides the width;
    // onPartitionsChanged() re-measures once there are rows.
    updateTablePreferredWidth();

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
                    const int row = items.first()->row();
                    if (row == newPartitionRow()) return;   // the "+" row is not a partition
                    QTableWidgetItem* nameItem = m_tableWidget->item(row, Column::Name);
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

// [aurora2#1725] The delete button on a partition's own row. Confirmation is asked only for
// a partition that holds something -- regions and elements are work the user would lose. An
// empty partition is a placeholder, and a prompt for one is just an extra click.
QWidget* PartitionsListWidget::buildRemoveButton(int partitionId, const QString& name,
                                                 bool wellFormed)
{
    QToolButton* button = new QToolButton;
    button->setIcon(QIcon(":/delete-10402_32.png"));   // trash bin: this deletes, not clears
    button->setAutoRaise(true);   // flat until hovered, so a column of these is not noisy
    button->setToolTip(tr("Delete partition '%1'").arg(name));

    connect(button, &QToolButton::clicked, this, [this, partitionId, name, wellFormed] {
        if (wellFormed) {
            const QMessageBox::StandardButton answer = QMessageBox::question(
                this, tr("Delete partition"),
                tr("Delete partition '%1'?\n\nIts regions and the instances assigned to it "
                   "are removed with it.").arg(name),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (answer != QMessageBox::Yes) return;
        }
        emit partitionRemoveRequested(partitionId);
    });

    // Centred in the cell: a button stretched across the column looks like a banner.
    QWidget* host = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(button, 0, Qt::AlignCenter);
    return host;
}

// [aurora2#1725] The trailing row: "+ New partition". A row rather than a toolbar button, so
// creating one reads as adding to this list, and so the new name is typed in the Name column
// (see onPartitionsChanged) instead of in a modal dialog before the partition exists.
void PartitionsListWidget::buildNewPartitionRow(int row)
{
    for (int column = Column::Clb; column <= Column::Remove; ++column) {
        auto* filler = new QTableWidgetItem();
        filler->setFlags(Qt::NoItemFlags);   // not selectable, not editable, not a partition
        m_tableWidget->setItem(row, column, filler);
    }
    auto* nameCell = new QTableWidgetItem();
    nameCell->setFlags(Qt::NoItemFlags);
    m_tableWidget->setItem(row, Column::Name, nameCell);

    QToolButton* button = new QToolButton;
    button->setIcon(QIcon(":/add.png"));
    button->setText(tr("New partition"));
    button->setToolTipDuration(-1);
    button->setToolTip(tr("Create a partition and name it here"));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(true);
    connect(button, &QToolButton::clicked, this, [this] {
        // Remember what exists now: the repopulate that follows identifies the new partition
        // as the one id that was not here before, and starts editing its name.
        m_idsBeforeCreate.clear();
        for (const auto& [name, id] : m_names2ids) {
            (void)name;
            m_idsBeforeCreate.insert(id);
        }
        m_editNewRowOnRefresh = true;
        emit newPartitionRequested();
    });

    QWidget* host = new QWidget;
    QHBoxLayout* layout = new QHBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(button, 0, Qt::AlignLeft);
    m_tableWidget->setCellWidget(row, Column::Name, host);
}

void PartitionsListWidget::onPartitionsChanged(const std::map<int, PartitionPtr>& partitions)
{
    m_tableWidget->clearContents();
    // +1 for the trailing "New partition" row, which is not a partition and is skipped by
    // everything that walks rows looking for one.
    m_tableWidget->setRowCount(static_cast<int>(partitions.size()) + 1);
    m_names2ids.clear();

    int row = 0;
    QTableWidgetItem* autoSelectedItem{nullptr};
    QTableWidgetItem* newlyCreatedItem{nullptr};
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
        // [aurora2#1725] Every figure here is counted from the post-synthesis netlist --
        // the panel does not open before synthesis, so there is no pre-synthesis estimate
        // to distinguish these from and no "~" marker to carry.
        auto resourceItem = [](int required, int available) {
            auto* item = new QTableWidgetItem(QString("%1/%2").arg(required).arg(available));
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            item->setToolTip(
                QObject::tr("Counted from the post-synthesis netlist. The CLB figure is still\n"
                            "packing-density derived, so it is a sizing hint rather than the tile\n"
                            "count the placer will use."));
            return item;
        };
        m_tableWidget->setItem(row, Column::Clb, resourceItem(partition->clbRequiredCount(), partition->clbAvailableCount()));
        m_tableWidget->setItem(row, Column::Dsp, resourceItem(partition->dspRequiredCount(), partition->dspAvailableCount()));
        m_tableWidget->setItem(row, Column::Bram, resourceItem(partition->bramRequiredCount(), partition->bramAvailableCount()));

        // [aurora2#1725 stage P7] Tier 2: CLB tiles the placer actually used. Blank until a
        // placement exists, because there is no honest number to show before then.
        const Partition::ResourceContribution contribution = partition->resourceContribution();
        auto* placedItem = new QTableWidgetItem();
        placedItem->setFlags(placedItem->flags() & ~Qt::ItemIsEditable);
        if (contribution.hasActual) {
            // ">=" when a contributing instance shares clusters with logic outside its own
            // subtree: those tiles are counted for both, so the sum is an upper bound.
            const QString prefix = contribution.actualShared ? QString::fromUtf8("\u2265") : QString();
            placedItem->setText(prefix + QString::number(contribution.clbActual));
            QString tip = QObject::tr("Measured from the placement -- CLB tiles this\n"
                                      "partition actually occupies.");
            if (contribution.actualShared) {
                tip += QObject::tr("\n"
                                   "At least one instance shares clusters with logic outside it, so\n"
                                   "those tiles are counted for both and this is an upper bound.");
            }
            if (contribution.actualPartial) {
                tip += QObject::tr("\n"
                                   "Some elements of this partition have no measurement, so\n"
                                   "the total is incomplete.");
            }
            placedItem->setToolTip(tip);
        } else {
            placedItem->setText(QStringLiteral("-"));
            placedItem->setToolTip(QObject::tr(
                "No placement measurement yet. Run placement to compare what this\n"
                "partition needs against the tiles it actually occupies."));
        }
        m_tableWidget->setItem(row, Column::Placed, placedItem);

        // Well formed = it holds something worth confirming the loss of.
        const bool wellFormed = !partition->regions().empty() && !partition->elements().empty();
        m_tableWidget->setCellWidget(
            row, Column::Remove,
            buildRemoveButton(id, QString::fromStdString(name), wellFormed));

        m_names2ids[name] = id;
        if (m_selectedIdBackupOpt && (id == m_selectedIdBackupOpt.value())) {
            autoSelectedItem = nameItem;
        }
        if (m_editNewRowOnRefresh && !m_idsBeforeCreate.count(id)) {
            newlyCreatedItem = nameItem;
        }
        ++row;
    }

    buildNewPartitionRow(row);

    if (autoSelectedItem) {
        setSelectedItemSilently(autoSelectedItem);
    }

    // [aurora2#1725] A partition just created from the "+" row: select it and open its name
    // for editing, which is where the user types the name that used to be asked for up front.
    // The default the Partition constructor assigned ("partition<n>") stands if they leave it.
    if (m_editNewRowOnRefresh) {
        m_editNewRowOnRefresh = false;
        if (newlyCreatedItem) {
            setSelectedItemSilently(newlyCreatedItem);
            m_tableWidget->scrollToItem(newlyCreatedItem);
            m_tableWidget->editItem(newlyCreatedItem);
        }
    }

    updateResourceSourceLabel(partitions);

    // The resource cells are what set the column widths, so the figure can only be
    // computed once they are in place.
    updateTablePreferredWidth();
}

// [aurora2#1725 stage P7] Name the source of the numbers in the table.
//
// Deliberately reports the source of the CLB/DSP/BRAM columns, which is not the same as the
// tier of design_resources.json. Those columns are fed by the atom-based tally, so they are
// always counted from the post-synthesis netlist. A tier-2 file on disk does NOT make them
// tier 2: claiming placed-tile accuracy for a packing-density estimate is exactly the
// confusion A.13.5 exists to prevent.
//
// The tier-2 measurement has its own column, Placed, and is named separately below --
// keeping the two apart is the whole reason it is a column rather than a second number
// folded into CLB.
void PartitionsListWidget::updateResourceSourceLabel(const std::map<int, PartitionPtr>& partitions)
{
    if (partitions.empty()) {
        m_lbResourceSource->setVisible(false);
        return;
    }

    QString text = tr("Resources: counted from the post-synthesis netlist.");

    // Only ever a hint, at any tier: CLB required is atoms divided by packing density, not
    // the tile count the placer will settle on. "Placed" is where the real figure lives.
    text += tr("  CLB is a sizing hint in tiles, not a placement result.");
    if (Partition::designResources().tier >= 2) {
        text += tr("  Placed shows tiles measured from the placement.");
    }

    m_lbResourceSource->setText(text);
    m_lbResourceSource->setVisible(true);
}

// [aurora2#1725] Measure what the table's five columns actually need, so all of them are
// visible the first time the panel is shown.
//
// Nothing else tells the splitter how wide this pane wants to be: QAbstractScrollArea's size
// hint is a generic 256x192 that knows nothing about columns, so without this the pane is
// handed whatever is left over and the rightmost columns fall outside it. Widening the window
// made them reappear, which is the bug as it was originally reported: the numbers were there
// the whole time, just out of view.
//
// It is the pane's PREFERRED width, not a minimum. It used to be a hard
// m_tableWidget->setMinimumWidth(), which fixed the columns but also made this the one pane
// in the splitter that could not be dragged narrower -- 306px of the window that the
// hierarchy pane could never borrow. Now the pane asks for the width and gives it up when
// dragged; the table falls back to its horizontal scrollbar, which is the ordinary way a
// table too narrow for its columns behaves.
//
// Only the ResizeToContents columns are measured. Name is Stretch, so its width follows the
// pane, and feeding that back in would ratchet the figure up every time the user widened the
// window and never let it shrink again; it gets a text-derived width instead.
void PartitionsListWidget::updateTablePreferredWidth()
{
    const QHeaderView* header = m_tableWidget->horizontalHeader();

    int width = 0;
    for (int column = 0; column < m_tableWidget->columnCount(); ++column) {
        if (column == Column::Name) continue;
        width += header->sectionSize(column);
    }

    width += m_tableWidget->fontMetrics().horizontalAdvance(QLatin1String(kNameWidthSample)) +
             2 * kCellTextMargin;
    width += 2 * m_tableWidget->frameWidth();

    // Reserve the vertical scrollbar now: it takes its width out of the viewport as soon as
    // there are more partitions than fit, and must not do that by pushing Placed out of view.
    width += m_tableWidget->style()->pixelMetric(QStyle::PM_ScrollBarExtent);

    if (width == m_preferredWidth) {
        return;
    }
    m_preferredWidth = width;
    updateGeometry();  // the new sizeHint() is only consulted once the layout re-reads it
}

QSize PartitionsListWidget::sizeHint() const
{
    QSize hint = QWidget::sizeHint();
    hint.setWidth(qMax(hint.width(), m_preferredWidth));
    return hint;
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
