#pragma once

#include <QMenuBar>

namespace opentree {

// QMenuBar can end up stuck: when an interaction leaves its internal "mouse down" state set
// (a click that lands before the pop-up is laid out, a pop-up that never opened, focus
// changes while the pointer is over the bar), the drop-down stops appearing on every
// subsequent click until the app is restarted.
//
// This subclass watches its own presses and, when Qt does not show the menu, opens it
// itself; it also clears a stale highlight on release/leave so the bar always stays usable.
class RobustMenuBar : public QMenuBar {
    Q_OBJECT

public:
    explicit RobustMenuBar(QWidget *parent = nullptr);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    // Opens `action`'s menu when Qt did not, for whatever reason.
    void recoverPopupFor(QAction *action);
    void clearStaleHighlight();

    QAction *m_pendingAction = nullptr;
};

} // namespace opentree
