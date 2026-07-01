#pragma once

#include <QLibrary>
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

    bool isAvailable() const;
    QString availabilityError() const;
    bool ensureEverythingRunning(const QString &everythingExecutablePath, QString *errorMessage = nullptr);
    QVector<FileEntry> queryRecursive(const QString &rootPath, QString *errorMessage = nullptr);
    QVector<FolderEntry> queryFoldersRecursive(const QString &rootPath, QString *errorMessage = nullptr);

private:
    using SetSearchWFn = void (*)(LPCWSTR);
    using SetMatchPathFn = void (*)(BOOL);
    using SetRegexFn = void (*)(BOOL);
    using SetRequestFlagsFn = void (*)(DWORD);
    using SetSortFn = void (*)(DWORD);
    using SetMaxFn = void (*)(DWORD);
    using SetOffsetFn = void (*)(DWORD);
    using QueryWFn = BOOL (*)(BOOL);
    using GetNumResultsFn = DWORD (*)();
    using IsFolderResultFn = BOOL (*)(DWORD);
    using GetResultFullPathNameWFn = DWORD (*)(DWORD, LPWSTR, DWORD);
    using GetResultSizeFn = BOOL (*)(DWORD, LARGE_INTEGER *);
    using GetLastErrorFn = DWORD (*)();

    bool load();
    bool runQuery(const QString &rootPath, QVector<FileEntry> *files, QVector<FolderEntry> *folders, QString *errorMessage);

    QLibrary m_library;
    bool m_loaded = false;
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
    IsFolderResultFn m_isFolderResult = nullptr;
    GetResultFullPathNameWFn m_getResultFullPathNameW = nullptr;
    GetResultSizeFn m_getResultSize = nullptr;
    GetLastErrorFn m_getLastError = nullptr;
};

}
