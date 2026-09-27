#include "utils/ProcessUtils.h"

#include <QProcess>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace opentree::ProcessUtils {

bool runHidden(const QString &program, const QStringList &arguments, int timeoutMs,
               int *exitCode, QByteArray *standardError, QString *errorMessage)
{
    if (program.trimmed().isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("No program was given to run.");
        }
        return false;
    }

    QProcess process;
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
        arguments->startupInfo->dwFlags |= STARTF_USESHOWWINDOW;
        arguments->startupInfo->wShowWindow = SW_HIDE;
    });
#endif

    process.start(program, arguments);
    if (!process.waitForStarted(timeoutMs)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Could not start %1: %2").arg(program, process.errorString());
        }
        return false;
    }

    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        if (errorMessage) {
            *errorMessage = QStringLiteral("%1 did not finish within %2 ms.").arg(program).arg(timeoutMs);
        }
        return false;
    }

    if (exitCode) {
        *exitCode = process.exitCode();
    }
    if (standardError) {
        *standardError = process.readAllStandardError();
    }
    if (errorMessage) {
        errorMessage->clear();
    }
    return true;
}

} // namespace opentree::ProcessUtils
