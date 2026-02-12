#pragma once

#include <QWidget>

#include "DeviceGridDescriptor.h"
#include "Partition.h"

#include <map>
#include <set>

class QPushButton;

namespace FOEDAG { class RoundProgressWidget; }

namespace fp {

class CheckableButton;
class SynthResourceHierarchyWidget;
class DeviceGridWidget;
class PartitionsListWidget;
class NewUniqueNameDialog;
class ErrorsListWidget;

class FloorPlanningWidget : public QWidget {
    Q_OBJECT

public:
    explicit FloorPlanningWidget(const QString& projectName, QWidget* parent = nullptr);
    ~FloorPlanningWidget();

    void loadNetList(const std::set<std::string>& elements);
    void setDeviceGridDescriptor(const DeviceGridDescriptorPtr& deviceDescriptor);
    void setQdcFilePath(const std::filesystem::path&, bool load = true);

private slots:
    void onPartitionsChanged(const std::map<int, PartitionPtr>& partitions);
    void onCheckErrorsFinished(std::unordered_set<std::string> errors);

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
    SynthResourceHierarchyWidget* m_synthResourcesWidget{nullptr};
    SynthResourceHierarchyWidget* m_partitionResourcesWidget{nullptr};
    DeviceGridWidget* m_deviceWidget{nullptr};
    PartitionsListWidget* m_partitionsListWidget{nullptr};
    NewUniqueNameDialog* m_newPartitionNameDialog{nullptr};
    ErrorsListWidget* m_errorsListWidget{nullptr};

    void createNewPartition();
    void checkErrors();
    void onNotify(QString, QString);
};

}  // namespace fp
