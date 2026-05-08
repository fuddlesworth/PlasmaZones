// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellLoader.h>

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace PhosphorShell {

ShellLoader::ShellLoader(const QString& shellName)
    : m_shellName(shellName)
{
}

QUrl ShellLoader::resolve() const
{
    const QStringList configPaths = QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation);

    for (const QString& basePath : configPaths) {
        const QString candidate =
            basePath + QDir::separator() + m_shellName + QDir::separator() + QStringLiteral("shell.qml");
        if (QFileInfo::exists(candidate)) {
            return QUrl::fromLocalFile(candidate);
        }
    }

    return {};
}

QString ShellLoader::shellConfigDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QDir::separator() + m_shellName;
}

} // namespace PhosphorShell
