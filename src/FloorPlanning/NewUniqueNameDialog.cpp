#include "NewUniqueNameDialog.h"

#include <QLineEdit>
#include <QLabel>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPushButton>

#include <QDebug>

namespace fp {

NewUniqueNameDialog::NewUniqueNameDialog(const QString& title, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);

    m_edit = new QLineEdit(this);

    m_error = new QLabel(this);
    m_error->setText("");
    m_error->setVisible(false);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    m_ok = m_buttons->button(QDialogButtonBox::Ok);
    m_ok->setText("Apply");
    m_ok->setEnabled(false);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_edit);
    layout->addWidget(m_error);
    layout->addStretch();
    layout->addWidget(m_buttons);

    connect(m_edit, &QLineEdit::textChanged, this, &NewUniqueNameDialog::validate);
    connect(m_buttons, &QDialogButtonBox::accepted, this, [this](){
        m_edit->clear();
        QDialog::accept();
    });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    validate(m_edit->text());

    QFontMetrics fm(font());
    int titleWidth = fm.horizontalAdvance(title) + 80;

    adjustSize();
    setMinimumWidth(qMax(width(), titleWidth));
}

void NewUniqueNameDialog::validate(const QString& raw)
{
    const QString candidate = raw.trimmed();
    const bool empty = candidate.isEmpty();
    const bool exists = m_existedNames.find(candidate.toStdString()) != m_existedNames.end();
    const bool ok = !empty && !exists;

    // Red text if invalid
    QPalette pal = m_edit->palette();
    pal.setColor(QPalette::Base, ok ? Qt::white : QColor(255, 200, 200));
    pal.setColor(QPalette::Text, Qt::black);
    m_edit->setPalette(pal);

    // Error message + disable Apply
    if (empty) {
        m_error->setText("Name can't be empty.");
        m_error->setVisible(true);
    } else if (exists) {
        m_error->setText(QString("Name '%1' is already taken.").arg(candidate));
        m_error->setVisible(true);
    } else {
        m_error->setVisible(false);
    }

    m_ok->setEnabled(ok);

    if (ok) {
        m_name = m_edit->text().trimmed().toStdString();
    }
}

} // namespace fp
