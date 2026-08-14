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

    void createNewPartition();
    void checkErrors();
    void onNotify(QString, QString);

    void updateSaveQdcButtonEnability();
};

}  // namespace fp
