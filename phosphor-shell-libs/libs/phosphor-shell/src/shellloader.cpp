// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShell/ShellLoader.h>

#include <QDir>
#include <QFile>
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
    // A whitespace-only name is non-empty but resolves to a nonsense
    // directory ("<config>/   /shell.qml") — reject it.
    if (name.trimmed().isEmpty()) {
        return false;
    }
    if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\'))) {
        return false;
    }
    if (name == QLatin1String(".") || name == QLatin1String("..")) {
        return false;
    }
    // Control characters (NUL, newline, etc.) never belong in a
    // directory name — they corrupt log output and path comparisons.
    for (const QChar ch : name) {
        if (ch.category() == QChar::Other_Control) {
            return false;
        }
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

    // Final fallback: a qrc-baked example shell linked into the binary at
    // build time (see examples/phosphor-shell/CMakeLists.txt — registers
    // a qt-qml-module under URI `Phosphor.Shell.Example`). Qt's
    // qt_add_qml_module places module files at
    // `:/qt/qml/<URI-with-slashes>/shell.qml`; QFile resolves the `:/`
    // scheme via the static resource registry that the static plugin's
    // initializer populates at link time. Lets the executable run on a
    // fresh system that has no user config and skipped the optional
    // DATADIR install.
    const QString qrcPath = QStringLiteral(":/qt/qml/Phosphor/Shell/Example/shell.qml");
    if (QFile::exists(qrcPath)) {
        return QUrl(QStringLiteral("qrc") + qrcPath);
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
