#pragma once

#include <QWidget>

#include "domain/ScanTypes.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

class VirtualTrashService;

// Review pane for staged deletions. Nothing here touches the file system until the user
// reviews the item list and confirms moving the staged items to the Windows Recycle Bin.
class TrashPanel : public QWidget {
    Q_OBJECT

public:
    explicit TrashPanel(VirtualTrashService *trashService, QWidget *parent = nullptr);

    void setSelection(const TreeEntry &entry);
    void refresh();

private:
    void stageSelection();
    void unstageSelected();
    void clearStaged();
    void moveStagedToRecycleBin();

    VirtualTrashService *m_trashService;
    QLabel *m_summaryLabel;
    QLabel *m_statusLabel;
    QPushButton *m_stageButton;
    QPushButton *m_unstageButton;
    QPushButton *m_clearButton;
    QPushButton *m_recycleButton;
    QTableWidget *m_table;
    TreeEntry m_selection;
};

} // namespace opentree
