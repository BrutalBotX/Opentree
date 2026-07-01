#pragma once

#include <QDateTime>
#include <QString>

namespace opentree {

struct FileMetadataDetails {
    QString owner;
    QString attributes;
    QString permissions;
    qint64 allocatedBytes = 0;
    bool allocatedBytesKnown = false;
};

namespace FileMetadataUtils {

FileMetadataDetails readMetadata(const QString &path);
QString formatTimestamp(const QDateTime &timestamp);
QString formatCompressionRate(qint64 logicalSize, qint64 allocatedSize, bool knownAllocatedSize);
QString formatPercentOfParent(qint64 size, qint64 parentSize);

}

}
