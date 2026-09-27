#pragma once

#include <QObject>
#include <QString>

QT_FORWARD_DECLARE_CLASS(QNetworkAccessManager)

namespace opentree {

struct UpdateCheckResult {
    enum class Status {
        UpToDate,
        UpdateAvailable,
        Failed,
    };

    Status status = Status::Failed;
    QString latestVersion;   // without a leading "v"
    QString releasesUrl;     // where to send the user (GitHub releases page)
    QString message;         // friendly text for the About dialog / message box
};

// Asks GitHub for the newest release or tag and compares it with the running version.
// Nothing is downloaded or installed: a newer version just opens the releases page.
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    void check();

    static QString repositoryUrl();
    static QString releasesPageUrl();

    // True when `candidate` is a higher version than `current` ("0.10.0" > "0.9.9").
    static bool isNewerVersion(const QString &candidate, const QString &current);

signals:
    void finished(const UpdateCheckResult &result);

private:
    void requestLatestRelease();
    void requestTags();
    void finishFromJson(const QByteArray &payload, const QString &latestReleaseName);

    QNetworkAccessManager *m_network;
};

} // namespace opentree
