#pragma once

#include <QWidget>

#include "DeviceGridDescriptor.h"
#include "Partition.h"

#include <map>

class QLabel;
class QPushButton;

namespace fp {

class SynthResourceHierarchyWidget;
class DeviceGridWidget;
class PartitionsListWidget;
class NewUniqueNameDialog;
class ErrorsListWidget;

class FloorPlanningWidget : public QWidget {
    Q_OBJECT

public:
    explicit FloorPlanningWidget(QWidget* parent = nullptr);
    void loadNetList(const std::set<std::string>& elements);
    void setDeviceGridDescriptor(const DeviceGridDescriptorPtr& deviceDescriptor);

private slots:
    void onPartitionsChanged(const std::map<int, PartitionPtr>& partitions);
    void onCheckErrorsFinished(std::unordered_set<std::string> errors);

private:
    // QLabel* m_lbStatus{nullptr};
    QPushButton* m_bnSaveQdc{nullptr};
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
