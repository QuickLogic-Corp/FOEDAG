#include "SelectedResourcesWidget.h"

#include "Partition.h"

#include <QHeaderView>
#include <QLabel>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace fp {

namespace {

// Both tables are sized to this many rows and scroll past it. A fixed height rather than one
// that follows the content: the Atoms tab's row count changes with every selection, and a
// widget that grows and shrinks under the tree drags the tree's rows up and down with it.
// Four: the Tiles tab is always three, and a design's atoms run to about that many types --
// fft256 uses five across the whole netlist and two to four for any one instance.
constexpr int kVisibleRows = 4;

// Strips the vertical header and the frame's own margins from a table, so it can sit in a
// tab as a plain grid of label/value rows.
void configureTable(QTableWidget* table) {
    table->horizontalHeader()->setVisible(false);
    table->verticalHeader()->setVisible(false);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFocusPolicy(Qt::NoFocus);
    table->setWordWrap(false);
}

QTableWidgetItem* readOnlyItem(const QString& text = {}) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

}  // namespace

SelectedResourcesWidget::SelectedResourcesWidget(QWidget* parent)
    : QWidget(parent),
      m_lbSelection(new QLabel),
      m_tabs(new QTabWidget(this)),
      m_tiles(new QTableWidget(TileRowCount, 2, this)),
      m_atoms(new QTableWidget(0, 2, this))
{
    const int m = FP_UI_MARGIN;

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(m,m,m,m);
    layout->setSpacing(m);
    layout->addWidget(new QLabel(tr("Selected RTL Resources:")));
    layout->addWidget(m_lbSelection);
    layout->addWidget(m_tabs);

    configureTable(m_tiles);
    configureTable(m_atoms);

    // The name column takes what it needs and the number sits to its right, in both tables,
    // so the two tabs read as the same table with different rows in it.
    for (QTableWidget* table : {m_tiles, m_atoms}) {
        table->horizontalHeader()->setSectionResizeMode(Type, QHeaderView::Stretch);
        table->horizontalHeader()->setSectionResizeMode(Count, QHeaderView::ResizeToContents);
    }

    const QString tileNames[TileRowCount] = {tr("CLB"), tr("DSP"), tr("BRAM")};
    for (int row = 0; row < TileRowCount; ++row) {
        m_tiles->setItem(row, Type, readOnlyItem(tileNames[row]));
        m_tiles->setItem(row, Count, readOnlyItem());
    }

    m_tabs->addTab(m_tiles, tr("Tiles"));
    m_tabs->addTab(m_atoms, tr("Atoms"));
    m_tabs->setCurrentWidget(m_atoms);   // exact synthesized types, not the CLB estimate

    // Sized rather than left to the layout: a QTableWidget's own hint is a generic 256x192
    // that would take a quarter of the pane and spend most of it on blank rows.
    const int rowHeight = m_tiles->verticalHeader()->defaultSectionSize();
    const int tableHeight = kVisibleRows * rowHeight + 2 * m_tiles->frameWidth();
    m_tiles->setFixedHeight(tableHeight);
    m_atoms->setFixedHeight(tableHeight);
    // The tabs have to be pinned as well as the tables inside them. A QTabWidget sizes its
    // page area from the page's sizeHint -- a QTableWidget's generic 256x192, which
    // setFixedHeight() does not touch -- so left alone it draws a band of empty frame under
    // a table that cannot grow into it.
    m_tabs->setFixedHeight(m_tabs->tabBar()->sizeHint().height() + tableHeight +
                           2 * m_tabs->style()->pixelMetric(QStyle::PM_DefaultFrameWidth));
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    clear();
}

void SelectedResourcesWidget::setTileValue(TileRow row, int value, const QString& tip)
{
    QTableWidgetItem* item = m_tiles->item(row, Count);
    item->setText(QString::number(value));
    item->setToolTip(tip);
    m_tiles->item(row, Type)->setToolTip(tip);
}

void SelectedResourcesWidget::setAtomRows(const std::map<std::string, int>& atomResources)
{
    // Biggest first: the row that decides whether this fits is the one to see without
    // scrolling. Ties fall back to the name, so the order is stable between selections.
    std::vector<std::pair<std::string, int>> rows(atomResources.begin(), atomResources.end());
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return (a.second != b.second) ? (a.second > b.second) : (a.first < b.first);
    });

    m_atoms->setRowCount(static_cast<int>(rows.size()));
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        const QString type = QString::fromStdString(rows[row].first);
        auto* typeItem = readOnlyItem(type);
        // A BRAM's type name runs to 49 characters, which no pane this shares with the tree
        // will show in full. Elided in place, whole in the tooltip.
        typeItem->setToolTip(type);
        m_atoms->setItem(row, Type, typeItem);
        m_atoms->setItem(row, Count, readOnlyItem(QString::number(rows[row].second)));
    }
    m_atoms->setTextElideMode(Qt::ElideRight);
}

void SelectedResourcesWidget::setTally(const ResourceTally& tally,
                                       const std::map<std::string, int>& atomResources,
                                       int instances)
{
    m_lbSelection->setText(instances == 0
        ? tr("Nothing selected")
        : tr("%n instance(s) selected, %1 atoms", nullptr, instances).arg(tally.atoms()));

    setTileValue(Clb, tally.clbTiles,
                 tr("%1 clb atoms at %2 atoms per tile.\n\n"
                    "Luts and flops pack many to a tile, so this is a sizing estimate rather "
                    "than a tile count the packer has agreed to -- the same figure, from the "
                    "same arithmetic, that a partition's CLB column reports.")
                     .arg(tally.clbAtoms)
                     .arg(Partition::atomsPerTile()));
    setTileValue(Dsp, tally.dsp, tr("One DSP atom occupies one DSP tile, so this is exact."));
    setTileValue(Bram, tally.bram, tr("One BRAM atom occupies one BRAM tile, so this is exact."));

    setAtomRows(atomResources);
}

void SelectedResourcesWidget::clear()
{
    setTally(ResourceTally{}, {}, 0);
}

}  // namespace fp
