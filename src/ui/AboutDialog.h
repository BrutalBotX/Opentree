#pragma once

#include <QDialog>

#include "services/UpdateChecker.h"

QT_FORWARD_DECLARE_CLASS(QLabel)
QT_FORWARD_DECLARE_CLASS(QPushButton)

namespace opentree {

// Casual About box: what OpenTree is, who made it, the exact build, and a button that checks
// GitHub for a newer version.
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

    // Starts the GitHub check (also reachable from the Help menu).
    void checkForUpdates();

private:
    void showResult(const UpdateCheckResult &result);
    void openReleasesPage();

    QLabel *m_updateLabel;
    QPushButton *m_checkButton;
    QPushButton *m_downloadButton;
    QString m_releasesUrl;
};

} // namespace opentree
