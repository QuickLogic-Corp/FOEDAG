#include "ErrorsListWidget.h"

#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QDebug>

namespace fp {

ErrorsListWidget::ErrorsListWidget(QWidget* parent)
    : QWidget(parent)
{
    QVBoxLayout* layout = new QVBoxLayout;
    layout->setContentsMargins(0,0,0,0);
    setLayout(layout);

    QLabel* lbErrors = new QLabel("Errors:");
    m_lwErrors = new QListWidget;

    layout->addWidget(lbErrors);
    layout->addWidget(m_lwErrors);

    QPixmap pm(":/images/error.png");
    pm = pm.scaled(16,16, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_errorIcon = QIcon(pm);
}

void ErrorsListWidget::setErrors(const std::unordered_set<std::string>& errors)
{
    setEnabled(true);

    m_lwErrors->clear();
    for (const std::string& error: errors) {
        QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(error));
        item->setIcon(m_errorIcon);
        m_lwErrors->addItem(item);
    }
}

void ErrorsListWidget::clear()
{
    setEnabled(false);
    m_lwErrors->clear();
}

} // namespace fp
