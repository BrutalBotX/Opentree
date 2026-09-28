#pragma once

#include <QString>
#include <QUrl>
#include <QtPlugin>

class QObject;
class QWidget;

namespace opentree {

// A renderer for the Graph tab's HTML/JS page. It lives in a plugin so the application does
// not link Qt WebEngine: loading Chromium commits a few hundred megabytes and most sessions
// never open the Graph tab. The plugin is loaded the first time the tab is shown.
class IGraphView {
public:
    virtual ~IGraphView() = default;

    // The widget to insert into the panel. It is owned by the panel afterwards.
    virtual QWidget *widget() = 0;
    virtual void setHtml(const QString &html, const QUrl &url) = 0;
    virtual void runJavaScript(const QString &script) = 0;
    // Called when the tab becomes visible again, to revive a discarded renderer.
    virtual void refreshAfterShow() = 0;
    // Lets the renderer go (the view is recreated on the next visit).
    virtual void release() = 0;
    // The object that emits loadFinished(bool). Connect with the string-based syntax, since the
    // concrete type is not known here.
    virtual QObject *notifier() = 0;
};

class IGraphViewFactory {
public:
    virtual ~IGraphViewFactory() = default;

    // Creates a renderer; bridge is exposed to the page as "graphBridge" and stays owned by
    // the caller.
    virtual IGraphView *create(QObject *parent, QObject *bridge) = 0;
    virtual QString backendName() const = 0;
};

}

#define OpentreeGraphViewFactory_iid "com.brutalbot.opentree.GraphViewFactory/1.0"
Q_DECLARE_INTERFACE(opentree::IGraphViewFactory, OpentreeGraphViewFactory_iid)
