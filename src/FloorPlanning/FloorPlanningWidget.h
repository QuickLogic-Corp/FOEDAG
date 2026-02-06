#pragma once

#include <QWidget>

#include "DeviceGridDescriptor.h"
#include "Partition.h"

#include <map>
#include <set>

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
    void closeEvent(QCloseEvent* event) override {
        emit closed();
        QWidget::closeEvent(event);
    }

private:
    QPushButton* m_bnSaveQdc{nullptr};
    QPushButton* m_bnRemovePartition{nullptr};
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
