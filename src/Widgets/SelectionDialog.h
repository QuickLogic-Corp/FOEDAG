#pragma once

#include <QDialog>
#include <QString>
#include <QKeyEvent>

#include <set>

namespace FOEDAG {

class SelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit SelectionDialog(const QString& title, const std::set<std::string>& items, QWidget* parent = nullptr);
    ~SelectionDialog();

    QString selectedText() const { return m_selectedText; }

protected:
    void keyPressEvent(QKeyEvent* event);

private:
    QString m_selectedText;
};

}  // namespace FOEDAG
