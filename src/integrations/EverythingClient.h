#pragma once

#include <QLibrary>
#include <QMutex>
#include <QString>
#include <QVector>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "domain/ScanTypes.h"

namespace opentree {

class EverythingClient {
public:
    EverythingClient();
    ~EverythingClient();

    bool isLibraryLoaded() const;
    bool isServiceAvailable() const;
    bool isAvailable() const;
    QString availabilityError() const;
    QString libraryPath() const;

    // The voidtools download page shown to users who do not have Everything yet.
    static QString downloadUrl();

    // Looks for an installed Everything.exe: the configured path first, then the uninstall
    // registry entries and the usual install locations. Empty when nothing is installed.
    static QString detectInstalledExecutable(const QString &configuredPath = QString());

    // Starts an installed Everything so the SDK becomes reachable. Only used from an
    // explicit user action; the scan path never launches anything on its own.
    static bool startEverything(const QString &executablePath, QString *errorMessage = nullptr);

    // Runs a trivial query to confirm the Everything IPC/service is actually reachable.
    // DLL load alone is not enough: the DLL loads even when Everything is not running.
    bool testConnection(QString *errorMessage = nullptr);

    // Single combined query returning files and folders under rootPath.
    bool queryRoot(const QString &rootPath, QVector<FileEntry> *files, QVector<FolderEntry> *folders, QString *errorMessage = nullptr);

    QVector<FileEntry> queryRecursive(const QString &rootPath, QString *errorMessage = nullptr);
    QVector<FolderEntry> queryFoldersRecursive(const QString &rootPath, QString *errorMessage = nullptr);

private:
    using SetSearchWFn = void (WINAPI *)(LPCWSTR);
    using SetMatchPathFn = void (WINAPI *)(BOOL);
    using SetRegexFn = void (WINAPI *)(BOOL);
    using SetRequestFlagsFn = void (WINAPI *)(DWORD);
    using SetSortFn = void (WINAPI *)(DWORD);
    using SetMaxFn = void (WINAPI *)(DWORD);
    using SetOffsetFn = void (WINAPI *)(DWORD);
    using QueryWFn = BOOL (WINAPI *)(BOOL);
    using GetNumResultsFn = DWORD (WINAPI *)(void);
    using GetTotResultsFn = DWORD (WINAPI *)(void);
    using IsFolderResultFn = BOOL (WINAPI *)(DWORD);
    using GetResultFullPathNameWFn = DWORD (WINAPI *)(DWORD, LPWSTR, DWORD);
    using GetResultSizeFn = BOOL (WINAPI *)(DWORD, LARGE_INTEGER *);
    using GetLastErrorFn = DWORD (WINAPI *)(void);
    using ResetFn = void (WINAPI *)(void);
    using CleanUpFn = void (WINAPI *)(void);

    static QString defaultLibraryName();

    // All *_Locked helpers assume m_mutex is already held.
    bool loadLocked();
    bool testConnectionLocked(QString *errorMessage);
    bool queryRootLocked(const QString &rootPath, QVector<FileEntry> *files, QVector<FolderEntry> *folders, QString *errorMessage);

    mutable QMutex m_mutex;
    QLibrary m_library;
    QString m_libraryPath;
    bool m_libraryLoaded = false;
    bool m_serviceAvailable = false;
    QString m_availabilityError;

    SetSearchWFn m_setSearchW = nullptr;
    SetMatchPathFn m_setMatchPath = nullptr;
    SetRegexFn m_setRegex = nullptr;
    SetRequestFlagsFn m_setRequestFlags = nullptr;
    SetSortFn m_setSort = nullptr;
    SetMaxFn m_setMax = nullptr;
    SetOffsetFn m_setOffset = nullptr;
    QueryWFn m_queryW = nullptr;
    GetNumResultsFn m_getNumResults = nullptr;
    GetTotResultsFn m_getTotResults = nullptr;
    IsFolderResultFn m_isFolderResult = nullptr;
    GetResultFullPathNameWFn m_getResultFullPathNameW = nullptr;
    GetResultSizeFn m_getResultSize = nullptr;
    GetLastErrorFn m_getLastError = nullptr;
    ResetFn m_reset = nullptr;
    CleanUpFn m_cleanUp = nullptr;
};

}
