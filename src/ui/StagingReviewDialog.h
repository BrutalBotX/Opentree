#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

#include "ui/EntryActions.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QTableWidget)

namespace opentree {

// Single review step for staging items in the virtual trash or sending them to the Windows
// Recycle Bin. It lists exactly what will be affected and asks for one confirmation instead
// of a chain of dialogs.
class StagingReviewDialog : public QDialog {
    Q_OBJECT

public:
    enum class Action {
        Stage,
        MoveToRecycleBin,
    };

    StagingReviewDialog(Action action, const QVector<StageRequest> &items,
                        const QStringList &skipped, QWidget *parent = nullptr);

    int itemCount() const { return m_itemCount; }
    qint64 totalBytes() const { return m_totalBytes; }

private:
    int m_itemCount = 0;
    qint64 m_totalBytes = 0;
};

} // namespace opentree
