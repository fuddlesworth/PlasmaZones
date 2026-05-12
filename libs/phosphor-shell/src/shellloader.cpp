// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellLoader.h>

#include <QDir>
#include <QFileInfo>
#include <QLatin1String>
#include <QLoggingCategory>
#include <QStandardPaths>

Q_LOGGING_CATEGORY(lcShellLoader, "phosphorshell.shellloader")

namespace PhosphorShell {

namespace {

// Reject anything that could escape the config dir or jump filesystems.
// shellName is supplied by the executable embedder; a bug there must not
// translate into "load shell.qml from /etc" or "from ../../somewhere".
bool isValidShellName(const QString& name)
{
    if (name.isEmpty()) {
        return false;
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        return false;
    }
    if (name == QLatin1String(".") || name == QLatin1String("..")) {
        return false;
    }
    return true;
}

} // namespace

ShellLoader::ShellLoader(const QString& shellName)
    : m_shellName(shellName)
{
    if (!isValidShellName(m_shellName)) {
        qCWarning(lcShellLoader) << "Rejecting invalid shell name (path-like input):" << m_shellName;
        m_shellName.clear();
    }
}

QUrl ShellLoader::resolve() const
{
    if (m_shellName.isEmpty()) {
        return {};
    }
    const QStringList configPaths = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);
    // Also probe DataLocation so a bundled example shipped to
    // ${DATADIR}/<shellName>/ can be the fallback when the user hasn't
    // installed a custom shell.qml in their config dir yet.
    const QStringList dataPaths = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    QStringList searched = configPaths;
    searched.append(dataPaths);

    for (const QString& basePath : std::as_const(searched)) {
        // QDir::filePath joins normally and collapses any trailing slash on
        // basePath, avoiding `/foo//phosphor-shell/shell.qml`. The filesystem
        // tolerates the doubled slash but it leaks into log output and
        // breaks symbolic comparisons of the resolved path.
        const QString shellDir = QDir(basePath).filePath(m_shellName);
        const QString candidate = QDir(shellDir).filePath(QStringLiteral("shell.qml"));
        if (QFileInfo::exists(candidate)) {
            return QUrl::fromLocalFile(candidate);
        }
    }

    return {};
}

QString ShellLoader::shellConfigDir() const
{
    if (m_shellName.isEmpty()) {
        return {};
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)).filePath(m_shellName);
}

} // namespace PhosphorShell
