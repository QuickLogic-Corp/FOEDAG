#include "SelectionDialog.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QString>

namespace FOEDAG {

SelectionDialog::SelectionDialog(const QString& title, const std::set<std::string>& items, QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint); // hide close button
    setWindowTitle(title);

    QVBoxLayout* layout = new QVBoxLayout(this);
    setLayout(layout);

    for (const auto& item: items) {
        auto* button = new QPushButton(QString::fromStdString(item), this);
        layout->addWidget(button);

        connect(button, &QPushButton::clicked, this, [this, item]() {
            m_selectedText = QString::fromStdString(item);
            accept(); // Close the dialog and unblock exec()
        });
    }
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