// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/ScriptedAlgorithm.h>
#include <PhosphorTiles/ScriptedAlgorithmWatchdog.h>
#include "tileslogging.h"

#include <PhosphorFsLoader/IScanStrategy.h>
#include <PhosphorFsLoader/WatchedDirectorySet.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace PhosphorTiles {

// Per-directory cap shared by loadFromDirectory() and the validated-files
// helper used by the scan strategy.
static constexpr int MaxWatchedFilesPerDir = 100;

/// Scan strategy backing the loader's `WatchedDirectorySet`. The strategy
/// ignores the base's directory list and re-derives the algorithm
/// directories from `m_subdirectory` on every rescan — XDG path
/// resolution can change between rescans (e.g. user-dir creation) and
/// the registry-side stale-script purge depends on a full re-walk.
class ScriptedAlgorithmLoader::JsScanStrategy : public PhosphorFsLoader::IScanStrategy
{
public:
    explicit JsScanStrategy(ScriptedAlgorithmLoader& loader)
        : m_loader(&loader)
    {
    }

    QStringList performScan(const QStringList&) override
    {
        return m_loader->performScan();
    }

private:
    ScriptedAlgorithmLoader* m_loader;
};

ScriptedAlgorithmLoader::ScriptedAlgorithmLoader(const QString& subdirectory, ITileAlgorithmRegistry* registry,
                                                 QObject* parent)
    : QObject(parent)
    , m_subdirectory(subdirectory)
    , m_registry(registry)
    , m_watchdog(std::make_shared<ScriptedAlgorithmWatchdog>())
    , m_strategy(std::make_unique<JsScanStrategy>(*this))
    , m_watcher(std::make_unique<PhosphorFsLoader::WatchedDirectorySet>(*m_strategy, this))
{
    Q_ASSERT(m_registry);
    // The subdirectory is appended to XDG data roots (system + user),
    // so it must be relative and free of traversal segments. Developer
    // error — assert rather than silently scan an unexpected path.
    // Empty subdirectory is explicitly supported as "loader disabled".
    Q_ASSERT_X(!m_subdirectory.startsWith(QLatin1Char('/')), "ScriptedAlgorithmLoader",
               "subdirectory must be a relative XDG path, not absolute");
    Q_ASSERT_X(!m_subdirectory.contains(QLatin1String("..")), "ScriptedAlgorithmLoader",
               "subdirectory must not contain '..' traversal");

    // Directory registration + the initial scan happen on the first
    // `scanAndRegister()` call. Deferring lets consumers connect their
    // `algorithmsChanged` slot before the empty→populated-set signal
    // fires — the canonical daemon shape is `make_unique → connect →
    // scanAndRegister` and a synchronous emit from inside the ctor
    // would bypass the slot entirely.
}

ScriptedAlgorithmLoader::~ScriptedAlgorithmLoader()
{
    // Guard: during static destruction the injected registry may already
    // be destroyed. QCoreApplication being null is a reliable proxy for
    // "we are in static destruction" — skip cleanup to avoid calling into
    // a half-torn-down registry.
    //
    // Order: this destructor body calls unregisterAlgorithm() per script,
    // which uses deleteLater() — so algorithm dtors run on a later
    // event-loop pass, AFTER this loader's members destruct. The
    // shared_ptr<watchdog> design lets the deferred-delete algorithm
    // keep the watchdog alive until ~ScriptedAlgorithm finally fires;
    // the thread joins when the last shared_ptr (loader's or a
    // deferred algo's) is released. No member-ordering trick required.
    if (!QCoreApplication::instance() || !m_registry)
        return;
    for (auto it = m_scriptIdToPath.constBegin(); it != m_scriptIdToPath.constEnd(); ++it) {
        m_registry->unregisterAlgorithm(it.key());
    }
}

QString ScriptedAlgorithmLoader::userAlgorithmDir() const
{
    if (m_subdirectory.isEmpty())
        return QString();
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (dataDir.isEmpty())
        return QString();
    return dataDir + QLatin1Char('/') + m_subdirectory;
}

QStringList ScriptedAlgorithmLoader::algorithmDirectories() const
{
    if (m_subdirectory.isEmpty())
        return {};

    QStringList dirs =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, m_subdirectory, QStandardPaths::LocateDirectory);

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

void ScriptedAlgorithmLoader::ensureUserDirectoryExists()
{
    const QString dirPath = userAlgorithmDir();
    if (dirPath.isEmpty()) {
        qCWarning(PhosphorTiles::lcTilesLib) << "Cannot determine user data directory; skipping algorithm dir creation";
        return;
    }
    QDir dir(dirPath);
    if (!dir.exists()) {
        if (dir.mkpath(QStringLiteral("."))) {
            // Restrict user algorithm directory to owner-only access (0700)
            QFile::setPermissions(dir.absolutePath(),
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
            qCInfo(PhosphorTiles::lcTilesLib) << "Created user algorithm directory:" << dir.absolutePath();
        } else {
            qCWarning(PhosphorTiles::lcTilesLib) << "Failed to create user algorithm directory:" << dir.absolutePath();
        }
    }
}

bool ScriptedAlgorithmLoader::scanAndRegister()
{
    // Lazily create user directory on first scan, then re-register the
    // (possibly newly-existing) directory set with the watcher so the
    // freshly-created user dir gets a direct watch instead of staying
    // proxied via parent-watch.
    ensureUserDirectoryExists();
    if (!m_subdirectory.isEmpty()) {
        const QStringList dirs = algorithmDirectories();
        if (!dirs.isEmpty()) {
            m_watcher->registerDirectories(dirs, PhosphorFsLoader::LiveReload::On);
        }
    }

    // Synchronous rescan via the strategy. Returns the change flag.
    const QByteArray priorSignature = m_lastScriptSignature;
    m_watcher->rescanNow();
    return m_lastScriptSignature != priorSignature;
}

QStringList ScriptedAlgorithmLoader::performScan()
{
    auto* registry = m_registry;

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
    for (auto it = oldScriptIdToPath.constBegin(); it != oldScriptIdToPath.constEnd(); ++it) {
        if (!newScriptIds.contains(it.key())) {
            registry->unregisterAlgorithm(it.key());
            qCInfo(PhosphorTiles::lcTilesLib) << "Unregistered stale scripted algorithm:" << it.key();
        }
    }

    qCInfo(PhosphorTiles::lcTilesLib) << "Scripted algorithms loaded:" << m_scriptIdToPath.size();

    // Emit only when the registered script set — id, path, size, mtime —
    // actually differs from the last scan. Suppresses redundant emissions
    // from no-op filesystem pokes (editor-save of an unrelated file in
    // the watched dir, lstat-only events, etc.). Downstream listeners
    // (layout adaptor → D-Bus → every subscribed client) get one
    // notification per real change instead of one per inotify wake.
    QCryptographicHash hasher(QCryptographicHash::Sha1);
    QList<QString> sortedIds = m_scriptIdToPath.keys();
    std::sort(sortedIds.begin(), sortedIds.end());
    for (const QString& id : std::as_const(sortedIds)) {
        const QString& path = m_scriptIdToPath.value(id);
        QFileInfo info(path);
        hasher.addData(id.toUtf8());
        hasher.addData(QByteArrayView("|"));
        hasher.addData(path.toUtf8());
        hasher.addData(QByteArrayView("|"));
        hasher.addData(QByteArray::number(info.size()));
        hasher.addData(QByteArrayView("|"));
        hasher.addData(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
        hasher.addData(QByteArrayView("\n"));
    }
    const QByteArray signature = hasher.result();
    if (signature != m_lastScriptSignature) {
        m_lastScriptSignature = signature;
        Q_EMIT algorithmsChanged();
    }

    // Tell the base which individual files to install per-file watches
    // on. The base re-arms these every rescan, which closes the
    // atomic-rename inode-replacement window the legacy followup timer
    // used to cover.
    QStringList desiredFileWatches;
    desiredFileWatches.reserve(m_scriptIdToPath.size());
    for (auto it = m_scriptIdToPath.constBegin(); it != m_scriptIdToPath.constEnd(); ++it) {
        desiredFileWatches.append(it.value());
    }
    return desiredFileWatches;
}

void ScriptedAlgorithmLoader::loadFromDirectory(const QString& dir, bool isUserDir)
{
    const QStringList validFiles = validatedJsFiles(dir, MaxWatchedFilesPerDir);

    for (const QString& fullPath : validFiles) {
        // Validate filename against whitelist to prevent injection via crafted filenames
        static const QRegularExpression validIdRe(QStringLiteral("^[a-zA-Z0-9_-]+$"));
        const QString baseName = QFileInfo(fullPath).completeBaseName();
        if (!validIdRe.match(baseName).hasMatch()) {
            qCWarning(PhosphorTiles::lcTilesLib) << "Skipping script with invalid filename:" << fullPath;
            continue;
        }
        // Create with nullptr parent so the registry takes full ownership
        // via setParent(this) in registerAlgorithm(). If the algo is invalid,
        // we delete it explicitly below. Pass the watchdog as shared_ptr
        // so the algorithm keeps it alive across deferred-delete teardown
        // (registry uses deleteLater(); algorithm dtor can run after the
        // loader is gone — see ScriptedAlgorithm.h for the contract).
        auto* algo = new ScriptedAlgorithm(fullPath, m_watchdog, nullptr);
        if (!algo->isValid()) {
            qCWarning(PhosphorTiles::lcTilesLib) << "Invalid scripted algorithm, skipping:" << fullPath;
            delete algo;
            continue;
        }

        // Use id metadata if present, otherwise default to "script:filename"
        const QString scriptId = algo->id().isEmpty() ? (QStringLiteral("script:") + baseName) : algo->id();

        // registerAlgorithm() handles replacement internally (removes old,
        // takes ownership of new) — no need to unregister first.
        auto* registry = m_registry;
        algo->setUserScript(isUserDir);

        // Duplicate detection is scoped to THIS scan. m_scriptIdToPath was
        // cleared at the top of scanAndRegister, so a hit here means an
        // earlier loadFromDirectory call in the same scan already
        // registered this id. The registry itself still holds entries from
        // previous scans — using registry->hasAlgorithm would incorrectly
        // classify every system-script content edit as a duplicate on
        // every rescan and skip re-registration, silently disabling hot-
        // reload for system scripts.
        //
        // For user-dir overrides of a bundled script: warn and fall
        // through to registerAlgorithm, which replaces cleanly.
        // For cross-system-dir duplicates (same id in two XDG roots):
        // first registration wins, skip the rest.
        if (m_scriptIdToPath.contains(scriptId)) {
            if (isUserDir) {
                qCWarning(PhosphorTiles::lcTilesLib)
                    << "User script overrides bundled algorithm:" << scriptId << "from=" << fullPath;
            } else {
                qCWarning(PhosphorTiles::lcTilesLib) << "Duplicate system script for algorithm:" << scriptId
                                                     << "from=" << fullPath << "— skipping (first registration wins)";
                delete algo;
                continue;
            }
        }

        registry->registerAlgorithm(scriptId, algo);
        m_scriptIdToPath[scriptId] = fullPath;

        qCInfo(PhosphorTiles::lcTilesLib)
            << "Registered scripted algorithm:" << scriptId << "from=" << fullPath << "user=" << isUserDir;
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

    // No QDir::NoSymLinks — symlink escapes are already prevented by the
    // canonical-path containment check below (each candidate file must resolve
    // inside canonicalDir after symlink expansion). Leaving NoSymLinks in the
    // filter would additionally block benign user-local symlinks pointing at
    // read-only system script directories.
    const QStringList files = dirObj.entryList({QStringLiteral("*.js")}, QDir::Files | QDir::Readable);
    for (const QString& file : files) {
        if (result.size() >= maxFiles) {
            qCWarning(PhosphorTiles::lcTilesLib) << "Reached max file limit (" << maxFiles
                                                 << ") for directory:" << dirPath << "— skipping remaining files";
            break;
        }
        const QString fullPath = QFileInfo(dirObj.filePath(file)).canonicalFilePath();
        if (fullPath.isEmpty())
            continue;
        if (!fullPath.startsWith(canonicalDir + QLatin1Char('/'))) {
            qCWarning(PhosphorTiles::lcTilesLib) << "Script path escaped directory:" << fullPath;
            continue;
        }
        result.append(fullPath);
    }
    return result;
}

} // namespace PhosphorTiles
