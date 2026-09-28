// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include <PhosphorShell/ShellEngine.h>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QTimer>
#include <memory>

namespace {
Q_LOGGING_CATEGORY(lcShellWatcher, "phosphorshell.engine")
}
namespace PhosphorShell {
void ShellEngine::setupWatcher()
{
    // A qrc: shell URL has no local file to watch, and it is a reachable case
    // (ShellLoader falls back to the bundled example). Without this the two
    // addPath calls below would each log "path is empty" at every startup.
    if (!m_shellUrl.isLocalFile()) {
        qCDebug(lcShellWatcher) << "shell URL is not a local file; hot reload disabled";
        return;
    }
    if (m_watcher) {
        return;
    }

    m_watcher = new QFileSystemWatcher(this);

    // Both returns are checked. When the per-user inotify watch limit is
    // exhausted these fail, and hot reload then stops working for the life of
    // the process with nothing logged at any level: the re-arm below can only
    // run from a reload, and the missing watch is what would have caused one.
    const QString filePath = m_shellUrl.toLocalFile();
    if (!m_watcher->addPath(filePath)) {
        qCWarning(lcShellWatcher) << "could not watch" << filePath
                                  << "— hot reload is disabled (inotify watch limit reached?)";
    }

    const QString dir = QFileInfo(filePath).absolutePath();
    if (!m_watcher->addPath(dir)) {
        qCWarning(lcShellWatcher) << "could not watch" << dir << "— an atomic-rename save will not trigger a reload";
    }

    const auto fingerprint = [filePath]() {
        QFile source(filePath);
        if (!source.open(QIODevice::ReadOnly))
            return QByteArray();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        hash.addData(&source);
        return hash.result();
    };
    auto previous = std::make_shared<QByteArray>(fingerprint());
    auto kickReload = [this, filePath, fingerprint, previous]() {
        // A QSaveFile beside shell.qml is not a source edit. Directory
        // events still re-arm the watch after an atomic source replacement,
        // even if that replacement has exactly the same contents.
        if (!m_watcher->files().contains(filePath) && QFileInfo::exists(filePath))
            m_watcher->addPath(filePath);
        const auto current = fingerprint();
        if (current == *previous)
            return;
        *previous = current;
        m_reloadTimer->start();
    };
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, kickReload);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, kickReload);

    qCDebug(lcShellWatcher) << "Watching for changes:" << filePath;
}

}
