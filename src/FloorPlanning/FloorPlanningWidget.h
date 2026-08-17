#pragma once

#include <QWidget>

#include "DeviceGridDescriptor.h"
#include "DeviceGrid.h"
#include "Partition.h"
#include "NaturalSort.h"

#include <map>
#include <memory>
#include <set>
#include <vector>

class QPushButton;
class QAction;

namespace FOEDAG { class RoundProgressWidget; }

namespace fp {

class CheckableButton;
class SynthResourceHierarchyWidget;
class DeviceGridWidget;
class PartitionsListWidget;
class NewUniqueNameDialog;
class IssuesListWidget;

class FloorPlanningWidget : public QWidget {
    Q_OBJECT

public:
    explicit FloorPlanningWidget(const QString& projectName, QWidget* parent = nullptr);
    ~FloorPlanningWidget();

    void loadNetList(const NaturalStringSet& elements);

    // [aurora2#1725 stage P3] Feeds the "Atom List"/"Type" columns: RTL path -> the
    // atom names it covers, from atomsets.json.
    void setAtomNames(std::map<std::string, std::vector<std::string>, NaturalLess> atomNames);

    // [aurora2#1725 stage P3] atomsets.json's atoms_per_tile: how many clb atoms pack into
    // one tile, so a partition's required clb can be reported in the same unit as what its
    // regions have available.
    void setAtomsPerTile(int atomsPerTile) { Partition::setAtomsPerTile(atomsPerTile); }

    // [aurora2#1725 stage P4] Per-instance verdicts from validation.json, so the trees can
    // grey out what synthesis deleted and flag what is only partially trustworthy.
    void setInstanceVerdicts(std::map<std::string, InstanceVerdict> verdicts);

    void setDeviceGridDescriptor(const DeviceGridDescriptorPtr& deviceDescriptor);
    void setQdcFilePath(const std::filesystem::path&, bool load = true);

private slots:
    void onPartitionsChanged(const std::map<int, PartitionPtr>& partitions);
    void onCheckIssuesFinished(DeviceGrid::IssuesPtr);

signals:
    void closed();
    void qdcFileSaved();

protected:
    void resizeEvent(QResizeEvent* event) override final;
    void closeEvent(QCloseEvent* event) override final;
    void keyPressEvent(QKeyEvent* event) override final;

private:
    FOEDAG::RoundProgressWidget* m_busyOverlayWidget{nullptr};
    QPushButton* m_bnSaveQdc{nullptr};
    QPushButton* m_bnRemoveSelectedPartition{nullptr};
    QPushButton* m_bnRemoveAllPartitions{nullptr};
    CheckableButton* m_bnZoomInRegion{nullptr};
    CheckableButton* m_drawRegion{nullptr};
    QPushButton* m_bnPartitionColor{nullptr};
    SynthResourceHierarchyWidget* m_synthResourcesWidget{nullptr};
    SynthResourceHierarchyWidget* m_partitionResourcesWidget{nullptr};
    DeviceGridWidget* m_deviceWidget{nullptr};
    PartitionsListWidget* m_partitionsListWidget{nullptr};
    NewUniqueNameDialog* m_newPartitionNameDialog{nullptr};
    IssuesListWidget* m_issuesListWidget{nullptr};

    // [aurora2#1725] Options menu state, persisted in floorplanning.cfg beside the .qdc.
    // The actions are the single source of truth for the values -- they are what the user
    // sets and what saveOptions() reads back.
    QAction* m_actTreatWarningsAsErrors{nullptr};
    QAction* m_actShowAtomColumns{nullptr};
    std::filesystem::path m_optionsFilePath;

    void loadOptions();
    void saveOptions() const;

    void createNewPartition();
    void checkErrors();
    void onNotify(QString, QString);

    void updateSaveQdcButtonEnability();
};

}  // namespace fp
