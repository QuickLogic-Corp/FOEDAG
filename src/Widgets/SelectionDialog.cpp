#include "SelectionDialog.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QString>

namespace FOEDAG {

SelectionDialog::SelectionDialog(const QString& title, const std::set<std::string>& items, QWidget* parent)
    : QDialog(parent), m_layout(new QVBoxLayout(this))
{
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint); // hide close button
    setWindowTitle(title);

    for (const auto& item: items) {
        QString prettyItem = QString::fromStdString(item);
        if (prettyItem.startsWith("_")) {
            prettyItem.remove(0, 1);
        }

        auto* button = new QPushButton(prettyItem, this);
        m_layout->addWidget(button);

        connect(button, &QPushButton::clicked, this, [this, item]() {
            m_selectedText = QString::fromStdString(item);
            accept(); // Close the dialog and unblock exec()
        });
    }

    setLayout(m_layout);
}

SelectionDialog::~SelectionDialog()
{

}

void SelectionDialog::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        event->ignore();
        return;
    }
    QDialog::keyPressEvent(event);
}

} // namespace FOEDAG