// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScriptedAlgorithmLoader.h"
#include "../AlgorithmRegistry.h"
#include "ScriptedAlgorithm.h"
#include "core/logging.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace PlasmaZones {

// Single file-scope constant shared by loadFromDirectory() and reWatchFiles()
static constexpr int MaxWatchedFilesPerDir = 100;

ScriptedAlgorithmLoader::ScriptedAlgorithmLoader(QObject* parent)
    : QObject(parent)
{
    // Lazy user directory creation — moved ensureUserDirectoryExists()
    // to scanAndRegister() so it is only called when actually needed.
    setupFileWatcher();
}

ScriptedAlgorithmLoader::~ScriptedAlgorithmLoader()
{
    // Guard: during static destruction, AlgorithmRegistry (a Meyer's singleton)
    // may already be destroyed. QCoreApplication being null is a reliable proxy
    // for "we are in static destruction" — skip cleanup to avoid dangling pointer.
    // Registry deletes its owned algorithms unconditionally for leak prevention;
    // the loader only manages registration state, which is meaningless without
    // QCoreApplication.
    if (!QCoreApplication::instance())
        return;
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

    // Deduplicate by canonical path — QStandardPaths::locateAll can return
    // the same physical directory via different XDG_DATA_DIRS entries or symlinks,
    // which causes scanAndRegister() to see duplicate registrations and mistakenly
    // unregister valid algorithms as "stale" during refresh.
    QSet<QString> seen;
    QStringList deduped;
    for (const QString& dir : std::as_const(dirs)) {
        const QString canonical = QFileInfo(dir).canonicalFilePath();
        if (!canonical.isEmpty() && !seen.contains(canonical)) {
            seen.insert(canonical);
            deduped.append(dir);
        }
    }
    return deduped;
}

void ScriptedAlgorithmLoader::watchDirectory(const QString& dirPath)
{
    if (!QDir(dirPath).exists()) {
        return;
    }
    m_watcher->addPath(dirPath);

    // Watch individual .js files so in-place content modifications trigger fileChanged.
    // Directory-level inotify only fires for create/delete/rename, not in-place writes.
    const QStringList validFiles = validatedJsFiles(dirPath, MaxWatchedFilesPerDir);
    for (const QString& file : validFiles) {
        m_watcher->addPath(file);
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
            // Restrict user algorithm directory to owner-only access (0700)
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
    // Lazily create user directory on first scan
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
        // Use canonical paths to handle symlinks and relative path differences
        loadFromDirectory(dir, QFileInfo(dir).canonicalFilePath() == QFileInfo(userDir).canonicalFilePath());
    }

    // Collect all newly registered script IDs
    for (auto it = m_scriptIdToPath.constBegin(); it != m_scriptIdToPath.constEnd(); ++it) {
        newScriptIds.insert(it.key());
    }

    // Remove stale scripts that no longer exist on disk.
    // AlgorithmRegistry::unregisterAlgorithm() uses deleteLater(), so the
    // algorithm object lives until the event loop drains the deferred-delete
    // queue — safe for any in-flight signal handlers.
    bool changed = false;
    for (auto it = oldScriptIdToPath.constBegin(); it != oldScriptIdToPath.constEnd(); ++it) {
        if (!newScriptIds.contains(it.key())) {
            registry->unregisterAlgorithm(it.key());
            qCInfo(lcAutotile) << "Unregistered stale scripted algorithm:" << it.key();
            changed = true;
        }
    }

    // Also detect newly added or updated scripts (not just removals).
    // NOTE: `changed` only tracks ID/path changes, not content edits —
    // content change detection is handled by always emitting algorithmsChanged() below.
    if (!changed) {
        for (const QString& id : std::as_const(newScriptIds)) {
            if (!oldScriptIdToPath.contains(id) || oldScriptIdToPath.value(id) != m_scriptIdToPath.value(id)) {
                changed = true;
                break;
            }
        }
    }

    qCInfo(lcAutotile) << "Scripted algorithms loaded:" << m_scriptIdToPath.size() << "changed=" << changed;
    // Always emit — the signal is cheap (listeners just refresh their model)
    // and content-only edits (same IDs/paths but different script body) would
    // otherwise be missed by the old change-detection logic.
    Q_EMIT algorithmsChanged();
    return changed;
}

void ScriptedAlgorithmLoader::loadFromDirectory(const QString& dir, bool isUserDir)
{
    const QStringList validFiles = validatedJsFiles(dir, MaxWatchedFilesPerDir);

    for (const QString& fullPath : validFiles) {
        // Validate filename against whitelist to prevent injection via crafted filenames
        static const QRegularExpression validIdRe(QStringLiteral("^[a-zA-Z0-9_-]+$"));
        const QString baseName = QFileInfo(fullPath).completeBaseName();
        if (!validIdRe.match(baseName).hasMatch()) {
            qCWarning(lcAutotile) << "Skipping script with invalid filename:" << fullPath;
            continue;
        }
        // Create with nullptr parent so the registry takes full ownership
        // via setParent(this) in registerAlgorithm(). If the algo is invalid,
        // we delete it explicitly below.
        auto* algo = new ScriptedAlgorithm(fullPath, nullptr);
        if (!algo->isValid()) {
            qCWarning(lcAutotile) << "Invalid scripted algorithm, skipping:" << fullPath;
            delete algo;
            continue;
        }

        // Use @builtinId metadata if present, otherwise default to "script:filename"
        const QString scriptId =
            algo->builtinId().isEmpty() ? (QStringLiteral("script:") + baseName) : algo->builtinId();

        // registerAlgorithm() handles replacement internally (removes old,
        // takes ownership of new) — no need to unregister first.
        auto* registry = AlgorithmRegistry::instance();
        algo->setUserScript(isUserDir);

        // Warn when a script overrides an existing algorithm ID.
        // For system scripts with duplicate @builtinId, skip registration
        // to prevent silent replacement of a bundled algorithm.
        if (registry->hasAlgorithm(scriptId)) {
            if (isUserDir) {
                qCWarning(lcAutotile) << "User script overrides bundled algorithm:" << scriptId << "from=" << fullPath;
            } else {
                qCWarning(lcAutotile) << "Duplicate system script for algorithm:" << scriptId << "from=" << fullPath
                                      << "— skipping (first registration wins)";
                // Defensive: ensure scriptId is tracked so the stale-removal pass
                // in scanAndRegister() does not unregister it. In normal operation
                // the first directory scan already populated m_scriptIdToPath, so
                // this guard is a no-op — it only fires if a future code path
                // clears the map between individual directory scans within a single
                // refresh cycle.
                if (!m_scriptIdToPath.contains(scriptId)) {
                    m_scriptIdToPath[scriptId] = fullPath;
                }
                delete algo;
                continue;
            }
        }

        registry->registerAlgorithm(scriptId, algo);
        m_scriptIdToPath[scriptId] = fullPath;

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
    // scanAndRegister() emits algorithmsChanged() internally when changed,
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
    // Build QSets once to avoid O(n^2) repeated contains() calls
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
        // Validate path containment and cap file count (same as loadFromDirectory)
        const QStringList validFiles = validatedJsFiles(dir, MaxWatchedFilesPerDir);
        for (const QString& file : validFiles) {
            if (!watchedFiles.contains(file)) {
                m_watcher->addPath(file);
            }
        }
    }
}

QStringList ScriptedAlgorithmLoader::validatedJsFiles(const QString& dirPath, int maxFiles) const
{
    QStringList result;
    QDir dirObj(dirPath);
    if (!dirObj.exists())
        return result;

    const QString canonicalDir = QFileInfo(dirPath).canonicalFilePath();
    if (canonicalDir.isEmpty())
        return result;

    const QStringList files =
        dirObj.entryList({QStringLiteral("*.js")}, QDir::Files | QDir::NoSymLinks | QDir::Readable);
    for (const QString& file : files) {
        if (result.size() >= maxFiles) {
            qCWarning(lcAutotile) << "Reached max file limit (" << maxFiles << ") for directory:" << dirPath
                                  << "— skipping remaining files";
            break;
        }
        const QString fullPath = QFileInfo(dirObj.filePath(file)).canonicalFilePath();
        if (fullPath.isEmpty())
            continue;
        if (!fullPath.startsWith(canonicalDir + QLatin1Char('/'))) {
            qCWarning(lcAutotile) << "Script path escaped directory:" << fullPath;
            continue;
        }
        result.append(fullPath);
    }
    return result;
}

} // namespace PlasmaZones
