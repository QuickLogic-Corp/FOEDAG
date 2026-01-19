#pragma once

#include <QListWidget>

#include "Region.h"

#include <map>

namespace fp {

class RegionsListWidget final : public QListWidget {
    Q_OBJECT
public:
    RegionsListWidget(QWidget* parent = nullptr);

signals:
    void selectionChanged(QString);

public slots:
    void onRegionsChanged(const std::map<int, RegionPtr>& regions);
    void onRegionSelectedOutside(const RegionPtr& region);

private:

};

}  // namespace fp
