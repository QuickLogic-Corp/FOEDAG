#pragma once

#include "AtomSets.h"
#include "Partition.h"
#include "NaturalSort.h"

#include <QWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QLabel>

#include <map>
#include <memory>
#include <set>
#include <unordered_set>
#include <string>
#include <vector>

class QLineEdit;
class QCheckBox;
class QSplitter;

namespace fp {

class CheckableButton;
class SelectedResourcesWidget;

class SynthResourceHierarchyWidget : public QWidget {
    Q_OBJECT

    enum Column {
      Netlist = 0,
      AtomList = 1,
      AtomType = 2,
      Partitions = 3,
      // [aurora2#1725 stage P7] Clickable "?" for rows that have something to explain --
      // a partial placement or an instance synthesis optimised out. Last logically so the
      // existing column indices are untouched; moved next to the name visually in build().
      Why = 4
    };
public:
    enum Flag {
        None                 = 0,
        HidePartitionsColumn = 1 << 0,
        ShowOnlyCheckedItems = 1 << 1,
        // [aurora2#1725] Puts a "Selected RTL Resources" table under the tree and lets rows
        // be multi-selected (Ctrl/Shift) to feed it. Off for the partition-netlist tree,
        // which shows what one partition already holds -- the partitions table answers that
        // question for it, and answering it twice in one panel invites the two to disagree.
        ShowSelectedResources = 1 << 2
    };

    explicit SynthResourceHierarchyWidget(int flags = Flag::None, QWidget* parent = nullptr);

    void setViewLabelTemplate(const QString& label) {
        m_viewLabelTemplate = label;
        m_lbView->setVisible(true);
        updateViewLabel();
    }
    void build(const NaturalStringSet& elements);

    // [aurora2#1725] The top module's path, which build() makes the root of the tree. Must
    // be set BEFORE build(): addPath() reads it as it creates each item. Empty means "no
    // top entry" -- an older instances.json -- and the tree keeps its previous flat-roots
    // shape rather than inventing a root.
    void setTopInstance(const std::string& path) { m_topInstance = path; }

    // [aurora2#1725 stage P3] Populates the "Atom List" and "Type" columns: RTL path ->
    // the atom names it covers, from atomsets.json.
    void setAtomNames(std::map<std::string, std::vector<std::string>, NaturalLess> atomNames);

    // [aurora2#1725] Cell-type counts per instance, for the Atoms tab of the selected-
    // resources table. Independent of setAtomNames(): a file without a "resources" map
    // leaves that tab empty and everything else working.
    void setAtomResources(AtomResourceMap atomResources);

    // [aurora2#2377] Each atom's real Yosys cell type, from <top>_post_synth_debug.json
    // (AtomSets::loadAtomTypes()). Independent of the two setters above: a project with no
    // debug json falls back to classifyAtomType()'s name-substring guess, same as before
    // this existed.
    void setAtomTypes(AtomTypeMap atomTypes);

    // [aurora2#1725] "Show Atom List and Type columns" from the Options menu. Off by
    // default: the atom names are long and mostly of interest when debugging a mapping.
    void setAtomColumnsVisible(bool visible);

    // [aurora2#1725] What build() found while matching RTL leaves to atoms: the leaves with
    // no atoms at all, split by whether stage P4 accounts for them. Reported by
    // FloorPlanningWidget rather than logged from here, because it owns two of these trees
    // and logging from each said the same thing twice.
    struct AtomMappingReport {
        int explainedByVerdict = 0;          // graded 'deleted' -- ordinary constant folding
        std::vector<std::string> unexplained;  // atomsets.json and validation.json disagree
        int total() const {
            return explainedByVerdict + static_cast<int>(unexplained.size());
        }
    };
    const AtomMappingReport& atomMappingReport() const { return m_atomMappingReport; }

    // [aurora2#1725 stage P4] Grades the tree: a deleted instance is shown greyed and cannot be checked, a partial
    // one stays selectable but is flagged. Optional -- without it every instance renders as
    // equally usable, which is what the panel did before stage P4 was wired up.
    void setInstanceVerdicts(std::map<std::string, InstanceVerdict> verdicts);

    // [aurora2#1725 stage P7] Placement status per constrained instance, from
    // <project>_floorplanning_placement.json: a green tick when every atom landed inside its
    // region, a warning triangle when some did not. Optional and independent of the P4
    // verdicts above -- an unconstrained instance gets no icon at all, because there is no
    // region it could be inside or outside of.
    void setPlacementVerdicts(std::map<std::string, InstancePlacement> placements);

    void onPartitionsChanged(const std::map<int, PartitionPtr>& partitions);
    void selectPartition(const PartitionPtr&);
    void unselectPartition();

 signals:
     void partitionElementsChanged(PartitionPtr);

private:
    int m_flags = Flag::None;
    CheckableButton* m_bnExpandCollapse{nullptr};
    QLineEdit* m_leFilter{nullptr};
    QString m_viewLabelTemplate;
    QLabel* m_lbView{nullptr};
    QTreeView* m_view{nullptr};
    QStandardItemModel* m_model{nullptr};

    PartitionPtr m_selectedPartition;
    std::unordered_set<std::string> m_rawElements;

    // Unset (m_hasAtomNames == false) means "no data source connected" -- the whole
    // column stays blank, nothing gets hidden. Once set() (even to an empty map, e.g.
    // atomsets.json legitimately found no atoms), a leaf with no entry is a real
    // "optimised away" case and gets hidden. Distinguishing these is why this isn't
    // just an empty-map check.
    std::string m_topInstance;
    bool m_hasAtomNames = false;
    std::map<std::string, std::vector<std::string>, NaturalLess> m_atomNames;
    AtomResourceMap m_atomResources;
    AtomTypeMap m_atomTypes;

    std::map<std::string, InstanceVerdict> m_verdicts;
    std::map<std::string, InstancePlacement> m_placements;
    bool m_atomColumnsVisible = false;
    AtomMappingReport m_atomMappingReport;

    // [aurora2#1725] Atoms covered by an RTL path -- its own atomsets.json entry when it
    // has one, else the union of the descendant entries that do (see the .cpp).
    std::set<std::string> atomNamesFor(const std::string& path) const;

    // [aurora2#1725] The "Selected RTL Resources" table, when Flag::ShowSelectedResources
    // is set; null otherwise, which is what every use of it tests for.
    SelectedResourcesWidget* m_selectedResourcesWidget{nullptr};

    // Holds m_view and m_selectedResourcesWidget when the latter exists, so their shared
    // border is draggable instead of the tree's height being whatever a VBox leaves it.
    // Null when there is no resources table to split against.
    QSplitter* m_splitter{nullptr};

    // Re-tallies the selected rows into that table. Called on every selection change, and
    // again whenever the atom names arrive or the tree is rebuilt, since either changes the
    // answer for a selection that has not moved.
    void updateSelectedResources();

    // The RTL path an item stands for, walked up through its parents. buildChildPath()'s
    // rules, applied top down, so a bus bit joins to its parent without a dot.
    std::string pathForItem(const QStandardItem* item) const;

    bool showsSelectedResources() const { return m_flags & Flag::ShowSelectedResources; }

    void filterRawElemenets(const std::string& pattern);
    void fillPartitionWithSelectedElements(const PartitionPtr& partition) const;
    void populateAtomColumns();
    void applyInstanceVerdicts();
    void applyPlacementVerdicts();
    // Fills the Why cell of one row and stores the text its popup shows.
    void setExplanation(QStandardItem* whyItem, const QString& title, const QString& body);

    void addPath(const std::string&);
    void onItemChanged(QStandardItem*, bool reportChanges);

    void showFilteredItems(const std::string& pattern);
    void showOnlyCheckedItems();
    void showAllItems();

    bool isShowOnlyCheckedItems() const { return m_flags & Flag::ShowOnlyCheckedItems; }
    bool isPartitionsColumnHidden() const { return m_flags & Flag::HidePartitionsColumn; }

    void updateViewLabel() const;
};

}  // namespace fp
