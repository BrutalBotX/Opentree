#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace opentree::ProcessUtils {

// Runs a console program and waits for it without flashing a console window. CreateProcess
// is called with CREATE_NO_WINDOW and a hidden start-up window, which matters for helpers
// such as schtasks.exe that would otherwise pop a command prompt over the app.
bool runHidden(const QString &program, const QStringList &arguments, int timeoutMs,
               int *exitCode = nullptr, QByteArray *standardError = nullptr,
               QString *errorMessage = nullptr);

} // namespace opentree::ProcessUtils
