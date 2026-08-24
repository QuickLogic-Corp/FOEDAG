#include "SynthResourceHierarchyWidget.h"

#include "AtomSets.h"
#include "HierarhyElement.h"
#include "CheckableButton.h"

#include <QStringListModel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QHeaderView>
#include <QLabel>
#include <QCompleter>
#include <QApplication>
#include <QClipboard>
#include <QMenu>
#include <QShortcut>
#include <QBrush>
#include <QDialog>
#include <QFontDatabase>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QStyle>
#include <QCursor>
#include <QPainter>
#include <QPointer>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>

#include <QDebug>

#define DISABLE_SHOW_ONLY_CHECKED_ITEMS_UI

namespace fp {

namespace {

// Marks rows that exist solely to display individual atoms (not RTL instances).
// These rows are non-checkable leaf children of netlist items.
static constexpr int kVprDisplayRowRole = Qt::UserRole + 100;

// Marks a tree item as a bus-bit child (e.g. "[0]" under "din").
// Path construction concatenates directly: parent "din" + "[0]" → "din[0]".
static constexpr int kBusBitRole = Qt::UserRole + 101;

// [aurora2#1725 stage P7] The Why cell's popup text and window title. Held on the item so
// the click handler needs no parallel lookup table keyed by row.
static constexpr int kExplanationRole = Qt::UserRole + 102;
static constexpr int kExplanationTitleRole = Qt::UserRole + 103;

bool isVprDisplayRow(const QStandardItem* item) {
    return item && item->data(kVprDisplayRowRole).toBool();
}

// [aurora2#1725 stage P7] Paints the Why cell as a push button, so it looks pressable and
// lights up under the pointer, instead of a bare "?" glyph that reads as text. Drawn through
// the style's own CE_PushButton, so the background and the hover state match every other
// button in the panel under any theme.
//
// A delegate rather than a widget per row: only some rows have an explanation, but a tree can
// hold thousands of rows and hover has to be cheap.
class WhyButtonDelegate final : public QStyledItemDelegate {
public:
    explicit WhyButtonDelegate(QTreeView* view)
        : QStyledItemDelegate(view), m_view(view) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        if (index.data(kExplanationRole).toString().isEmpty()) {
            // Nothing to explain on this row: no button at all, not a disabled one.
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }

        QStyleOptionButton button;
        button.rect = option.rect.adjusted(2, 2, -2, -2);
        button.text = QStringLiteral("?");
        button.state = QStyle::State_Enabled | QStyle::State_Raised;
        // option.state's MouseOver covers the whole hovered row, so the pointer is tested
        // against this cell instead -- the button must light up only when it is the thing
        // under the cursor.
        if (m_view && option.rect.contains(m_view->viewport()->mapFromGlobal(QCursor::pos()))) {
            button.state |= QStyle::State_MouseOver;
        }
        QStyle* style = m_view ? m_view->style() : QApplication::style();
        style->drawControl(QStyle::CE_PushButton, &button, painter, m_view);
    }

private:
    QTreeView* m_view{nullptr};
};

// [aurora2#1725 stage P7] Shows one row's explanation. A dialog rather than a tooltip because
// the interesting case is hundreds of out-of-region atom names: that has to be scrollable,
// selectable and copyable, none of which a tooltip is. Non-modal, so it can stay open while
// the user carries on clicking through the tree, and it deletes itself when closed.
// One dialog for the whole panel: pressing another row's button replaces what is on screen
// rather than stacking a second window over it. Shared across both trees (netlist and
// partition), because to the user they are one panel.
QPointer<QDialog> g_explanationDialog;

void showExplanationDialog(QWidget* parent, const QString& title, const QString& body) {
    if (g_explanationDialog) {
        g_explanationDialog->close();   // WA_DeleteOnClose disposes of it
    }

    QDialog* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(title);
    dialog->setModal(false);
    dialog->resize(560, 420);

    QVBoxLayout* layout = new QVBoxLayout(dialog);

    QPlainTextEdit* text = new QPlainTextEdit(body, dialog);
    text->setReadOnly(true);              // read-only, still selectable and copyable
    // Wrap rather than scroll sideways: an atom name plus its coordinates can be longer than
    // the dialog, and text hidden off to the right may as well not be there. Anywhere, not
    // just at word boundaries, because these names have no spaces to break at.
    text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    text->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    text->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(text);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    // Explicit Copy: selecting a few hundred atom names by dragging is worse than a button.
    QPushButton* copyAll = buttons->addButton(QObject::tr("Copy"), QDialogButtonBox::ActionRole);
    QObject::connect(copyAll, &QPushButton::clicked, dialog, [body]{
        QApplication::clipboard()->setText(body);
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    layout->addWidget(buttons);

    g_explanationDialog = dialog;
    dialog->show();
    dialog->raise();
}

// Build the full path for `item` given its parent's path.
// Bus-bit items concatenate directly; regular items are joined with ".".
std::string buildChildPath(const std::string& prefix, const QStandardItem* item) {
    if (!item) return prefix;
    const std::string name = item->text().toStdString();
    if (item->data(kBusBitRole).toBool())
        return prefix + name;          // "din" + "[0]"  → "din[0]"
    return prefix.empty() ? name : (prefix + "." + name);
}

// [aurora2#1725] Holds the tree's expand/collapse state across an operation that would
// otherwise change it. Selecting a partition is not a request to reshape the tree: what the
// user opened stays open, what they closed stays closed. Two things used to violate that --
// selectPartition() expanded every element it checked (and nothing ever undid it, so each
// selection left more of the tree open), and showOnlyCheckedItems()/showAllItems() call
// expandAll() when the expand/collapse button is on, which the partition view triggers on
// every selection change.
//
// Restoring afterwards rather than suppressing each call: the state is then preserved
// whatever those helpers do internally, including a full expandAll(). Persistent indexes are
// safe here because these operations only change check states -- rows are added in build(),
// which legitimately sets the initial state and does not use this.
//
// It stands down entirely while the expand/collapse button is on. In that mode the user has
// asked for the whole tree open, so there is no per-node choice of theirs to protect -- and
// restoring anyway is what made the partition view contradict its own button: the button
// starts checked there (ShowOnlyCheckedItems), selectPartition() captured the state while
// the rows were still hidden and collapsed, showOnlyCheckedItems() then expanded them, and
// this destructor promptly collapsed them again.
class ExpansionKeeper {
public:
    ExpansionKeeper(QTreeView* view, QAbstractItemModel* model, bool expandAllActive)
        : m_view(view), m_keeping(!expandAllActive) {
        if (m_keeping) {
            capture(QModelIndex(), model);
        }
    }
    ~ExpansionKeeper() {
        if (!m_keeping) {
            return;
        }
        for (const auto& [index, wasExpanded]: m_state) {
            if (index.isValid() && (m_view->isExpanded(index) != wasExpanded)) {
                m_view->setExpanded(index, wasExpanded);
            }
        }
    }
    ExpansionKeeper(const ExpansionKeeper&) = delete;
    ExpansionKeeper& operator=(const ExpansionKeeper&) = delete;

private:
    void capture(const QModelIndex& parent, QAbstractItemModel* model) {
        const int rows = model->rowCount(parent);
        for (int row = 0; row < rows; ++row) {
            const QModelIndex index = model->index(row, 0, parent);
            if (model->hasChildren(index)) {
                m_state.emplace_back(QPersistentModelIndex(index), m_view->isExpanded(index));
                capture(index, model);
            }
        }
    }

    QTreeView* m_view;
    bool m_keeping;
    std::vector<std::pair<QPersistentModelIndex, bool>> m_state;
};

void uncheckAllRecursive(QStandardItem* item, int col) {
    if (!item) {
        return;
    }

    const int rows = item->rowCount();
    for (int row = 0; row < rows; ++row) {
        QStandardItem* child = item->child(row, col);
        if (!child) {
            continue;
        }

        if (child->checkState() != Qt::Unchecked) {
            child->setCheckState(Qt::Unchecked);
        }

        uncheckAllRecursive(child, col);
    }
};

}

SynthResourceHierarchyWidget::SynthResourceHierarchyWidget(int flags, QWidget* parent)
    : QWidget(parent),
      m_flags(flags),
      m_lbView(new QLabel),
      m_view(new QTreeView(this)),
      m_model(new QStandardItemModel(this))
{
    const int m = FP_UI_MARGIN;

    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(m,m,m,m);
    layout->setSpacing(m);

    // tool bar
    QCheckBox* chShowChecked{nullptr};
    if (!isShowOnlyCheckedItems()) {
#ifndef DISABLE_SHOW_ONLY_CHECKED_ITEMS_UI
        chShowChecked = new QCheckBox("show only checked items");
        QObject::connect(chShowChecked, &QCheckBox::stateChanged, this, [this](int state){
            if (state == Qt::Checked) {
                showOnlyCheckedItems();
            } else {
                showAllItems();
            }
        });
#endif // DISABLE_SHOW_ONLY_CHECKED_ITEMS_UI
        m_leFilter = new QLineEdit;
    }

    QHBoxLayout* toolbarLayout = new QHBoxLayout;
    toolbarLayout->setContentsMargins(m,m,m,m);
    toolbarLayout->setSpacing(m);
    layout->addLayout(toolbarLayout);
    if (chShowChecked) {
        toolbarLayout->addWidget(chShowChecked);
    }

    m_bnExpandCollapse = new CheckableButton(QIcon(":/right-arrow.png"), QIcon(":/down-arrow.png"));
    m_bnExpandCollapse->setChecked(isShowOnlyCheckedItems());

    m_bnExpandCollapse->setToolTip(tr("Expand/collapse netlist items"));
    QObject::connect(m_bnExpandCollapse, &QPushButton::toggled, this, [this](bool checked) {
      if (checked) {
        m_view->expandAll();
      } else {
        m_view->collapseAll();
      }
    });

    toolbarLayout->addWidget(m_bnExpandCollapse);

    if (m_leFilter) {
        toolbarLayout->addWidget(new QLabel("Filter"));
        toolbarLayout->addWidget(m_leFilter);

        QObject::connect(m_leFilter, &QLineEdit::textChanged, this, [this](const QString& pattern) {
            if (pattern.isEmpty()) {
                showAllItems();
            } else {
                showFilteredItems(pattern.toStdString());
            }
        });
    }
    //

    if (toolbarLayout->count() == 1) {
      toolbarLayout->addStretch();
    }

    layout->addWidget(m_lbView);
    layout->addWidget(m_view);
    m_lbView->setVisible(false); // optionally activated by method

    m_view->setModel(m_model);

    connect(m_model, &QStandardItemModel::itemChanged, this, [this](QStandardItem* item) {
        onItemChanged(item, /*reportChanges*/true);
    });

    // Returns the atom name text for a given tree index (leaf items only).
    auto atomCopyText = [this](const QModelIndex& idx) -> QString {
        if (!idx.isValid()) return {};
        const QModelIndex atomIdx = m_model->index(idx.row(), Column::AtomList, idx.parent());
        return atomIdx.data(Qt::DisplayRole).toString();
    };

    // Right-click context menu: copy atom name(s) of the hovered row
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    // [aurora2#1725 stage P7] The "?" cell behaves as a button: one click, one popup. Kept as
    // a cell rather than a real QPushButton per row so a tree of thousands of instances does
    // not carry thousands of widgets.
    m_view->setItemDelegateForColumn(Column::Why, new WhyButtonDelegate(m_view));
    // Hover is painted from the cursor position, so the viewport has to be repainted as the
    // pointer moves for the button to light up and go dark again.
    m_view->setMouseTracking(true);
    connect(m_view, &QTreeView::entered, this, [this](const QModelIndex&) {
        m_view->viewport()->update();
    });
    connect(m_view, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (!index.isValid() || index.column() != Column::Why) return;
        const QString body = index.data(kExplanationRole).toString();
        if (body.isEmpty()) return;
        showExplanationDialog(this, index.data(kExplanationTitleRole).toString(), body);
    });

    connect(m_view, &QTreeView::customContextMenuRequested, this, [this, atomCopyText](const QPoint& pos) {
        const QModelIndex idx = m_view->indexAt(pos);
        const QString text = atomCopyText(idx);
        if (text.isEmpty()) return;
        QMenu menu;
        QAction* act = menu.addAction(tr("Copy Atom Name"));
        if (menu.exec(m_view->viewport()->mapToGlobal(pos)) == act)
            QApplication::clipboard()->setText(text);
    });

    // Ctrl+C: copy atom name(s) of the current row
    auto* copyShortcut = new QShortcut(QKeySequence::Copy, m_view);
    connect(copyShortcut, &QShortcut::activated, this, [this, atomCopyText]() {
        const QString text = atomCopyText(m_view->currentIndex());
        if (!text.isEmpty())
            QApplication::clipboard()->setText(text);
    });

    setEnabled(false);
}

void SynthResourceHierarchyWidget::build(const NaturalStringSet& elements)
{
    m_model->clear();
    // Both variants carry all five columns so Column::Why means the same index in each;
    // the partition tree hides Partitions instead of omitting it.
    if (!isPartitionsColumnHidden()) {
        m_model->setHorizontalHeaderLabels(QList<QString>() << "RTL Names" << "Atom List" << "Type" << "Partitions" << "");
    } else {
        m_model->setHorizontalHeaderLabels(QList<QString>() << "Partition RTL Names" << "Atom List" << "Type" << "" << "");
        m_view->header()->setVisible(false);
    }

    QHeaderView* header = m_view->header();
    // Why is the last logical section, so stretching the last one would stretch the "?"
    // column; AtomList and Partitions are Stretch and absorb the slack instead.
    header->setStretchLastSection(false);
    header->setSectionResizeMode(Column::Netlist, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(Column::AtomList, QHeaderView::Stretch);
    header->setSectionResizeMode(Column::AtomType, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(Column::Why, QHeaderView::Fixed);
    header->resizeSection(Column::Why, m_view->style()->pixelMetric(QStyle::PM_SmallIconSize) + 12);
    if (!isPartitionsColumnHidden()) {
        header->setSectionResizeMode(Column::Partitions, QHeaderView::Stretch);
    } else {
        m_view->setColumnHidden(Column::Partitions, true);
    }
    // Sits immediately right of the name, where the icon it explains is. Logical order is
    // untouched, so every Column:: index elsewhere still addresses the same cell.
    header->moveSection(header->visualIndex(Column::Why), 1);

    QSignalBlocker blocker(m_model); // prevent itemChanged spam
    for (const std::string& element: elements) {
        addPath(element);
    }
    populateAtomColumns();
    // After populateAtomColumns(): a graded instance's verdict is the better explanation
    // than "no atoms in atomsets.json", so it overrides that hiding.
    applyInstanceVerdicts();
    // After applyInstanceVerdicts(): placement is the later, measured word on a row, and its
    // icon should win over nothing more than the grading colour it is drawn beside.
    applyPlacementVerdicts();
    blocker.unblock();

    // completer
    if (!isShowOnlyCheckedItems()) {
        QList<QString> qtElements;
        for (const std::string& element: elements) {
            qtElements.append(QString::fromStdString(element));
        }
        QStringListModel* model = new QStringListModel(qtElements, m_leFilter);

        QCompleter* completer = new QCompleter(model, m_leFilter);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);      // or Qt::MatchStartsWith
        completer->setCompletionMode(QCompleter::PopupCompletion);
        m_leFilter->setCompleter(completer);
    }
    //

    if (isShowOnlyCheckedItems()) {
        showOnlyCheckedItems();
    }
    m_view->viewport()->update();

    if (m_bnExpandCollapse->isChecked()) {
      m_view->expandAll();
    }

    setAtomColumnsVisible(m_atomColumnsVisible);   // header sections were just rebuilt
}

void SynthResourceHierarchyWidget::onPartitionsChanged(const std::map<int, PartitionPtr>& partitions)
{
    // [aurora2#1725] Re-derive real atom names for elements that were loaded
    // without them. QdcSerializer::load() builds each partition's elements from
    // RTL paths alone (a .qdc has no atom data), so clb/dsp/bram *RequiredCount()
    // reads back 0 until this runs -- exactly the gap that made re-checking a tree
    // item "fix" it: fillPartitionWithSelectedElements() does this same lookup,
    // just only for whichever one partition is currently selected. Cheap and
    // idempotent: an element that already carries the right vprNames just gets the
    // same set back, and if m_atomNames is empty (pre-synthesis, no atomsets.json
    // yet) every element keeps whatever it already had.
    for (const auto& [id, partition]: partitions) {
        const HierarhyElements existing = partition->elements();
        partition->clearElemenets();
        for (const HierarhyElement& element: existing) {
            std::set<std::string> names = atomNamesFor(element.path);
            if (names.empty()) {
                names = element.vprNames;
            }
            partition->addElement(HierarhyElement{element.path, element.isLeaf, names});
        }
    }

    // intermediate data be captured in lambda: path -> the partitions naming that exact path
    std::map<std::string, std::set<std::string>> data;
    for (const auto& [id, partition]: partitions) {
        for (const HierarhyElement& element: partition->elements()) {
            data[element.path].insert(partition->name());
        }
    }

    const bool isPartitionsColumnVisible = !isPartitionsColumnHidden();

    // [aurora2#1725] An element covers its whole subtree -- that is what the .qdc's
    // "<path>.*" form means -- so a row belongs to every partition naming it OR any of its
    // ancestors, and the column has to say so. Put partition1 on an instance and partition2
    // on something nested inside it and the nested row really is in both: selecting
    // partition1 shows it checked because its ancestor is checked, while selecting
    // partition2 shows it checked in its own right. Listing only the exact match hid that
    // second claim, so the row looked cleanly owned by partition2 and the double constraint
    // -- unsatisfiable for VPR, since a cluster cannot sit in two regions -- was invisible.
    // Inherited names are threaded down the walk rather than re-derived per row.
    std::function<void(QStandardItem*, const std::string&, const std::set<std::string>&)> setPartitionRecursive =
        [&data, &setPartitionRecursive, isPartitionsColumnVisible](QStandardItem* item,
                                                                  const std::string& prefix,
                                                                  const std::set<std::string>& fromAncestors)
    {
        if (!item) {
            return;
        }

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child || isVprDisplayRow(child)) {
                continue;
            }

            const std::string fullPath = buildChildPath(prefix, child);

            std::set<std::string> covering{fromAncestors};
            if (auto it = data.find(fullPath); it != data.end()) {
                covering.insert(it->second.begin(), it->second.end());
            }

            if (isPartitionsColumnVisible) {
                QStandardItem* partitionItem = item->child(row, Column::Partitions);
                if (partitionItem) {
                    std::string text;
                    for (const std::string& name: covering) {
                        text += (text.empty() ? "" : ",") + name;
                    }
                    partitionItem->setText(QString::fromStdString(text));
                }
            }

            setPartitionRecursive(child, fullPath, covering);
        }
    };

    QSignalBlocker blockModel(m_model);     // Block model signals to avoid massive itemChanged cascades
    setPartitionRecursive(m_model->invisibleRootItem(), "", {});
    blockModel.unblock();

    m_view->viewport()->update();
}


void SynthResourceHierarchyWidget::unselectPartition()
{
    setEnabled(false);

    ExpansionKeeper keepExpansion(m_view, m_model, m_bnExpandCollapse->isChecked());

    m_selectedPartition.reset();

    QSignalBlocker blocker(m_model); // prevent itemChanged spam
    uncheckAllRecursive(m_model->invisibleRootItem(), Column::Netlist);

    if (isShowOnlyCheckedItems()) {
        showOnlyCheckedItems();
    }

    blocker.unblock();

    updateViewLabel();
    m_view->viewport()->update();
}

void SynthResourceHierarchyWidget::selectPartition(const PartitionPtr& partition)
{
    m_selectedPartition = partition;

    setEnabled(true);

    ExpansionKeeper keepExpansion(m_view, m_model, m_bnExpandCollapse->isChecked());

    // check only requested elements + set partition label
    std::function<void(QStandardItem*, const std::string&)> checkAndPartitionRecursive =
        [&](QStandardItem* item, const std::string& prefix)
    {
        if (!item) {
            return;
        }

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child || isVprDisplayRow(child)) {
                continue;
            }

            const std::string fullPath = buildChildPath(prefix, child);

            // [aurora2#1725 stage P4] A .qdc written before the instance was deleted -- or
            // before verdicts were rendered at all -- can still name it. Don't tick it:
            // setCheckState() writes the check-state role whatever the flags say, so a
            // greyed, non-checkable row would come back with a checkbox that no other code
            // path agrees with (every one of them is isCheckable()-gated, so the tick
            // silently vanished again on the next selection change). The row stays greyed,
            // and DeviceGrid reports that the partition names dead instances.
            if (partition->elements().contains(fullPath) && child->isCheckable()) {
                child->setCheckState(Qt::Checked);
                onItemChanged(child, /*reportChanges*/false); // update ancestor and descendant item states
                // Deliberately does NOT expand the matched item any more: selecting a
                // partition must leave the user's expand/collapse state alone (see
                // ExpansionKeeper), and expanding it here was never undone, so every
                // selection left another branch open until the whole tree was unfolded.
            }

            checkAndPartitionRecursive(child, fullPath);
        }
    };

    // Block model signals to avoid massive itemChanged cascades
    QSignalBlocker blockModel(m_model);
    uncheckAllRecursive(m_model->invisibleRootItem(), Column::Netlist);
    checkAndPartitionRecursive(m_model->invisibleRootItem(), "");

    if (isShowOnlyCheckedItems()) {
        showOnlyCheckedItems();
    }

    blockModel.unblock();

    updateViewLabel();
    m_view->viewport()->update();
}

void SynthResourceHierarchyWidget::fillPartitionWithSelectedElements(const PartitionPtr& partition) const
{
    std::function<void(QStandardItem*, const std::string&, const PartitionPtr&)> fillPartitionSelectedElementsRecursive =
        [&fillPartitionSelectedElementsRecursive, this](QStandardItem* item, const std::string& prefix, const PartitionPtr& partition)
    {
        if (!item) return;

        const std::string path = buildChildPath(prefix, item);

        const Qt::CheckState st = item->isCheckable() ? item->checkState() : Qt::Unchecked;

        const int rows = item->rowCount();
        // VPR display child rows don't count — an item with only those children is still a leaf
        bool isLeaf = (rows == 0);
        if (!isLeaf) {
            isLeaf = true;
            for (int r = 0; r < rows; ++r) {
                QStandardItem* c = item->child(r, Column::Netlist);
                if (c && !isVprDisplayRow(c)) { isLeaf = false; break; }
            }
        }

        auto vprNames = [this](const std::string& p) { return atomNamesFor(p); };

        if (isLeaf) {
            if (st == Qt::Checked) {
                partition->addElement(HierarhyElement{path, true, vprNames(path)});
            }
        } else {
            if (st == Qt::Checked) {
                bool allChildrenChecked = true;
                for (int row = 0; row < rows; ++row) {
                    QStandardItem* child = item->child(row, Column::Netlist);
                    if (child && !isVprDisplayRow(child) && child->isCheckable() && child->checkState() != Qt::Checked) {
                        allChildrenChecked = false;
                        break;
                    }
                }
                if (allChildrenChecked) {
                    partition->addElement(HierarhyElement{path, false, vprNames(path)});
                } else {
                    for (int row = 0; row < rows; ++row) {
                        QStandardItem* child = item->child(row, Column::Netlist);
                        if (!isVprDisplayRow(child))
                            fillPartitionSelectedElementsRecursive(child, path, partition);
                    }
                }
            } else {
                for (int row = 0; row < rows; ++row) {
                    QStandardItem* child = item->child(row, Column::Netlist);
                    if (!isVprDisplayRow(child))
                        fillPartitionSelectedElementsRecursive(child, path, partition);
                }
            }
        }
    };

    partition->clearElemenets();
    QStandardItem* root = m_model->invisibleRootItem();
    for (int row = 0; row < root->rowCount(); ++row) {
        if (QStandardItem* child = root->child(row, Column::Netlist))
            fillPartitionSelectedElementsRecursive(child, "", partition);
    }
}

void SynthResourceHierarchyWidget::addPath(const std::string& dottedPath)
{
    std::function<QStandardItem*(QStandardItem*, const QString&)>
        findOrCreateChild = [](QStandardItem* parent, const QString& text)
    {
        auto applyFlags = [](QStandardItem* item) {
            Qt::ItemFlags flags = item->flags()
            | Qt::ItemIsEnabled
            | Qt::ItemIsSelectable
            | Qt::ItemIsUserCheckable;

            item->setFlags(flags);
            if (!item->data(Qt::CheckStateRole).isValid()) {
                item->setCheckState(Qt::Unchecked);
            }
        };

        for (int row = 0; row < parent->rowCount(); ++row) {
            QStandardItem* child = parent->child(row, Column::Netlist);
            if (child && (child->text() == text)) {
                applyFlags(child);
                return child;
            }
        }

        QStandardItem* item = new QStandardItem(text);
        applyFlags(item);
        QStandardItem* atomListItem = new QStandardItem("");
        atomListItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        QStandardItem* atomTypeItem = new QStandardItem("");
        atomTypeItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        QStandardItem* partitionItem = new QStandardItem("");
        QStandardItem* whyItem = new QStandardItem("");
        whyItem->setFlags(Qt::ItemIsEnabled);   // clickable, never checkable or editable
        whyItem->setTextAlignment(Qt::AlignCenter);
        parent->appendRow({item, atomListItem, atomTypeItem, partitionItem, whyItem});
        return item;
    };

    QStringList parts = QString::fromStdString(dottedPath).split('.');
    if (parts.isEmpty()) return;

    // Detect bus-bit path: last component contains "[index]" (e.g. "din[0]").
    // Group bits under a shared bus parent so the tree shows:
    //   ▼ din   (bus parent, checkable)
    //       [0]
    //       [1]  ...
    const QString lastPart = parts.last();
    const int bracketPos = lastPart.indexOf('[');
    const bool lastIsBusBit = (bracketPos > 0);
    QString busBase, busBit;
    if (lastIsBusBit) {
        busBase = lastPart.left(bracketPos);   // "din"
        busBit  = lastPart.mid(bracketPos);    // "[0]"
        parts.last() = busBase;                // navigate to bus parent
    }

    QStandardItem* root = m_model->invisibleRootItem();
    QStandardItem* parent = root;
    for (const QString& part : parts)
        parent = findOrCreateChild(parent, part);

    if (lastIsBusBit) {
        QStandardItem* bitItem = findOrCreateChild(parent, busBit);
        bitItem->setData(true, kBusBitRole);
        // Also register the bus parent path for filtering/completion
        m_rawElements.insert(parts.join('.').toStdString());
    }

    m_rawElements.insert(dottedPath);
}

void SynthResourceHierarchyWidget::setAtomNames(std::map<std::string, std::vector<std::string>, NaturalLess> atomNames)
{
    m_atomNames = std::move(atomNames);
    m_hasAtomNames = true;
}

std::set<std::string> SynthResourceHierarchyWidget::atomNamesFor(const std::string& path) const
{
    // [aurora2#1725 REQ-004] Delegates to the shared lookup, which the batch checker also
    // uses. The subtree reconstruction this used to hold lives there now: a second copy is a
    // second set of rules to keep in step, and the requirement is that a rule added to one
    // path is present in the other.
    return fp::atomNamesFor(m_atomNames, path);
}

// [aurora2#1725 stage P7] Placement status. Same shape as setInstanceVerdicts(): store, then
// apply if the tree already exists (build() applies both itself).
void SynthResourceHierarchyWidget::setPlacementVerdicts(
    std::map<std::string, InstancePlacement> placements)
{
    m_placements = std::move(placements);
    applyPlacementVerdicts();
}

void SynthResourceHierarchyWidget::setExplanation(QStandardItem* whyItem, const QString& title,
                                                  const QString& body)
{
    if (!whyItem) return;
    whyItem->setText(QStringLiteral("?"));
    whyItem->setData(body, kExplanationRole);
    whyItem->setData(title, kExplanationTitleRole);
    whyItem->setToolTip(tr("Click for details"));
}

// Renders the measured placement of every constrained instance: a tick when all of its atoms
// landed inside its region, a warning triangle when any did not, and nothing at all for an
// instance no .qdc constrains -- there is no region for those to be inside of, and marking
// them all "placed" would turn the column into decoration.
//
// The out-of-region atom names go in the Why popup rather than the tooltip: there can be
// hundreds, and the whole point is being able to read and copy them.
void SynthResourceHierarchyWidget::applyPlacementVerdicts()
{
    if (!m_model) return;

    std::function<void(QStandardItem*, const std::string&)> markRecursive =
        [&markRecursive, this](QStandardItem* item, const std::string& prefix)
    {
        if (!item) return;
        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child || isVprDisplayRow(child)) continue;

            const std::string path = buildChildPath(prefix, child);
            const auto found = m_placements.find(path);
            if (found != m_placements.end()) {
                const InstancePlacement& placement = found->second;
                const QString region = QString::fromStdString(placement.region);

                if (placement.fullyPlaced()) {
                    // No icon: a tick here reads as the row's own checkbox. Nothing wrong is
                    // the default state, and the tooltip still says so on hover.
                    child->setToolTip(tr("Placed: all %1 atoms are inside %2 (partition %3).")
                                          .arg(placement.atomsTotal)
                                          .arg(region)
                                          .arg(QString::fromStdString(placement.partition)));
                } else if (placement.partiallyPlaced()) {
                    child->setIcon(m_view->style()->standardIcon(QStyle::SP_MessageBoxWarning));

                    // Tooltip: the count, the region, and the first few names, so hovering is
                    // already useful. The full list is one click away.
                    QStringList preview;
                    const int kPreview = 5;
                    for (const PlacedAtom& atom : placement.outside) {
                        if (preview.size() >= kPreview) break;
                        preview << (atom.located
                            ? tr("%1 at (%2,%3)").arg(QString::fromStdString(atom.name))
                                  .arg(atom.x).arg(atom.y)
                            : tr("%1 (unplaced)").arg(QString::fromStdString(atom.name)));
                    }
                    const int remaining =
                        static_cast<int>(placement.outside.size()) - preview.size();
                    QString tip = tr("Partially placed: %1 of %2 atoms are outside %3.\n\n%4")
                                      .arg(placement.outside.size())
                                      .arg(placement.atomsTotal)
                                      .arg(region)
                                      .arg(preview.join(QStringLiteral("\n")));
                    if (remaining > 0) {
                        tip += tr("\n... and %n more atom(s). Click ? for the full list.",
                                  nullptr, remaining);
                    }
                    child->setToolTip(tip);

                    // Popup body: every atom, one per line, with where it actually landed.
                    QStringList lines;
                    lines << tr("Instance      : %1").arg(QString::fromStdString(path));
                    lines << tr("Partition     : %1").arg(QString::fromStdString(placement.partition));
                    lines << tr("Region        : %1").arg(region);
                    lines << tr("Atoms placed  : %1").arg(placement.atomsTotal);
                    lines << tr("Inside region : %1").arg(placement.inRegion);
                    lines << tr("Outside region: %1").arg(placement.outside.size());
                    lines << QString();
                    lines << tr("Atoms placed elsewhere:");
                    for (const PlacedAtom& atom : placement.outside) {
                        lines << (atom.located
                            ? QStringLiteral("  %1  clb(%2,%3)")
                                  .arg(QString::fromStdString(atom.name)).arg(atom.x).arg(atom.y)
                            : QStringLiteral("  %1  (not placed)")
                                  .arg(QString::fromStdString(atom.name)));
                    }
                    setExplanation(item->child(row, Column::Why),
                                   tr("Why partially placed: %1").arg(QString::fromStdString(path)),
                                   lines.join(QStringLiteral("\n")));
                }
            } else {
                // No verdict for this row: clear any icon a previous placement left behind, so
                // a re-measured tree never shows a status that is no longer true. The crossed
                // icon on an optimised-out instance is not ours to clear -- that one comes
                // from the P4 grading, which has already run by this point.
                const auto graded = m_verdicts.find(path);
                const bool optimisedOut =
                    graded != m_verdicts.end() && graded->second.verdict == "deleted";
                if (!optimisedOut) {
                    child->setIcon(QIcon());
                }
            }

            markRecursive(child, path);
        }
    };

    markRecursive(m_model->invisibleRootItem(), "");
}

void SynthResourceHierarchyWidget::setInstanceVerdicts(std::map<std::string, InstanceVerdict> verdicts)
{
    m_verdicts = std::move(verdicts);
}

void SynthResourceHierarchyWidget::setAtomColumnsVisible(bool visible)
{
    m_atomColumnsVisible = visible;
    // Kept as a member and re-applied from build() too: the model is cleared there, which
    // rebuilds the header sections and loses their hidden flags.
    m_view->setColumnHidden(Column::AtomList, !visible);
    m_view->setColumnHidden(Column::AtomType, !visible);

    // A multi-atom leaf gets one child row per atom, and those rows carry nothing but the
    // two columns just hidden -- their first column is deliberately blank. Hiding only the
    // columns therefore left a run of empty placeholder rows under such a leaf. Hide the
    // rows as well, so the tree shows RTL instances and nothing else.
    std::function<void(QStandardItem*)> applyRecursive =
        [&applyRecursive, this, visible](QStandardItem* item)
    {
        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child) {
                continue;
            }
            if (isVprDisplayRow(child)) {
                const QModelIndex parentIdx = (item == m_model->invisibleRootItem())
                    ? QModelIndex() : item->index();
                m_view->setRowHidden(row, parentIdx, !visible);
                continue;   // an atom row has no children of its own
            }
            applyRecursive(child);
        }
    };
    applyRecursive(m_model->invisibleRootItem());
}

void SynthResourceHierarchyWidget::applyInstanceVerdicts()
{
    if (m_verdicts.empty()) return;

    // [aurora2#1725 stage P4] Render what validation.json says about each instance.
    //
    // Until this ran, the panel showed every instance as equally usable and merely HID a
    // leaf that had no atoms -- so an instance synthesis had optimised away was
    // indistinguishable from a mistyped hierarchy path, and a 'partial' one (emitted, but
    // with an atom set that may be incomplete) looked perfectly healthy. Both now say so.
    //
    // 'deleted' loses its checkbox rather than being disabled outright: the row stays
    // selectable and copyable, but cannot be put in a partition, because it has no atoms to
    // constrain. Dropping the check state also keeps it out of the parent's tristate maths
    // (computeParentState() and the allChildrenChecked() test both skip non-checkable rows),
    // so one dead sub-instance no longer stops a parent from collapsing to a single element.
    std::function<void(QStandardItem*, const std::string&)> gradeRecursive =
        [&gradeRecursive, this](QStandardItem* item, const std::string& prefix)
    {
        if (!item) return;
        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child || isVprDisplayRow(child)) continue;

            const std::string path = buildChildPath(prefix, child);
            const auto found = m_verdicts.find(path);
            if (found != m_verdicts.end()) {
                const InstanceVerdict& graded = found->second;
                const QString reason = QString::fromStdString(graded.reason);

                if (graded.verdict == "deleted") {
                    const QModelIndex parentIdx = (item == m_model->invisibleRootItem())
                        ? QModelIndex() : item->index();
                    m_view->setRowHidden(row, parentIdx, false);   // marked, not hidden

                    child->setFlags(child->flags() & ~Qt::ItemIsUserCheckable);
                    child->setData(QVariant(), Qt::CheckStateRole);
                    child->setForeground(QBrush(Qt::gray));
                    child->setIcon(m_view->style()->standardIcon(QStyle::SP_MessageBoxCritical));
                    child->setToolTip(reason.isEmpty()
                        ? tr("Optimised out by synthesis: no atoms in the netlist.")
                        : tr("Optimised out by synthesis: %1").arg(reason));

                    QStringList lines;
                    lines << tr("Instance: %1").arg(QString::fromStdString(path));
                    lines << QString();
                    lines << tr("This instance is not in the synthesised netlist, so it has no "
                                "atoms to place and cannot be constrained.");
                    lines << QString();
                    lines << (reason.isEmpty()
                        ? tr("Reason: synthesis reported no atoms for it.")
                        : tr("Reason: %1").arg(reason));
                    setExplanation(item->child(row, Column::Why),
                                   tr("Why optimised out: %1").arg(QString::fromStdString(path)),
                                   lines.join(QStringLiteral("\n")));
                    if (QStandardItem* atomItem = item->child(row, Column::AtomList)) {
                        atomItem->setText(tr("(deleted)"));
                        atomItem->setForeground(QBrush(Qt::gray));
                    }
                } else if (graded.verdict == "partial") {
                    child->setForeground(QBrush(QColor(0xB8, 0x86, 0x0B)));  // dark goldenrod
                    child->setToolTip(
                        tr("This instance can be constrained, but its atom set may be "
                           "incomplete, so its resource figures are a lower bound."));
                }
            }

            gradeRecursive(child, path);
        }
    };

    gradeRecursive(m_model->invisibleRootItem(), "");
}

void SynthResourceHierarchyWidget::populateAtomColumns()
{
    if (!m_hasAtomNames) return;

    // [aurora2#1725] Leaves with no atoms are tallied here, not logged: FloorPlanningWidget
    // reads the tally back and reports it once, rather than once per tree.
    m_atomMappingReport = AtomMappingReport{};

    std::function<void(QStandardItem*, const std::string&)> populateRecursive =
        [&populateRecursive, this](QStandardItem* item, const std::string& prefix)
    {
        if (!item) return;
        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child || isVprDisplayRow(child)) continue;

            const std::string path = buildChildPath(prefix, child);

            const auto it = m_atomNames.find(path);
            const std::vector<std::string> names = (it != m_atomNames.end()) ? it->second : std::vector<std::string>{};
            const bool isLeaf = (child->rowCount() == 0);

            if (isLeaf && names.empty()) {
                // This RTL leaf has no matching VPR atom — it exists in the source but was
                // optimised away or renamed by synthesis. Hide it so the user only sees items
                // that can actually be placed; applyInstanceVerdicts() brings it back greyed
                // out if stage P4 graded it, which is the better explanation.
                const QModelIndex parentIdx = (item == m_model->invisibleRootItem())
                    ? QModelIndex() : item->index();
                m_view->setRowHidden(row, parentIdx, true);

                const auto graded = m_verdicts.find(path);
                if ((graded != m_verdicts.end()) && (graded->second.verdict == "deleted")) {
                    ++m_atomMappingReport.explainedByVerdict;
                } else {
                    m_atomMappingReport.unexplained.push_back(path);
                }
            } else if (!names.empty()) {
                if (isLeaf) {
                    if (names.size() == 1) {
                        // Single atom: show directly in this row, no child row needed
                        const std::string& name = *names.begin();
                        if (QStandardItem* atomItem = item->child(row, Column::AtomList))
                            atomItem->setText(QString::fromStdString(name));
                        if (QStandardItem* typeItem = item->child(row, Column::AtomType))
                            typeItem->setText(classifyAtomType(name));
                    } else {
                        // Multiple atoms: one child row each so each can be selected and copied
                        for (const auto& n : names) {
                            auto* col0 = new QStandardItem();
                            col0->setFlags(Qt::ItemIsEnabled);
                            col0->setData(true, kVprDisplayRowRole);
                            auto* col1 = new QStandardItem(QString::fromStdString(n));
                            col1->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                            auto* col2 = new QStandardItem(classifyAtomType(n));
                            col2->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                            child->appendRow({col0, col1, col2, new QStandardItem(),
                                              new QStandardItem()});
                        }
                    }
                }
                // Non-leaf: Atom List/Type cells are intentionally left empty —
                // a comma-separated list of all descendant atoms is unreadable.
            }
            populateRecursive(child, path);
        }
    };

    populateRecursive(m_model->invisibleRootItem(), "");
}

void SynthResourceHierarchyWidget::onItemChanged(QStandardItem* item, bool reportChanges)
{
    static std::function<Qt::CheckState(QStandardItem*)>
        computeParentState = [](QStandardItem* item)
    {
        if (!item || item->rowCount() == 0) {
            return item ? item->checkState() : Qt::Unchecked;
        }

        bool anyChildChecked = false;
        bool anyChildUnchecked = false;

        for (int row = 0; row < item->rowCount(); ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child || !child->isCheckable()) {
                continue;
            }

            const Qt::CheckState st = child->checkState();
            if (st == Qt::PartiallyChecked) {
                return Qt::PartiallyChecked;
            }
            if (st == Qt::Checked) {
                anyChildChecked = true;
            }
            if (st == Qt::Unchecked) {
                anyChildUnchecked = true;
            }

            if (anyChildChecked && anyChildUnchecked) {
                return Qt::PartiallyChecked;
            }
        }

        if (anyChildChecked && !anyChildUnchecked) {
            return Qt::Checked;
        }
        return Qt::Unchecked;
    };

    static std::function<void(QStandardItem*, Qt::CheckState)>
        setChildrenStateRecursive = [](QStandardItem* item, Qt::CheckState st)
    {
        if (!item) {
            return;
        }

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child) {
                continue;
            }
            if (child->isCheckable()) {
                child->setCheckState(st);
            }
            setChildrenStateRecursive(child, st);
        }
    };

    if (!item || !item->isCheckable()) {
        return;
    }

    const bool isParent = (item->rowCount() > 0);
    const Qt::CheckState st = item->checkState();

    // Ticking a box is not a request to reshape the tree either -- the show-only-checked
    // view calls expandAll() from here via showOnlyCheckedItems().
    ExpansionKeeper keepExpansion(m_view, m_model, m_bnExpandCollapse->isChecked());

    QSignalBlocker blocker(m_model);
    // if user clicked a parent checkbox -> force all descendants
    if (isParent && (st != Qt::PartiallyChecked)) {
        setChildrenStateRecursive(item, st);
    }

    // walk upward and update parent partial/checked state
    for (QStandardItem* parent = item->parent(); parent; parent = parent->parent()) {
        if (!parent->isCheckable()) {
            continue;
        }
        parent->setCheckState(computeParentState(parent));
    }

    if (isShowOnlyCheckedItems()) {
        showOnlyCheckedItems();
    }

    blocker.unblock();

    m_view->viewport()->update();

    if (m_selectedPartition && reportChanges) {
        fillPartitionWithSelectedElements(m_selectedPartition);
        emit partitionElementsChanged(m_selectedPartition);
    }
}

void SynthResourceHierarchyWidget::showAllItems()
{
    // "All" means all RTL instances. The per-atom display rows stay hidden while their
    // columns are hidden, otherwise clearing the filter would bring the blank placeholder
    // rows back (see setAtomColumnsVisible()).
    const bool showAtomRows = m_atomColumnsVisible;
    std::function<void(QTreeView*, QStandardItem*, const QModelIndex&)> showAllRecursive =
        [&showAllRecursive, showAtomRows](QTreeView* view, QStandardItem* item, const QModelIndex& parentIdx)
    {
        if (!item) {
            return;
        }

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            const bool isAtomRow = isVprDisplayRow(child);
            view->setRowHidden(row, parentIdx, isAtomRow && !showAtomRows);

            if (!child) {
                continue;
            }

            showAllRecursive(view, child, child->index());
        }
    };

    showAllRecursive(m_view, m_model->invisibleRootItem(), QModelIndex());

    if (m_bnExpandCollapse->isChecked()) {
      m_view->expandAll();
    }
}

void SynthResourceHierarchyWidget::showOnlyCheckedItems()
{
    // returns true if anything under parentItem should remain visible
    static std::function<bool(QTreeView*, QStandardItem*, const QModelIndex&)>
        showOnlyCheckedRecursive = [](QTreeView* view, QStandardItem* item, const QModelIndex& parentIdx)
    {
        static std::function<bool(QStandardItem*)> isMarkedVisible = [](QStandardItem* item) {
            if (!item || !item->isCheckable()) {
                return false;
            }
            const Qt::CheckState st = item->checkState();
            // include Qt::PartiallyChecked so parent paths remain visible
            return st == Qt::Checked || st == Qt::PartiallyChecked;
        };

        if (!item) {
            return false;
        }

        bool anyVisible = false;

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child) {
                continue;
            }
            // VPR display rows are not checkable; skip them and let them follow parent visibility
            if (isVprDisplayRow(child)) {
                continue;
            }

            const bool descVisible = showOnlyCheckedRecursive(view, child, child->index());
            const bool selfVisible = isMarkedVisible(child);

            const bool visible = selfVisible || descVisible;

            view->setRowHidden(row, parentIdx, !visible);

            anyVisible |= visible;
        }

        return anyVisible;
    };

    showOnlyCheckedRecursive(m_view, m_model->invisibleRootItem(), QModelIndex());

    if (m_bnExpandCollapse->isChecked()) {
      m_view->expandAll();
    }
}

void SynthResourceHierarchyWidget::showFilteredItems(const std::string& pattern)
{
    std::unordered_set<std::string_view> matches;
    matches.reserve(m_rawElements.size());

    for (const auto& s : m_rawElements) {
        if (s.find(pattern) != std::string::npos) {
            matches.emplace(s);
        }
    }

    // returns true if anything under parentItem should remain visible
    std::function<bool(QStandardItem*, const std::string&)> updateVisibility =
        [&](QStandardItem* item, const std::string& prefix) -> bool
    {
        if (!item) {
            return false;
        }

        const bool isRoot = (item == m_model->invisibleRootItem());
        const std::string name = item->text().toStdString();
        const std::string itemPath = prefix.empty() ? name : (prefix + "." + name);

        bool anyChildVisible = false;

        const int rows = item->rowCount();
        for (int row = 0; row < rows; ++row) {
            QStandardItem* child = item->child(row, Column::Netlist);
            if (!child) {
                continue;
            }
            // VPR display rows are not RTL paths; skip them and let them follow parent
            // visibility -- unless their columns are hidden, in which case they are the blank
            // placeholder rows setAtomColumnsVisible() suppresses.
            if (isVprDisplayRow(child)) {
                m_view->setRowHidden(row, isRoot ? QModelIndex() : item->index(),
                                     !m_atomColumnsVisible);
                continue;
            }

            // child full path
            const std::string childPath = buildChildPath(itemPath, child);

            const bool childDescVisible = updateVisibility(child, itemPath);
            const bool childSelfVisible = matches.contains(childPath);
            const bool childVisible = childSelfVisible || childDescVisible;

            m_view->setRowHidden(row, isRoot? QModelIndex() : item->index(), !childVisible);

            anyChildVisible |= childVisible;
        }

        const bool selfVisible = (!itemPath.empty() && matches.contains(itemPath));
        return selfVisible || anyChildVisible;
    };

    QSignalBlocker blockModel(m_view);
    updateVisibility(m_model->invisibleRootItem(), "");
    blockModel.unblock();

    m_view->viewport()->update();
}

void SynthResourceHierarchyWidget::updateViewLabel() const {
    if (!m_viewLabelTemplate.isEmpty()) {
      QString label{m_viewLabelTemplate};
        if (m_selectedPartition) {
            // [aurora2#1725] Required-of-available clb/dsp/bram used to be appended
            // here; now shown in PartitionsListWidget's CLB/DSP/BRAM columns instead,
            // so this label goes back to just the name.
            m_lbView->setText(label.replace("%1", QString::fromStdString(m_selectedPartition->name())));
        } else {
            m_lbView->setText(label.replace("'%1'", ""));
        }
    }
}

} // namespace fp
