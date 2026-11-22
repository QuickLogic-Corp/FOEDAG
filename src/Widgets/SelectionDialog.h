#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QKeyEvent>

#include <set>

namespace FOEDAG {

class SelectionDialog : public QDialog {
    Q_OBJECT
    // The button texts, for GUI tests.
    Q_PROPERTY(QStringList items READ items)

public:
    explicit SelectionDialog(const QString& title, const std::set<std::string>& items, QWidget* parent = nullptr);
    ~SelectionDialog();

    QString selectedText() const { return m_selectedText; }
    QStringList items() const { return m_items; }

protected:
    void keyPressEvent(QKeyEvent* event);

private:
    QString m_selectedText;
    QStringList m_items;
};

}  // namespace FOEDAG
