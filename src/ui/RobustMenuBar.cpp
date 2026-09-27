#include "ui/RobustMenuBar.h"

#include <QApplication>
#include <QMouseEvent>
#include <QTimer>

#include "utils/Logger.h"

namespace opentree {

RobustMenuBar::RobustMenuBar(QWidget *parent)
    : QMenuBar(parent)
{
}

void RobustMenuBar::mousePressEvent(QMouseEvent *event)
{
    // Qt remembers the last action as "current" even when its menu never appeared. A further
    // click then toggles that state into "closed" instead of opening the drop-down, which is
    // why one aborted open used to leave the item dead. Drop the stale state first.
    if (!QApplication::activePopupWidget() && activeAction()) {
        setActiveAction(nullptr);
    }

    QAction *action = actionAt(event->pos());
    QMenuBar::mousePressEvent(event);

    if (action && action->menu()) {
        // Give Qt the rest of this event-loop turn to show the pop-up, then step in if the
        // drop-down never appeared. Deferring keeps ordinary clicks untouched.
        m_pendingAction = action;
        QTimer::singleShot(0, this, [this]() {
            QAction *pending = m_pendingAction;
            m_pendingAction = nullptr;
            recoverPopupFor(pending);
        });
    }
}

void RobustMenuBar::recoverPopupFor(QAction *action)
{
    if (!action || !action->menu() || action->menu()->isVisible()) {
        return; // Qt handled it
    }
    if (QApplication::activePopupWidget()) {
        return; // another pop-up owns the interaction right now
    }

    Logger::warning(QStringLiteral("menu-bar: Qt left the drop-down closed, recovering for '%1'").arg(action->text()));

    const QPoint position = mapToGlobal(QPoint(actionGeometry(action).left(), height()));
    setActiveAction(action);
    action->menu()->popup(position);
}

void RobustMenuBar::clearStaleHighlight()
{
    m_pendingAction = nullptr;
    if (QApplication::activePopupWidget()) {
        return; // a menu is open: the highlight is meaningful
    }
    if (activeAction()) {
        // Without this the bar can stay "active" and ignore further clicks.
        setActiveAction(nullptr);
    }
}

void RobustMenuBar::mouseReleaseEvent(QMouseEvent *event)
{
    QMenuBar::mouseReleaseEvent(event);
    // Note: m_pendingAction is deliberately kept so the deferred recovery still runs when Qt
    // swallowed the press; it is dropped when the pointer leaves the bar.
    if (!QApplication::activePopupWidget()) {
        clearStaleHighlight();
    }
}

void RobustMenuBar::leaveEvent(QEvent *event)
{
    QMenuBar::leaveEvent(event);
    clearStaleHighlight();
}

} // namespace opentree
