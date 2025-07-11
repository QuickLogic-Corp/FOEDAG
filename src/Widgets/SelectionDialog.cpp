#include "SelectionDialog.h"

#include <QVBoxLayout>
#include <QPushButton>
#include <QString>

namespace FOEDAG {

SelectionDialog::SelectionDialog(const QString& title, const std::vector<std::string>& items, QWidget* parent)
    : QDialog(parent), m_layout(new QVBoxLayout(this))
{
    setWindowTitle(title);

    for (const auto& item: items) {
        auto* button = new QPushButton(QString::fromStdString(item), this);
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

} // namespace FOEDAG