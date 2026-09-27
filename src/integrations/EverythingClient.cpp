#include "integrations/EverythingClient.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QProcess>
#include <QSettings>
#include <QThread>

#include "Everything.h"
#include "utils/Logger.h"
#include "utils/PathUtils.h"

namespace {

constexpr DWORD kQueryBatchSize = 65536;

bool isWithinRoot(const QString &fullPath, const QString &normalizedRoot)
{
    if (fullPath.compare(normalizedRoot, Qt::CaseInsensitive) == 0) {
        return true;
    }

    QString prefix = normalizedRoot;
    if (!prefix.endsWith('/')) {
        prefix += '/';
    }
    return fullPath.startsWith(prefix, Qt::CaseInsensitive);
}

} // namespace

namespace opentree {

EverythingClient::EverythingClient()
    : m_libraryPath(QCoreApplication::applicationDirPath() + "/" + defaultLibraryName())
{
    QMutexLocker locker(&m_mutex);
    loadLocked();
}

EverythingClient::~EverythingClient()
{
    QMutexLocker locker(&m_mutex);
    if (m_libraryLoaded && m_cleanUp) {
        m_cleanUp();
    }
    if (m_library.isLoaded()) {
        m_library.unload();
    }
}

QString EverythingClient::defaultLibraryName()
{
#if defined(_M_ARM64) || defined(__aarch64__)
    return QStringLiteral("EverythingARM64.dll");
#elif defined(_M_ARM) || defined(__arm__)
    return QStringLiteral("EverythingARM.dll");
#elif defined(_WIN64) || defined(__x86_64__) || defined(__amd64__)
    return QStringLiteral("Everything64.dll");
#else
    return QStringLiteral("Everything32.dll");
#endif
}

bool EverythingClient::isLibraryLoaded() const
{
    QMutexLocker locker(&m_mutex);
    return m_libraryLoaded;
}

bool EverythingClient::isServiceAvailable() const
{
    QMutexLocker locker(&m_mutex);
    return m_serviceAvailable;
}

bool EverythingClient::isAvailable() const
{
    QMutexLocker locker(&m_mutex);
    return m_libraryLoaded && m_serviceAvailable;
}

QString EverythingClient::availabilityError() const
{
    QMutexLocker locker(&m_mutex);
    return m_availabilityError;
}

QString EverythingClient::libraryPath() const
{
    QMutexLocker locker(&m_mutex);
    return m_libraryPath;
}

QString EverythingClient::downloadUrl()
{
    return QStringLiteral("https://www.voidtools.com/downloads/");
}

QString EverythingClient::detectInstalledExecutable(const QString &configuredPath)
{
    const QString configured = QDir::fromNativeSeparators(configuredPath);
    if (!configured.isEmpty() && QFileInfo::exists(configured)) {
        return configured;
    }

    // Uninstall entries written by the Everything installer.
    const QStringList uninstallRoots = {
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
        QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall"),
    };

    for (const QString &root : uninstallRoots) {
        QSettings registry(root, QSettings::NativeFormat);
        const QStringList groups = registry.childGroups();
        for (const QString &group : groups) {
            if (!group.contains(QStringLiteral("Everything"), Qt::CaseInsensitive)) {
                continue;
            }
            registry.beginGroup(group);
            const QString displayName = registry.value(QStringLiteral("DisplayName")).toString();
            const QString installLocation = registry.value(QStringLiteral("InstallLocation")).toString();
            QString displayIcon = registry.value(QStringLiteral("DisplayIcon")).toString();
            registry.endGroup();

            if (!displayName.contains(QStringLiteral("Everything"), Qt::CaseInsensitive)) {
                continue;
            }

            QStringList candidates;
            if (!installLocation.isEmpty()) {
                candidates << QDir::fromNativeSeparators(installLocation) + QStringLiteral("/Everything.exe");
            }
            if (!displayIcon.isEmpty()) {
                // DisplayIcon is usually "C:\Path\Everything.exe,0" and sometimes quoted.
                displayIcon.remove(QLatin1Char('"'));
                const int comma = displayIcon.lastIndexOf(QLatin1Char(','));
                if (comma > 0) {
                    displayIcon.truncate(comma);
                }
                candidates << QDir::fromNativeSeparators(displayIcon);
            }

            for (const QString &candidate : candidates) {
                if (QFileInfo(candidate).isFile() && QFileInfo(candidate).fileName().compare(QStringLiteral("Everything.exe"), Qt::CaseInsensitive) == 0) {
                    return candidate;
                }
            }
        }
    }

    // Default install locations as a fallback.
    QStringList paths;
    paths << QStringLiteral("C:/Program Files/Everything/Everything.exe")
          << QStringLiteral("C:/Program Files (x86)/Everything/Everything.exe");
    const QString localAppData = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"));
    if (!localAppData.isEmpty()) {
        paths << localAppData + QStringLiteral("/Programs/Everything/Everything.exe");
    }
    for (const QString &candidate : paths) {
        if (QFileInfo::exists(candidate)) {
            return candidate;
        }
    }

    return {};
}

bool EverythingClient::startEverything(const QString &executablePath, QString *errorMessage)
{
    const QString path = detectInstalledExecutable(executablePath);
    if (path.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Everything is not installed. Download it from %1").arg(downloadUrl());
        }
        return false;
    }

    // Everything is a GUI application, so starting it detached cannot open a console window.
    if (!QProcess::startDetached(path, {})) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Failed to launch %1.").arg(path);
        }
        return false;
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool EverythingClient::testConnection(QString *errorMessage)
{
    QMutexLocker locker(&m_mutex);
    return testConnectionLocked(errorMessage);
}

bool EverythingClient::testConnectionLocked(QString *errorMessage)
{
    if (!m_libraryLoaded && !loadLocked()) {
        m_serviceAvailable = false;
        if (errorMessage) {
            *errorMessage = m_availabilityError;
        }
        return false;
    }

    // Query a token that will not match anything so the round-trip stays cheap.
    const QString probe = QStringLiteral("opentreeconnectionprobe6f4a1c");
    m_reset();
    m_setSearchW(reinterpret_cast<LPCWSTR>(probe.utf16()));
    m_setMatchPath(FALSE);
    m_setRegex(FALSE);
    m_setRequestFlags(EVERYTHING_REQUEST_FILE_NAME);
    m_setOffset(0);
    m_setMax(1);

    const BOOL queried = m_queryW(TRUE);
    const DWORD lastError = m_getLastError ? m_getLastError() : EVERYTHING_OK;

    if (!queried || lastError != EVERYTHING_OK) {
        m_serviceAvailable = false;
        if (errorMessage) {
            if (lastError == EVERYTHING_ERROR_IPC) {
                *errorMessage = QStringLiteral("Everything service is not running (IPC error).");
            } else {
                *errorMessage = QStringLiteral("Everything probe failed with error %1.").arg(lastError);
            }
        }
        return false;
    }

    m_serviceAvailable = true;
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool EverythingClient::queryRoot(const QString &rootPath, QVector<FileEntry> *files, QVector<FolderEntry> *folders, QString *errorMessage)
{
    QMutexLocker locker(&m_mutex);
    return queryRootLocked(rootPath, files, folders, errorMessage);
}

QVector<FileEntry> EverythingClient::queryRecursive(const QString &rootPath, QString *errorMessage)
{
    QVector<FileEntry> files;
    queryRoot(rootPath, &files, nullptr, errorMessage);
    return files;
}

QVector<FolderEntry> EverythingClient::queryFoldersRecursive(const QString &rootPath, QString *errorMessage)
{
    QVector<FolderEntry> folders;
    queryRoot(rootPath, nullptr, &folders, errorMessage);
    return folders;
}

bool EverythingClient::queryRootLocked(const QString &rootPath, QVector<FileEntry> *files, QVector<FolderEntry> *folders, QString *errorMessage)
{
    if (!m_libraryLoaded && !loadLocked()) {
        m_serviceAvailable = false;
        if (errorMessage) {
            *errorMessage = m_availabilityError;
        }
        return false;
    }

    const QString normalizedRoot = PathUtils::normalizePath(rootPath);
    if (normalizedRoot.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Cannot query Everything with an empty root path.");
        }
        return false;
    }

    const QString nativeRoot = QDir::toNativeSeparators(normalizedRoot);
    QString searchTerm = nativeRoot;
    while (searchTerm.endsWith('\\')) {
        searchTerm.chop(1);
    }
    // Quote the path so Everything treats spaces and other operators as literal text.
    searchTerm = QStringLiteral("\"") + searchTerm + QStringLiteral("\"");

    m_reset();
    m_setSearchW(reinterpret_cast<LPCWSTR>(searchTerm.utf16()));
    m_setMatchPath(TRUE);
    m_setRegex(FALSE);
    m_setRequestFlags(EVERYTHING_REQUEST_FULL_PATH_AND_FILE_NAME | EVERYTHING_REQUEST_SIZE);
    m_setSort(EVERYTHING_SORT_PATH_ASCENDING);

    if (files) {
        files->clear();
    }
    if (folders) {
        folders->clear();
    }

    DWORD offset = 0;
    DWORD total = 0;

    for (;;) {
        m_setOffset(offset);
        m_setMax(kQueryBatchSize);

        if (!m_queryW(TRUE)) {
            const DWORD lastError = m_getLastError ? m_getLastError() : EVERYTHING_ERROR_INVALIDCALL;
            m_serviceAvailable = false;
            m_availabilityError = QStringLiteral("Everything query failed with error %1").arg(lastError);
            if (errorMessage) {
                *errorMessage = m_availabilityError;
            }
            return false;
        }

        const DWORD lastError = m_getLastError ? m_getLastError() : EVERYTHING_OK;
        if (lastError != EVERYTHING_OK) {
            m_serviceAvailable = false;
            m_availabilityError = QStringLiteral("Everything query failed with error %1").arg(lastError);
            if (errorMessage) {
                *errorMessage = m_availabilityError;
            }
            return false;
        }

        m_serviceAvailable = true;

        const DWORD count = m_getNumResults();
        if (total == 0) {
            total = m_getTotResults ? m_getTotResults() : count;
        }

        for (DWORD index = 0; index < count; ++index) {
            wchar_t buffer[32768] = {};
            const DWORD written = m_getResultFullPathNameW(index, buffer, 32768);
            if (written == 0 || written >= 32768) {
                continue;
            }

            const QString fullPath = PathUtils::normalizePath(QString::fromWCharArray(buffer));
            if (!isWithinRoot(fullPath, normalizedRoot)) {
                continue;
            }

            if (m_isFolderResult(index)) {
                if (!folders) {
                    continue;
                }

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
            } else if (files) {
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

        offset += count;
        if (count == 0 || offset >= total) {
            break;
        }
    }

    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

bool EverythingClient::loadLocked()
{
    if (m_libraryLoaded) {
        return true;
    }

    if (!m_library.isLoaded()) {
        m_library.setFileName(m_libraryPath);
        if (!m_library.load()) {
            m_availabilityError = QStringLiteral("Failed to load %1: %2")
                                      .arg(m_libraryPath, m_library.errorString());
            return false;
        }
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
    m_getTotResults = reinterpret_cast<GetTotResultsFn>(m_library.resolve("Everything_GetTotResults"));
    m_isFolderResult = reinterpret_cast<IsFolderResultFn>(m_library.resolve("Everything_IsFolderResult"));
    m_getResultFullPathNameW = reinterpret_cast<GetResultFullPathNameWFn>(m_library.resolve("Everything_GetResultFullPathNameW"));
    m_getResultSize = reinterpret_cast<GetResultSizeFn>(m_library.resolve("Everything_GetResultSize"));
    m_getLastError = reinterpret_cast<GetLastErrorFn>(m_library.resolve("Everything_GetLastError"));
    m_reset = reinterpret_cast<ResetFn>(m_library.resolve("Everything_Reset"));
    m_cleanUp = reinterpret_cast<CleanUpFn>(m_library.resolve("Everything_CleanUp"));

    m_libraryLoaded = m_setSearchW && m_setMatchPath && m_setRegex && m_setRequestFlags && m_setSort
        && m_setMax && m_setOffset && m_queryW && m_getNumResults && m_getTotResults
        && m_isFolderResult && m_getResultFullPathNameW && m_getResultSize && m_getLastError
        && m_reset && m_cleanUp;

    if (!m_libraryLoaded) {
        m_availabilityError = QStringLiteral("Everything SDK entry points could not be resolved from %1").arg(m_libraryPath);
        Logger::warning(m_availabilityError);
    }

    return m_libraryLoaded;
}

}
