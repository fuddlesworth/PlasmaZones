// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithmLoader.h"
#include "../AlgorithmRegistry.h"
#include "ScriptedAlgorithm.h"
#include "core/logging.h"
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

namespace PlasmaZones {

ScriptedAlgorithmLoader::ScriptedAlgorithmLoader(QObject* parent)
    : QObject(parent)
{
    ensureUserDirectoryExists();
    setupFileWatcher();
}

ScriptedAlgorithmLoader::~ScriptedAlgorithmLoader()
{
    auto* registry = AlgorithmRegistry::instance();
    for (auto it = m_scriptIdToPath.constBegin(); it != m_scriptIdToPath.constEnd(); ++it) {
        registry->unregisterAlgorithm(it.key());
    }
}

QString ScriptedAlgorithmLoader::userAlgorithmDir() const
{
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataDir.isEmpty())
        return QString();
    return dataDir + QStringLiteral("/plasmazones/algorithms");
}

void ScriptedAlgorithmLoader::ensureUserDirectoryExists()
{
    const QString dirPath = userAlgorithmDir();
    if (dirPath.isEmpty()) {
        qCWarning(lcAutotile) << "Cannot determine user data directory; skipping algorithm dir creation";
        return;
    }
    QDir dir(dirPath);
    if (!dir.exists()) {
        if (dir.mkpath(QStringLiteral("."))) {
            qCInfo(lcAutotile) << "Created user algorithm directory:" << dir.absolutePath();
        } else {
            qCWarning(lcAutotile) << "Failed to create user algorithm directory:" << dir.absolutePath();
        }
    }
}

void ScriptedAlgorithmLoader::scanAndRegister()
{
    auto* registry = AlgorithmRegistry::instance();

    // Track which script IDs we register in this scan
    QSet<QString> newScriptIds;

    // Save old tracking map so we can detect stale entries afterwards
    QHash<QString, QString> oldScriptIdToPath = m_scriptIdToPath;
    m_scriptIdToPath.clear();

    // Find ALL algorithm directories across XDG data paths
    QStringList dirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("plasmazones/algorithms"),
        QStandardPaths::LocateDirectory);

    // Reverse: system dirs first, user dirs last (user overrides system by same filename)
    std::reverse(dirs.begin(), dirs.end());

    const QString userDir = userAlgorithmDir();
    for (const QString& dir : std::as_const(dirs)) {
        loadFromDirectory(dir, dir == userDir);
    }

    // Collect all newly registered script IDs
    for (auto it = m_scriptIdToPath.constBegin(); it != m_scriptIdToPath.constEnd(); ++it) {
        newScriptIds.insert(it.key());
    }

    // Remove stale scripts that no longer exist on disk
    for (auto it = oldScriptIdToPath.constBegin(); it != oldScriptIdToPath.constEnd(); ++it) {
        if (!newScriptIds.contains(it.key())) {
            registry->unregisterAlgorithm(it.key());
            qCInfo(lcAutotile) << "Unregistered stale scripted algorithm:" << it.key();
        }
    }

    qCInfo(lcAutotile) << "Scripted algorithms loaded:" << m_scriptIdToPath.size();
}

void ScriptedAlgorithmLoader::loadFromDirectory(const QString& dir, bool isUserDir)
{
    QDir dirObj(dir);
    const QStringList files = dirObj.entryList({QStringLiteral("*.js")}, QDir::Files);

    for (const QString& file : files) {
        const QString fullPath = dirObj.filePath(file);
        const QString scriptId = QStringLiteral("script:") + QFileInfo(file).baseName();

        auto* algo = new ScriptedAlgorithm(fullPath, nullptr);
        if (!algo->isValid()) {
            qCWarning(lcAutotile) << "Invalid scripted algorithm, skipping:" << fullPath;
            delete algo;
            continue;
        }

        algo->setUserScript(isUserDir);
        AlgorithmRegistry::instance()->registerAlgorithm(scriptId, algo);
        m_scriptIdToPath[scriptId] = fullPath;

        qCInfo(lcAutotile) << "Registered scripted algorithm:" << scriptId
                           << "from=" << fullPath << "user=" << isUserDir;
    }
}

void ScriptedAlgorithmLoader::setupFileWatcher()
{
    m_watcher = new QFileSystemWatcher(this);

    auto watchDir = [this](const QString& dir) {
        if (!QDir(dir).exists()) {
            return;
        }
        m_watcher->addPath(dir);

        // Watch individual .js files so in-place content modifications trigger fileChanged.
        // Directory-level inotify only fires for create/delete/rename, not in-place writes.
        QDir dirObj(dir);
        const QStringList jsFiles = dirObj.entryList({QStringLiteral("*.js")}, QDir::Files);
        for (const QString& file : jsFiles) {
            m_watcher->addPath(dirObj.filePath(file));
        }

        qCInfo(lcAutotile) << "Watching algorithm directory:" << dir
                           << "paths=" << m_watcher->files().size() + m_watcher->directories().size();
    };

    // Watch ALL algorithm directories (system + user). locateAll() returns them
    // in priority order (user first, system last).
    const QStringList allDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("plasmazones/algorithms"),
        QStandardPaths::LocateDirectory);
    for (const QString& dir : allDirs) {
        watchDir(dir);
    }

    // Also watch the writable user dir even if it has no scripts yet
    // (user may add custom algorithms later)
    const QString userDir = userAlgorithmDir();
    if (!userDir.isEmpty() && !allDirs.contains(userDir)) {
        watchDir(userDir);
    }

    connect(m_watcher, &QFileSystemWatcher::directoryChanged,
            this, &ScriptedAlgorithmLoader::onDirectoryChanged);
    connect(m_watcher, &QFileSystemWatcher::fileChanged,
            this, &ScriptedAlgorithmLoader::onFileChanged);
}

void ScriptedAlgorithmLoader::onDirectoryChanged(const QString& path)
{
    qCInfo(lcAutotile) << "Algorithm directory change detected:" << path;
    scheduleRefresh();
}

void ScriptedAlgorithmLoader::onFileChanged(const QString& path)
{
    qCInfo(lcAutotile) << "Algorithm file change detected:" << path;

    // QFileSystemWatcher drops the watch after atomic rename (new inode).
    // Re-add the path if the file still exists so future edits are caught.
    if (QFile::exists(path) && m_watcher && !m_watcher->files().contains(path)) {
        m_watcher->addPath(path);
    }

    scheduleRefresh();
}

void ScriptedAlgorithmLoader::scheduleRefresh()
{
    // Debounce rapid changes (e.g., editor auto-save, batch installs)
    if (!m_refreshTimer) {
        m_refreshTimer = new QTimer(this);
        m_refreshTimer->setSingleShot(true);
        m_refreshTimer->setInterval(RefreshDebounceMs);
        connect(m_refreshTimer, &QTimer::timeout,
                this, &ScriptedAlgorithmLoader::performDebouncedRefresh);
    }

    m_refreshTimer->start();
}

void ScriptedAlgorithmLoader::performDebouncedRefresh()
{
    qCInfo(lcAutotile) << "Algorithm directory changed, refreshing...";
    scanAndRegister();
    reWatchFiles();
    Q_EMIT algorithmsChanged();
}

void ScriptedAlgorithmLoader::reWatchFiles()
{
    if (!m_watcher) {
        return;
    }

    // After a refresh (which may follow delete+recreate), re-add any
    // .js files that lost their inotify watch due to inode replacement.
    auto reWatch = [this](const QString& dir) {
        if (!QDir(dir).exists()) {
            return;
        }
        // Re-watch the directory itself
        if (!m_watcher->directories().contains(dir)) {
            m_watcher->addPath(dir);
        }
        QDir dirObj(dir);
        const QStringList files = dirObj.entryList({QStringLiteral("*.js")}, QDir::Files);
        for (const QString& file : files) {
            const QString fullPath = dirObj.filePath(file);
            if (!m_watcher->files().contains(fullPath)) {
                m_watcher->addPath(fullPath);
            }
        }
    };

    const QStringList allDirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation,
        QStringLiteral("plasmazones/algorithms"),
        QStandardPaths::LocateDirectory);
    for (const QString& dir : allDirs) {
        reWatch(dir);
    }
    const QString userDir = userAlgorithmDir();
    if (!userDir.isEmpty() && !allDirs.contains(userDir)) {
        reWatch(userDir);
    }
}

} // namespace PlasmaZones
