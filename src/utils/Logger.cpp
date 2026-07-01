#include "utils/Logger.h"

#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>

namespace {

QMutex &logMutex()
{
    static QMutex mutex;
    return mutex;
}

QString &logPathStorage()
{
    static QString path;
    return path;
}

void writeLine(const QString &line)
{
    QMutexLocker locker(&logMutex());
    QFile file(logPathStorage());
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << line << '\n';
    }
}

void log(const QString &level, const QString &message)
{
    const QString line = QStringLiteral("[%1] [%2] %3")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate), level, message);
    writeLine(line);
}

}

namespace opentree::Logger {

void initialize()
{
    if (logPathStorage().isEmpty()) {
        logPathStorage() = QStringLiteral("opentree.log");
        QFile file(logPathStorage());
        file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        file.close();
        info(QStringLiteral("Logger initialized at %1").arg(logPathStorage()));
    }
}

QString logFilePath()
{
    if (logPathStorage().isEmpty()) {
        initialize();
    }
    return logPathStorage();
}

void info(const QString &message)
{
    log("INFO", message);
}

void warning(const QString &message)
{
    log("WARN", message);
}

void error(const QString &message)
{
    log("ERROR", message);
}

}
