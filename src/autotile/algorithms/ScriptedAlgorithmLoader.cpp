// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithmLoader.h"
#include "../AlgorithmRegistry.h"
#include "ScriptedAlgorithm.h"
#include "core/logging.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace PlasmaZones {

ScriptedAlgorithmLoader::ScriptedAlgorithmLoader(QObject* parent)
    : QObject(parent)
{
    // L11: Lazy user directory creation — moved ensureUserDirectoryExists()
    // to scanAndRegister() so it is only called when actually needed.
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

QStringList ScriptedAlgorithmLoader::algorithmDirectories() const
{
    QStringList dirs = QStandardPaths::locateAll(
        QStandardPaths::GenericDataLocation, QStringLiteral("plasmazones/algorithms"), QStandardPaths::LocateDirectory);

    const QString userDir = userAlgorithmDir();
    if (!userDir.isEmpty() && !dirs.contains(userDir)) {
        dirs.prepend(userDir);
    }
    return dirs;
}

void ScriptedAlgorithmLoader::watchDirectory(const QString& dirPath)
{
    if (!QDir(dirPath).exists()) {
        return;
    }
    m_watcher->addPath(dirPath);

    // Watch individual .js files so in-place content modifications trigger fileChanged.
    // Directory-level inotify only fires for create/delete/rename, not in-place writes.
    QDir dirObj(dirPath);
    const QStringList jsFiles = dirObj.entryList({QStringLiteral("*.js")}, QDir::Files | QDir::NoSymLinks);
    for (const QString& file : jsFiles) {
        m_watcher->addPath(dirObj.filePath(file));
    }

    qCInfo(lcAutotile) << "Watching algorithm directory:" << dirPath
                       << "paths=" << m_watcher->files().size() + m_watcher->directories().size();
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
            // B6: Restrict user algorithm directory to owner-only access (0700)
            QFile::setPermissions(dir.absolutePath(),
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            qCInfo(lcAutotile) << "Created user algorithm directory:" << dir.absolutePath();
        } else {
            qCWarning(lcAutotile) << "Failed to create user algorithm directory:" << dir.absolutePath();
        }
    }
}

bool ScriptedAlgorithmLoader::scanAndRegister()
{
    // L11: Lazily create user directory on first scan
    ensureUserDirectoryExists();

    auto* registry = AlgorithmRegistry::instance();

    // Track which script IDs we register in this scan
    QSet<QString> newScriptIds;

    // Save old tracking map so we can detect stale entries afterwards
    QHash<QString, QString> oldScriptIdToPath = m_scriptIdToPath;
    m_scriptIdToPath.clear();

    // Find ALL algorithm directories across XDG data paths
    QStringList dirs = algorithmDirectories();

    // Reverse: system dirs first, user dirs last (user overrides system by same filename)
    std::reverse(dirs.begin(), dirs.end());

    const QString userDir = userAlgorithmDir();
    for (const QString& dir : std::as_const(dirs)) {
        // E2: Use canonical paths to handle symlinks and relative path differences
        loadFromDirectory(dir, QFileInfo(dir).canonicalFilePath() == QFileInfo(userDir).canonicalFilePath());
    }

    // Collect all newly registered script IDs
    for (auto it = m_scriptIdToPath.constBegin(); it != m_scriptIdToPath.constEnd(); ++it) {
        newScriptIds.insert(it.key());
    }

    // Remove stale scripts that no longer exist on disk.
    // AlgorithmRegistry::unregisterAlgorithm() uses deleteLater(), so any
    // in-flight calculateZones() calls on the old algorithm object will
    // finish before it is destroyed.
    bool changed = false;
    for (auto it = oldScriptIdToPath.constBegin(); it != oldScriptIdToPath.constEnd(); ++it) {
        if (!newScriptIds.contains(it.key())) {
            registry->unregisterAlgorithm(it.key());
            qCInfo(lcAutotile) << "Unregistered stale scripted algorithm:" << it.key();
            changed = true;
        }
    }

    qCInfo(lcAutotile) << "Scripted algorithms loaded:" << m_scriptIdToPath.size() << "changed=" << changed;
    // M-5: Always emit — the signal is cheap (listeners just refresh their model)
    // and content-only edits (same IDs/paths but different script body) would
    // otherwise be missed by the old change-detection logic.
    Q_EMIT algorithmsChanged();
    return changed;
}

void ScriptedAlgorithmLoader::loadFromDirectory(const QString& dir, bool isUserDir)
{
    QDir dirObj(dir);
    // H4: Exclude symlinks to prevent path traversal attacks
    const QStringList files =
        dirObj.entryList({QStringLiteral("*.js")}, QDir::Files | QDir::NoSymLinks | QDir::Readable);

    // S-07: Directory containment check — prevent symlink/traversal escapes
    const QString canonicalDir = QFileInfo(dir).canonicalFilePath();
    if (canonicalDir.isEmpty())
        return;

    // S-08: Cap file watcher count to prevent resource exhaustion
    static constexpr int MaxWatchedFilesPerDir = 100;
    int filesProcessed = 0;

    for (const QString& file : files) {
        if (filesProcessed >= MaxWatchedFilesPerDir) {
            qCWarning(lcAutotile) << "Reached max file limit (" << MaxWatchedFilesPerDir << ") for directory:" << dir
                                  << "— skipping remaining files";
            break;
        }

        const QString rawPath = dirObj.filePath(file);
        const QString fullPath = QFileInfo(rawPath).canonicalFilePath();
        if (fullPath.isEmpty())
            continue; // file vanished between listing and stat

        // S-07: Verify resolved path stays within the algorithm directory
        if (!fullPath.startsWith(canonicalDir + QLatin1Char('/'))) {
            qCWarning(lcAutotile) << "Script path escaped directory:" << fullPath;
            continue;
        }

        // Validate filename against whitelist to prevent injection via crafted filenames
        static const QRegularExpression validIdRe(QStringLiteral("^[a-zA-Z0-9_-]+$"));
        const QString baseName = QFileInfo(file).completeBaseName();
        if (!validIdRe.match(baseName).hasMatch()) {
            qCWarning(lcAutotile) << "Skipping script with invalid filename:" << file;
            continue;
        }
        const QString scriptId = QStringLiteral("script:") + baseName;

        // H3: Create with nullptr parent so the registry takes full ownership
        // via setParent(this) in registerAlgorithm(). If the algo is invalid,
        // we delete it explicitly below.
        auto* algo = new ScriptedAlgorithm(fullPath, nullptr);
        if (!algo->isValid()) {
            qCWarning(lcAutotile) << "Invalid scripted algorithm, skipping:" << fullPath;
            delete algo;
            continue;
        }

        // registerAlgorithm() handles replacement internally (removes old,
        // takes ownership of new) — no need to unregister first.
        auto* registry = AlgorithmRegistry::instance();
        algo->setUserScript(isUserDir);
        registry->registerAlgorithm(scriptId, algo);
        m_scriptIdToPath[scriptId] = fullPath;
        ++filesProcessed;

        qCInfo(lcAutotile) << "Registered scripted algorithm:" << scriptId << "from=" << fullPath
                           << "user=" << isUserDir;
    }
}

void ScriptedAlgorithmLoader::setupFileWatcher()
{
    m_watcher = new QFileSystemWatcher(this);

    // Watch ALL algorithm directories (system + user).
    // algorithmDirectories() already includes the writable user dir
    // even if it was not returned by locateAll().
    const QStringList allDirs = algorithmDirectories();
    for (const QString& dir : allDirs) {
        watchDirectory(dir);
    }

    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &ScriptedAlgorithmLoader::onDirectoryChanged);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &ScriptedAlgorithmLoader::onFileChanged);
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
        connect(m_refreshTimer, &QTimer::timeout, this, &ScriptedAlgorithmLoader::performDebouncedRefresh);
    }

    m_refreshTimer->start();
}

void ScriptedAlgorithmLoader::performDebouncedRefresh()
{
    qCInfo(lcAutotile) << "Algorithm directory changed, refreshing...";
    // H-4: scanAndRegister() emits algorithmsChanged() internally when changed,
    // so we do NOT re-emit here to avoid double signal emission.
    scanAndRegister();
    reWatchFiles();
}

void ScriptedAlgorithmLoader::reWatchFiles()
{
    if (!m_watcher) {
        return;
    }

    // After a refresh (which may follow delete+recreate), re-add any
    // .js files that lost their inotify watch due to inode replacement.
    // algorithmDirectories() already includes the writable user dir.
    // M5: Build QSets once to avoid O(n^2) repeated contains() calls
    const QSet<QString> watchedDirs(m_watcher->directories().cbegin(), m_watcher->directories().cend());
    const QSet<QString> watchedFiles(m_watcher->files().cbegin(), m_watcher->files().cend());
    const QStringList allDirs = algorithmDirectories();
    for (const QString& dir : allDirs) {
        if (!QDir(dir).exists()) {
            continue;
        }
        // Re-watch the directory itself
        if (!watchedDirs.contains(dir)) {
            m_watcher->addPath(dir);
        }
        // H-5: Validate path containment (same as loadFromDirectory) to prevent
        // symlink/traversal escapes when re-adding file watches
        const QString canonicalDir = QFileInfo(dir).canonicalFilePath();
        if (canonicalDir.isEmpty())
            continue;
        QDir dirObj(dir);
        const QStringList files = dirObj.entryList({QStringLiteral("*.js")}, QDir::Files | QDir::NoSymLinks);
        for (const QString& file : files) {
            const QString rawPath = dirObj.filePath(file);
            const QString fullPath = QFileInfo(rawPath).canonicalFilePath();
            if (fullPath.isEmpty())
                continue;
            if (!fullPath.startsWith(canonicalDir + QLatin1Char('/'))) {
                qCWarning(lcAutotile) << "reWatchFiles: script path escaped directory:" << fullPath;
                continue;
            }
            if (!watchedFiles.contains(fullPath)) {
                m_watcher->addPath(fullPath);
            }
        }
    }
}

} // namespace PlasmaZones
