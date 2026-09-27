#pragma once

#include <QDialog>
#include <QString>

QT_FORWARD_DECLARE_CLASS(QCheckBox)

namespace opentree {

// First-run offer to use the Everything index. Nothing is downloaded or installed silently:
// the dialog only opens the voidtools download page or starts an already-installed
// Everything, both on an explicit click, and the built-in filesystem scan stays available.
class EverythingPromptDialog : public QDialog {
    Q_OBJECT

public:
    EverythingPromptDialog(const QString &installedExecutable, const QString &availabilityError,
                           QWidget *parent = nullptr);

    bool downloadRequested() const { return m_downloadRequested; }
    bool startRequested() const { return m_startRequested; }
    bool dontShowAgain() const;

private:
    bool m_downloadRequested = false;
    bool m_startRequested = false;
    QCheckBox *m_dontShowAgain;
};

} // namespace opentree
