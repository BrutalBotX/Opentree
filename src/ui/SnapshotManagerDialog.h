#pragma once

#include <QDialog>

#include "services/SnapshotService.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

class SnapshotManagerDialog : public QDialog {
    Q_OBJECT

public:
    explicit SnapshotManagerDialog(SnapshotService *snapshotService, QWidget *parent = nullptr);

private slots:
    void accept() override;

private:
    void refreshList();
    int selectedSnapshotId() const;

    SnapshotService *m_snapshotService;
    QTableWidget *m_snapshotTable;
    QLabel *m_statusLabel;
    QPushButton *m_deleteButton;
};

}
