#include "SelectedResourcesWidget.h"

#include "Partition.h"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace fp {

SelectedResourcesWidget::SelectedResourcesWidget(QWidget* parent)
    : QWidget(parent),
      m_lbSelection(new QLabel),
      m_table(new QTableWidget(RowCount, 1, this))
{
    const int m = FP_UI_MARGIN;

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(m,m,m,m);
    layout->setSpacing(m);
    layout->addWidget(new QLabel(tr("Selected RTL Resources:")));
    layout->addWidget(m_lbSelection);
    layout->addWidget(m_table);

    m_table->setVerticalHeaderLabels({tr("CLB"), tr("DSP"), tr("BRAM")});
    // No column header: one column of numbers beside CLB/DSP/BRAM needs no title, and a
    // "Value" band over it only spends height in a pane shared with the tree.
    m_table->horizontalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setFocusPolicy(Qt::NoFocus);

    for (int row = 0; row < RowCount; ++row) {
        auto* item = new QTableWidgetItem();
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, item);
    }

    // Three rows and no more: this shares the pane with the tree, which is the thing the user
    // is actually working in. Sized rather than left to the layout, because a QTableWidget's
    // own hint is a generic 256x192 that would take a quarter of the pane and spend most of
    // it on blank rows.
    const int rowHeight = m_table->verticalHeader()->defaultSectionSize();
    m_table->setFixedHeight(RowCount * rowHeight + 2 * m_table->frameWidth());
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    clear();
}

void SelectedResourcesWidget::setValue(Row row, int value, const QString& tip)
{
    QTableWidgetItem* item = m_table->item(row, 0);
    item->setText(QString::number(value));
    item->setToolTip(tip);
}

void SelectedResourcesWidget::setTally(const ResourceTally& tally, int instances)
{
    m_lbSelection->setText(instances == 0
        ? tr("Nothing selected")
        : tr("%n instance(s) selected, %1 atoms", nullptr, instances).arg(tally.atoms()));

    setValue(Clb, tally.clbTiles,
             tr("%1 clb atoms at %2 atoms per tile.\n\n"
                "Luts and flops pack many to a tile, so this is a sizing estimate rather "
                "than a tile count the packer has agreed to -- the same figure, from the "
                "same arithmetic, that a partition's CLB column reports.")
                 .arg(tally.clbAtoms)
                 .arg(Partition::atomsPerTile()));
    setValue(Dsp, tally.dsp, tr("One DSP atom occupies one DSP tile, so this is exact."));
    setValue(Bram, tally.bram, tr("One BRAM atom occupies one BRAM tile, so this is exact."));
}

void SelectedResourcesWidget::clear()
{
    setTally(ResourceTally{}, 0);
}

}  // namespace fp
