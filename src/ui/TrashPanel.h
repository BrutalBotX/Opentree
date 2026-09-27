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

    // Re-reads the staged list. Staging itself happens from the context menus of the other
    // views (EntryActionHub), which pings this panel through the controller.
    void refresh();

private:
    void unstageSelected();
    void clearStaged();
    void moveStagedToRecycleBin();

    VirtualTrashService *m_trashService;
    QLabel *m_summaryLabel;
    QLabel *m_statusLabel;
    QPushButton *m_unstageButton;
    QPushButton *m_clearButton;
    QPushButton *m_recycleButton;
    QTableWidget *m_table;
};

} // namespace opentree
