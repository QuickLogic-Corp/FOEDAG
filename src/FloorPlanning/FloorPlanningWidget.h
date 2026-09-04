#pragma once

#include <QWidget>

#include "AtomSets.h"
#include "DeviceGridDescriptor.h"
#include "DeviceGrid.h"
#include "Partition.h"
#include "NaturalSort.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

class QAction;
class QSplitter;

namespace FOEDAG { class RoundProgressWidget; }

namespace fp {

class SynthResourceHierarchyWidget;
class DeviceGridWidget;
class PartitionsListWidget;
class IssuesListWidget;

class FloorPlanningWidget : public QWidget {
    Q_OBJECT

public:
    explicit FloorPlanningWidget(const QString& projectName, QWidget* parent = nullptr);
    ~FloorPlanningWidget();

    void loadNetList(const NaturalStringSet& elements);

    // [aurora2#1725] Root both trees at the top module. Must precede loadNetList().
    void setTopInstance(const std::string& path);

    // [aurora2#1725 stage P3] Feeds the "Atom List"/"Type" columns: RTL path -> the
    // atom names it covers, from atomsets.json.
    void setAtomNames(std::map<std::string, std::vector<std::string>, NaturalLess> atomNames);

    // [aurora2#1725] Per-instance cell-type counts, for the Atoms tab of "Selected RTL
    // Resources". Only the netlist tree has that table, so only it is told.
    void setAtomResources(AtomResourceMap atomResources);

    // [aurora2#2377] Feeds the "Type" column ground truth: each atom's real Yosys cell
    // type, from <top>_post_synth_debug.json. Both trees show the Type column, so both
    // are told, same as setAtomNames().
    void setAtomTypes(AtomTypeMap atomTypes);

    // [aurora2#1725 stage P3] atomsets.json's atoms_per_tile: how many clb atoms pack into
    // one tile, so a partition's required clb can be reported in the same unit as what its
    // regions have available.
    void setAtomsPerTile(int atomsPerTile) { Partition::setAtomsPerTile(atomsPerTile); }

    // [aurora2#1725 stage P4] Per-instance verdicts from validation.json, so the trees can
    // grey out what synthesis deleted and flag what is only partially trustworthy.
    void setInstanceVerdicts(std::map<std::string, InstanceVerdict> verdicts);

    // [aurora2#1725 stage P7] Measured placement per constrained instance, so the trees can
    // show whether a region constraint actually took effect: a tick when every atom of an
    // instance landed inside its region, a warning when some did not.
    void setPlacementVerdicts(std::map<std::string, InstancePlacement> placements);

    // [aurora2#1725 stage P7] design_resources.json: per-instance clb/dsp/bram plus the
    // tier that says how they were obtained. Gives the panel sizing figures before
    // synthesis has run -- until now it had none at all -- and reports the tier, which
    // A.13.5 requires be surfaced rather than folded silently into the numbers.
    void setDesignResources(DesignResources resources);

    // [aurora2#1725 stage P7] Whether the floorplan differs from the .qdc on disk. Read by
    // MainWindow before it reloads this panel's data after a compile: a reload repopulates
    // the partitions from disk, so doing it over unsaved edits would discard them.
    bool hasUnsavedChanges() const { return m_hasUnsavedChanges; }

    // [aurora2#1725 stage P7] Save on the user's behalf when they answer "Save" to that
    // prompt. No-ops when there is nothing to write, exactly as the button does.
    void saveUnsavedChanges() { saveQdc(); }

    void setDeviceGridDescriptor(const DeviceGridDescriptorPtr& deviceDescriptor);
    void setQdcFilePath(const std::filesystem::path&, bool load = true);

private slots:
    void onPartitionsChanged(const std::map<int, PartitionPtr>& partitions);
    void onCheckIssuesFinished(DeviceGrid::IssuesPtr);

signals:
    void closed();
    void qdcFileSaved();

    // [aurora2#1725] Goes to the compiler log rather than the terminal, so what the panel
    // found is recorded where the rest of the flow's output is. Connected in main_window_ql.
    void logMessage(QString message);

protected:
    void resizeEvent(QResizeEvent* event) override final;
    void closeEvent(QCloseEvent* event) override final;
    void keyPressEvent(QKeyEvent* event) override final;

private:
    FOEDAG::RoundProgressWidget* m_busyOverlayWidget{nullptr};
    // [aurora2#1725] QActions, not QPushButtons: the device toolbar is a QToolBar now, and
    // only an action can move into its extension menu when the pane is too narrow to show
    // everything. Enabling/checking them works the same either way.
    QAction* m_actSaveQdc{nullptr};
    QAction* m_actRemoveSelectedRegion{nullptr};
    QAction* m_actZoomInRegion{nullptr};
    QAction* m_actDrawRegion{nullptr};
    QAction* m_actPartitionColor{nullptr};
    SynthResourceHierarchyWidget* m_synthResourcesWidget{nullptr};
    SynthResourceHierarchyWidget* m_partitionResourcesWidget{nullptr};
    DeviceGridWidget* m_deviceWidget{nullptr};
    PartitionsListWidget* m_partitionsListWidget{nullptr};
    IssuesListWidget* m_issuesListWidget{nullptr};

    // [aurora2#1725] The three panes, and whether they have been given their opening widths
    // yet. See applyInitialSplitterSizes().
    QSplitter* m_splitter{nullptr};
    bool m_splitterSized = false;
    void applyInitialSplitterSizes();

    // [aurora2#1725] Options menu state, persisted in floorplanning.cfg beside the .qdc.
    // The actions are the single source of truth for the values -- they are what the user
    // sets and what saveOptions() reads back.
    QAction* m_actTreatWarningsAsErrors{nullptr};
    QAction* m_actShowAtomColumns{nullptr};
    std::filesystem::path m_optionsFilePath;

    void loadOptions();
    void saveOptions() const;

    void checkErrors();
    void onNotify(QString, QString);

    // [aurora2#1725] Shared by the Save QDC button and the Ctrl+S shortcut.
    void saveQdc();
    // Load, then clear the unsaved-changes flag the load itself sets.
    void loadQdc();

    // [aurora2#1725] Whether the floorplan differs from the .qdc on disk. Gates the Save QDC
    // button, which used to come up enabled straight after a load.
    bool m_hasUnsavedChanges = false;

    void updateSaveQdcButtonEnability();
};

}  // namespace fp
