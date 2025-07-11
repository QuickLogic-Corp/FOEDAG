#pragma once

#include <QDialog>
#include <QString>

#include <vector>

class QVBoxLayout;

namespace FOEDAG {

class SelectionDialog : public QDialog {
    Q_OBJECT

public:
    explicit SelectionDialog(const QString& title, const std::vector<std::string>& items, QWidget* parent = nullptr);
    ~SelectionDialog();
    
    QString selectedText() const { return m_selectedText; }

private:
    QString m_selectedText;
    QVBoxLayout* m_layout{nullptr};
};

}  // namespace FOEDAG
