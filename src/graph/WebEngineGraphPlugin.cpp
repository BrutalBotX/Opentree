// Graph tab renderer plugin: the only piece of OpenTree that links Qt WebEngine.
//
// Keeping WebEngine out of the main executable matters: just loading its DLLs commits a few
// hundred megabytes (Chromium's allocator and static initialisers), which users who never open
// the Graph tab should not pay for. The panel loads this plugin the first time the tab is shown.

#include "graph/IGraphView.h"

#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineView>
#include <QtPlugin>

#include <utility>

using opentree::IGraphView;
using opentree::IGraphViewFactory;

namespace {

class WebEngineGraphView : public QObject, public IGraphView {
    Q_OBJECT

public:
    WebEngineGraphView(QObject *parent, QObject *bridge)
        : QObject(parent)
        , m_view(new QWebEngineView)
    {
        m_channel = new QWebChannel(m_view);
        m_channel->registerObject(QStringLiteral("graphBridge"), bridge);
        m_view->page()->setWebChannel(m_channel);
        connect(m_view, &QWebEngineView::loadFinished, this, &WebEngineGraphView::loadFinished);
    }

    QWidget *widget() override { return m_view; }

    void setHtml(const QString &html, const QUrl &url) override { m_view->setHtml(html, url); }

    void runJavaScript(const QString &script) override
    {
        if (m_view && m_view->page()) {
            m_view->page()->runJavaScript(script);
        }
    }

    void refreshAfterShow() override
    {
        if (m_view && m_view->page()
            && m_view->page()->lifecycleState() == QWebEnginePage::LifecycleState::Discarded) {
            m_view->page()->setLifecycleState(QWebEnginePage::LifecycleState::Active);
        }
    }

    void release() override
    {
        if (!m_view) {
            return;
        }
        // The renderer process goes away with the page; Chromium's in-process state stays until
        // the application exits, which is why starting the graph lazily matters most.
        m_view->deleteLater();
        m_view = nullptr;
        m_channel = nullptr;
    }

    QObject *notifier() override { return this; }

signals:
    void loadFinished(bool ok);

private:
    QWebEngineView *m_view = nullptr;
    QWebChannel *m_channel = nullptr;
};

class GraphViewFactory : public QObject, public IGraphViewFactory {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OpentreeGraphViewFactory_iid)
    Q_INTERFACES(opentree::IGraphViewFactory)

public:
    IGraphView *create(QObject *parent, QObject *bridge) override
    {
        return new WebEngineGraphView(parent, bridge);
    }

    QString backendName() const override { return QStringLiteral("WebEngine"); }
};

}

#include "WebEngineGraphPlugin.moc"
