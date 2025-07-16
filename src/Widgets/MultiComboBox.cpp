#include "MultiComboBox.h"

#include <QStandardItemModel>
#include <QAbstractItemView>
#include <QStandardItem>
#include <QLineEdit>
#include <QVBoxLayout>

#include <QDebug>

namespace FOEDAG {

MultiComboBox::MultiComboBox(QWidget* parent): QWidget(parent) {
    m_combo = new QComboBox();
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);
    setLayout(layout);
    layout->addWidget(m_combo);

    m_combo->setModel(new QStandardItemModel(this));
    m_combo->view()->setSelectionMode(QAbstractItemView::NoSelection);  // disable selected highlight

    // these settings are required for multiselection, otherwise the selection event didn't happened in FOEDAG invironment
    m_combo->setEditable(true);
    m_combo->lineEdit()->setReadOnly(true); 
    //

    connect(m_combo->model(), &QAbstractItemModel::dataChanged,
            this, &MultiComboBox::updateDisplayText);

    connect(m_combo->model(), &QAbstractItemModel::dataChanged,
            this, [this]() {
                emit currentTextChanged(selectedItems().join(","));
    });
}

void MultiComboBox::addItem(const QString& text) {
    QStandardItem* item = new QStandardItem(text);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setData(Qt::Unchecked, Qt::CheckStateRole);
    static_cast<QStandardItemModel*>(m_combo->model())->appendRow(item);
    updateDisplayText();
}

QString MultiComboBox::currentText() const
{
    return m_combo->lineEdit()->text();
}

void MultiComboBox::setCurrentText(const QString& text)
{
    QList<QString> selectedItems = text.split(",");
    setSelectedItems(selectedItems);
}

void MultiComboBox::setSelectedItems(const QList<QString>& selectedItems)
{
    QStandardItemModel* model = static_cast<QStandardItemModel*>(m_combo->model());
    for (int i=0; i<model->rowCount(); ++i) {
        QStandardItem* item = model->item(i);
        if (selectedItems.contains(item->text())) {
            item->setCheckState(Qt::Checked);
        } else {
            item->setCheckState(Qt::Unchecked);
        }
    }

    updateDisplayText();
}

QList<QString> MultiComboBox::selectedItems() const {
    QList<QString> result;
    auto m = static_cast<QStandardItemModel*>(m_combo->model());
    for (int i=0; i<m->rowCount(); ++i) {
        QStandardItem* item = m->item(i);
        if (item->checkState() == Qt::Checked) {
            result << item->text();
        }
    }
    return result;
}

void MultiComboBox::updateDisplayText() {
    m_combo->lineEdit()->setText(selectedItems().join(","));
}

}  // namespace FOEDAG