#include "RegionsListWidget.h"

namespace fp {

RegionsListWidget::RegionsListWidget(QWidget* parent)
    : QListWidget(parent)
{
    connect(this, &QListWidget::itemSelectionChanged,
            this, [this]{
                QList<QListWidgetItem*> items = selectedItems();
                if (!items.isEmpty()) {
                    QListWidgetItem* item = items.first();
                    emit selectionChanged(item->text());
                }
            });
}

void RegionsListWidget::onRegionsChanged(const std::map<int, RegionPtr>& regions)
{
    clear();

    for (const auto& [id, region]: regions) {
        addItem(QString::number(id));
    }
}

void RegionsListWidget::onRegionSelectedOutside(const RegionPtr& region)
{
    QList<QListWidgetItem*> items = findItems(QString::number(region->id()), Qt::MatchExactly);
    if (!items.isEmpty()) {
        QListWidgetItem* item = items.first();
        scrollToItem(item);

        blockSignals(true);
        setCurrentItem(item);
        item->setSelected(true);
        blockSignals(false);
    }
}

} // namespace fp
