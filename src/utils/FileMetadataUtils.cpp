#include "utils/FileMetadataUtils.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <AclAPI.h>

namespace {

QString formatAttributes(DWORD attributes)
{
    QStringList parts;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
        parts << "Directory";
    }
    if (attributes & FILE_ATTRIBUTE_ARCHIVE) {
        parts << "Archive";
    }
    if (attributes & FILE_ATTRIBUTE_COMPRESSED) {
        parts << "Compressed";
    }
    if (attributes & FILE_ATTRIBUTE_ENCRYPTED) {
        parts << "Encrypted";
    }
    if (attributes & FILE_ATTRIBUTE_HIDDEN) {
        parts << "Hidden";
    }
    if (attributes & FILE_ATTRIBUTE_READONLY) {
        parts << "ReadOnly";
    }
    if (attributes & FILE_ATTRIBUTE_SYSTEM) {
        parts << "System";
    }
    if (attributes & FILE_ATTRIBUTE_TEMPORARY) {
        parts << "Temporary";
    }
    return parts.isEmpty() ? QStringLiteral("Normal") : parts.join(", ");
}

QString rightsToText(DWORD accessMask)
{
    QStringList parts;
    if (accessMask & FILE_GENERIC_READ) {
        parts << "Read";
    }
    if (accessMask & FILE_GENERIC_WRITE) {
        parts << "Write";
    }
    if (accessMask & FILE_GENERIC_EXECUTE) {
        parts << "Execute";
    }
    if (accessMask & DELETE) {
        parts << "Delete";
    }
    return parts.isEmpty() ? QStringLiteral("Special") : parts.join("/");
}

}

namespace opentree::FileMetadataUtils {

FileMetadataDetails readMetadata(const QString &path)
{
    FileMetadataDetails details;

    const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
    const DWORD attributes = GetFileAttributesW(nativePath.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) {
        details.attributes = formatAttributes(attributes);
    } else {
        details.attributes = QStringLiteral("Unknown");
    }

    HANDLE handle = CreateFileW(
        nativePath.c_str(),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (handle != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER allocationSize {};
        if (GetFileInformationByHandleEx(handle, FileStandardInfo, &allocationSize, sizeof(FILE_STANDARD_INFO))) {
            FILE_STANDARD_INFO info {};
            if (GetFileInformationByHandleEx(handle, FileStandardInfo, &info, sizeof(info))) {
                details.allocatedBytes = info.AllocationSize.QuadPart;
                details.allocatedBytesKnown = true;
            }
        }
        CloseHandle(handle);
    }

    PSID ownerSid = nullptr;
    PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
    if (GetNamedSecurityInfoW(
            const_cast<wchar_t *>(nativePath.c_str()),
            SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION,
            &ownerSid,
            nullptr,
            nullptr,
            nullptr,
            &securityDescriptor)
        == ERROR_SUCCESS) {
        wchar_t ownerName[256] = {};
        wchar_t domainName[256] = {};
        DWORD ownerNameSize = 256;
        DWORD domainNameSize = 256;
        SID_NAME_USE sidType {};
        if (LookupAccountSidW(nullptr, ownerSid, ownerName, &ownerNameSize, domainName, &domainNameSize, &sidType)) {
            details.owner = QString::fromWCharArray(domainName);
            if (!details.owner.isEmpty()) {
                details.owner += '\\';
            }
            details.owner += QString::fromWCharArray(ownerName);
        }
        if (securityDescriptor) {
            LocalFree(securityDescriptor);
        }
    }

    QFileInfo info(path);
    details.permissions = QStringLiteral("%1%2%3")
                              .arg(info.isReadable() ? "Read " : "")
                              .arg(info.isWritable() ? "Write " : "")
                              .arg(info.isExecutable() ? "Execute" : "")
                              .trimmed();
    if (details.permissions.isEmpty()) {
        details.permissions = QStringLiteral("Unknown");
    }

    if (details.owner.isEmpty()) {
        details.owner = QStringLiteral("Unknown");
    }

    return details;
}

QString formatTimestamp(const QDateTime &timestamp)
{
    return timestamp.isValid() ? timestamp.toString("yyyy-MM-dd HH:mm:ss") : QStringLiteral("-");
}

QString formatCompressionRate(qint64 logicalSize, qint64 allocatedSize, bool knownAllocatedSize)
{
    if (!knownAllocatedSize || logicalSize <= 0) {
        return QStringLiteral("0%");
    }

    const double ratio = std::max(0.0, 100.0 * (1.0 - (static_cast<double>(allocatedSize) / static_cast<double>(logicalSize))));
    return QString::number(ratio, 'f', 1) + "%";
}

QString formatPercentOfParent(qint64 size, qint64 parentSize)
{
    if (parentSize <= 0) {
        return QStringLiteral("-");
    }

    const double percent = 100.0 * static_cast<double>(size) / static_cast<double>(parentSize);
    return QString::number(percent, 'f', 1) + "%";
}

}
