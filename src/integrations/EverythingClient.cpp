#include "integrations/EverythingClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QThread>

#include "Everything.h"
#include "utils/PathUtils.h"

namespace opentree {

EverythingClient::EverythingClient()
    : m_library(QCoreApplication::applicationDirPath() + "/Everything64.dll")
{
    load();
}

bool EverythingClient::isAvailable() const
{
    return m_loaded;
}

QString EverythingClient::availabilityError() const
{
    return m_availabilityError;
}

bool EverythingClient::ensureEverythingRunning(const QString &everythingExecutablePath, QString *errorMessage)
{
    if (isAvailable()) {
        return true;
    }

    const QFileInfo executableInfo(everythingExecutablePath);
    if (!executableInfo.exists() || !executableInfo.isFile()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Configured Everything executable does not exist: %1").arg(everythingExecutablePath);
        }
        return false;
    }

    if (!QProcess::startDetached(everythingExecutablePath, {})) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to launch Everything executable: %1").arg(everythingExecutablePath);
        }
        return false;
    }

    for (int attempt = 0; attempt < 10; ++attempt) {
        QThread::msleep(300);
        m_library.unload();
        m_loaded = false;
        if (load()) {
            return true;
        }
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Everything started, but the SDK did not become available in time.");
    }
    return false;
}

QVector<FileEntry> EverythingClient::queryRecursive(const QString &rootPath, QString *errorMessage)
{
    QVector<FileEntry> files;
    runQuery(rootPath, &files, nullptr, errorMessage);
    return files;
}

QVector<FolderEntry> EverythingClient::queryFoldersRecursive(const QString &rootPath, QString *errorMessage)
{
    QVector<FolderEntry> folders;
    runQuery(rootPath, nullptr, &folders, errorMessage);
    return folders;
}

bool EverythingClient::runQuery(const QString &rootPath, QVector<FileEntry> *files, QVector<FolderEntry> *folders, QString *errorMessage)
{
    if (!m_loaded) {
        if (errorMessage) {
            *errorMessage = m_availabilityError.isEmpty() ? QStringLiteral("Everything64.dll is not available") : m_availabilityError;
        }
        return false;
    }

    const QString normalizedRoot = PathUtils::normalizePath(rootPath);
    const QString nativeRoot = QDir::toNativeSeparators(normalizedRoot);
    QString escapedRoot = nativeRoot;
    escapedRoot.replace('\'', "''");
    const QString search = QStringLiteral("parent:^") + escapedRoot + QStringLiteral("\\");

    m_setSearchW(reinterpret_cast<LPCWSTR>(search.utf16()));
    m_setMatchPath(TRUE);
    m_setRegex(TRUE);
    m_setRequestFlags(EVERYTHING_REQUEST_FULL_PATH_AND_FILE_NAME | EVERYTHING_REQUEST_SIZE);
    m_setSort(EVERYTHING_SORT_PATH_ASCENDING);
    m_setOffset(0);
    m_setMax(1000000);

    if (!m_queryW(TRUE)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Everything query failed with error %1").arg(m_getLastError ? m_getLastError() : -1);
        }
        return false;
    }

    const DWORD resultCount = m_getNumResults();
    if (files) {
        files->reserve(static_cast<int>(resultCount));
    }
    if (folders) {
        folders->reserve(static_cast<int>(resultCount));
    }

    for (DWORD index = 0; index < resultCount; ++index) {
        wchar_t buffer[32768] = {};
        m_getResultFullPathNameW(index, buffer, 32768);
        const QString fullPath = PathUtils::normalizePath(QString::fromWCharArray(buffer));
        if (!fullPath.startsWith(normalizedRoot, Qt::CaseInsensitive)) {
            continue;
        }

        if (m_isFolderResult(index)) {
            if (folders) {
                FolderEntry folder;
                folder.path = fullPath;
                folder.parentPath = fullPath.compare(normalizedRoot, Qt::CaseInsensitive) == 0
                    ? QString()
                    : PathUtils::parentPath(fullPath);
                folder.name = PathUtils::fileName(fullPath);
                if (folder.name.isEmpty()) {
                    folder.name = fullPath;
                }
                folders->push_back(folder);
            }
            continue;
        }

        if (files) {
            LARGE_INTEGER sizeValue {};
            m_getResultSize(index, &sizeValue);

            FileEntry file;
            file.path = fullPath;
            file.parentPath = PathUtils::parentPath(fullPath);
            file.name = PathUtils::fileName(fullPath);
            file.size = sizeValue.QuadPart;
            files->push_back(file);
        }
    }

    return true;
}

bool EverythingClient::load()
{
    if (m_loaded) {
        return true;
    }

    if (!m_library.load()) {
        m_availabilityError = m_library.errorString();
        return false;
    }

    m_setSearchW = reinterpret_cast<SetSearchWFn>(m_library.resolve("Everything_SetSearchW"));
    m_setMatchPath = reinterpret_cast<SetMatchPathFn>(m_library.resolve("Everything_SetMatchPath"));
    m_setRegex = reinterpret_cast<SetRegexFn>(m_library.resolve("Everything_SetRegex"));
    m_setRequestFlags = reinterpret_cast<SetRequestFlagsFn>(m_library.resolve("Everything_SetRequestFlags"));
    m_setSort = reinterpret_cast<SetSortFn>(m_library.resolve("Everything_SetSort"));
    m_setMax = reinterpret_cast<SetMaxFn>(m_library.resolve("Everything_SetMax"));
    m_setOffset = reinterpret_cast<SetOffsetFn>(m_library.resolve("Everything_SetOffset"));
    m_queryW = reinterpret_cast<QueryWFn>(m_library.resolve("Everything_QueryW"));
    m_getNumResults = reinterpret_cast<GetNumResultsFn>(m_library.resolve("Everything_GetNumResults"));
    m_isFolderResult = reinterpret_cast<IsFolderResultFn>(m_library.resolve("Everything_IsFolderResult"));
    m_getResultFullPathNameW = reinterpret_cast<GetResultFullPathNameWFn>(m_library.resolve("Everything_GetResultFullPathNameW"));
    m_getResultSize = reinterpret_cast<GetResultSizeFn>(m_library.resolve("Everything_GetResultSize"));
    m_getLastError = reinterpret_cast<GetLastErrorFn>(m_library.resolve("Everything_GetLastError"));

    m_loaded = m_setSearchW && m_setMatchPath && m_setRegex && m_setRequestFlags && m_setSort && m_setMax && m_setOffset && m_queryW
        && m_getNumResults && m_isFolderResult && m_getResultFullPathNameW && m_getResultSize;
    if (!m_loaded) {
        m_availabilityError = QStringLiteral("Everything SDK entry points could not be resolved");
    }
    return m_loaded;
}

}
