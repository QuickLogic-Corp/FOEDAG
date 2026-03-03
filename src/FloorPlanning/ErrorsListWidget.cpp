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
    const int m = FP_UI_MARGIN;
    layout->setContentsMargins(m,m,m,m);
    layout->setSpacing(m);
    setLayout(layout);

    m_lbErrors = new QLabel("Errors:");
    m_lwErrors = new QListWidget;

    layout->addWidget(m_lbErrors);
    layout->addWidget(m_lwErrors);

    m_lbWarnings = new QLabel("Warnings:");
    m_lwWarnings = new QListWidget;

    layout->addWidget(m_lbWarnings);
    layout->addWidget(m_lwWarnings);

    QPixmap epm(":/error.png");
    epm = epm.scaled(16,16, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_errorIcon = QIcon(epm);

    QPixmap wpm(":/warning.png");
    wpm = wpm.scaled(16,16, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_warningIcon = QIcon(wpm);
}

void ErrorsListWidget::setIssues(const std::unordered_map<std::string, std::string>& errors, const std::unordered_map<std::string, std::string>& warnings)
{
    clear();

    for (const auto& [error, toolTip]: errors) {
        QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(error));
        item->setIcon(m_errorIcon);
        if (!toolTip.empty()) {
          item->setToolTip(QString::fromStdString(toolTip));
        }
        m_lwErrors->addItem(item);
    }

    for (const auto& [warning, toolTip]: warnings) {
      QListWidgetItem* item = new QListWidgetItem(QString::fromStdString(warning));
      item->setIcon(m_warningIcon);
      if (!toolTip.empty()) {
        item->setToolTip(QString::fromStdString(toolTip));
      }
      m_lwWarnings->addItem(item);
    }
    updateVisibility(!errors.empty(), !warnings.empty());
}

void ErrorsListWidget::clear()
{
    m_lwErrors->clear();
    m_lwWarnings->clear();

    updateVisibility(false, false);
}

void ErrorsListWidget::updateVisibility(bool hasErrors, bool hasWarnings)
{
  m_lbErrors->setVisible(hasErrors);
  m_lwErrors->setVisible(hasErrors);

  m_lbWarnings->setVisible(hasWarnings);
  m_lwWarnings->setVisible(hasWarnings);
}

} // namespace fp
