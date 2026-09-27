#include "services/UpdateChecker.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <algorithm>

#include "utils/Logger.h"

namespace opentree {

namespace {

constexpr char kRepository[] = "BrutalBotX/Opentree";

QNetworkRequest gitHubRequest(const QString &url)
{
    QNetworkRequest request{QUrl(url)};
    // GitHub rejects requests without a User-Agent.
    request.setRawHeader("User-Agent", "OpenTree-update-check");
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    return request;
}

QString stripVersionPrefix(QString tag)
{
    tag = tag.trimmed();
    while (tag.startsWith(QLatin1Char('v')) || tag.startsWith(QLatin1Char('V'))) {
        tag.remove(0, 1);
    }
    return tag;
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
}

QString UpdateChecker::repositoryUrl()
{
    return QStringLiteral("https://github.com/%1").arg(QString::fromLatin1(kRepository));
}

QString UpdateChecker::releasesPageUrl()
{
    return QStringLiteral("https://github.com/%1/releases").arg(QString::fromLatin1(kRepository));
}

bool UpdateChecker::isNewerVersion(const QString &candidate, const QString &current)
{
    const QStringList candidateParts = stripVersionPrefix(candidate).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    const QStringList currentParts = stripVersionPrefix(current).split(QLatin1Char('.'), Qt::SkipEmptyParts);
    // Leading digits of each part, so "1.2.3-beta" behaves like "1.2.3".
    const auto leadingNumber = [](const QString &part) {
        int value = 0;
        for (const QChar character : part) {
            if (!character.isDigit()) {
                break;
            }
            value = value * 10 + character.digitValue();
        }
        return value;
    };

    const int count = std::max(candidateParts.size(), currentParts.size());
    for (int index = 0; index < count; ++index) {
        const int candidateNumber = index < candidateParts.size() ? leadingNumber(candidateParts.at(index)) : 0;
        const int currentNumber = index < currentParts.size() ? leadingNumber(currentParts.at(index)) : 0;
        if (candidateNumber != currentNumber) {
            return candidateNumber > currentNumber;
        }
    }
    return false;
}

void UpdateChecker::check()
{
    requestLatestRelease();
}

void UpdateChecker::requestLatestRelease()
{
    QNetworkReply *reply = m_network->get(gitHubRequest(QStringLiteral("https://api.github.com/repos/%1/releases/latest")
                                                            .arg(QString::fromLatin1(kRepository))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // No releases published (404) is normal for this project: try tags instead.
            Logger::info(QStringLiteral("update-check: no release available (%1), checking tags").arg(reply->errorString()));
            requestTags();
            return;
        }

        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        const QString tag = object.value(QStringLiteral("tag_name")).toString();
        const QString releaseUrl = object.value(QStringLiteral("html_url")).toString();
        const QString current = QCoreApplication::applicationVersion();
        if (tag.isEmpty()) {
            requestTags();
            return;
        }

        UpdateCheckResult result;
        result.latestVersion = stripVersionPrefix(tag);
        result.releasesUrl = releasesPageUrl();
        if (isNewerVersion(result.latestVersion, current)) {
            result.status = UpdateCheckResult::Status::UpdateAvailable;
            result.message = QStringLiteral("Version %1 is available (you have %2).").arg(result.latestVersion, current);
            if (!releaseUrl.isEmpty()) {
                result.releasesUrl = releaseUrl;
            }
        } else {
            result.status = UpdateCheckResult::Status::UpToDate;
            result.message = QStringLiteral("You are on the latest version (%1).").arg(current);
        }
        Logger::info(QStringLiteral("update-check: latest=%1 current=%2 status=%3")
                         .arg(result.latestVersion, current)
                         .arg(result.status == UpdateCheckResult::Status::UpdateAvailable ? QStringLiteral("update") : QStringLiteral("current")));
        emit finished(result);
    });
}

void UpdateChecker::requestTags()
{
    QNetworkReply *reply = m_network->get(gitHubRequest(QStringLiteral("https://api.github.com/repos/%1/tags")
                                                            .arg(QString::fromLatin1(kRepository))));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        UpdateCheckResult result;
        result.releasesUrl = releasesPageUrl();

        if (reply->error() != QNetworkReply::NoError) {
            result.status = UpdateCheckResult::Status::Failed;
            result.message = QStringLiteral("Could not reach GitHub (%1).").arg(reply->errorString());
            Logger::warning(QStringLiteral("update-check failed: %1").arg(reply->errorString()));
            emit finished(result);
            return;
        }

        const QJsonArray tags = QJsonDocument::fromJson(reply->readAll()).array();
        const QString current = QCoreApplication::applicationVersion();
        QString newest;
        for (const QJsonValue &value : tags) {
            const QString name = stripVersionPrefix(value.toObject().value(QStringLiteral("name")).toString());
            if (name.isEmpty()) {
                continue;
            }
            if (newest.isEmpty() || isNewerVersion(name, newest)) {
                newest = name;
            }
        }

        if (newest.isEmpty()) {
            result.status = UpdateCheckResult::Status::Failed;
            result.message = QStringLiteral("No releases or tags were found on GitHub.");
        } else if (isNewerVersion(newest, current)) {
            result.status = UpdateCheckResult::Status::UpdateAvailable;
            result.latestVersion = newest;
            result.message = QStringLiteral("Version %1 is available (you have %2).").arg(newest, current);
        } else {
            result.status = UpdateCheckResult::Status::UpToDate;
            result.latestVersion = newest;
            result.message = QStringLiteral("You are on the latest version (%1).").arg(current);
        }
        emit finished(result);
    });
}

} // namespace opentree
