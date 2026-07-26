// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorTiles/ScriptedAlgorithmLoader.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/LuauTileAlgorithm.h>
#include "tileslogging.h"

#include <PhosphorScripting/LuauEngine.h>
#include <PhosphorScripting/LuauWatchdog.h>

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

#include <algorithm>
#include <utility>

namespace PhosphorTiles {

// Per-directory cap shared by loadFromDirectory() and the validated-files
// helper used by the scan strategy.
static constexpr int MaxWatchedFilesPerDir = 100;

// Hard cap on scripts REGISTERED per rescan, summed across every
// registered directory. Same magnitude as DirectoryLoader::kMaxEntries and
// MetadataPackScanStrategy::kDefaultMaxEntries. Those two count entries
// CONSIDERED, because counting registrations lets a spray of files that each
// parse and then register nothing slip past the guard entirely. This counter
// is allowed to count registrations only because the considered-count is
// already bounded upstream: `validatedLuauFiles` caps files considered per
// directory at MaxWatchedFilesPerDir, so the total work a spray can buy is
// 100 x N_dirs regardless of how many of those files survive. Typical script
// counts are single digits; 10k is purely a DoS guard.
static constexpr int kMaxScripts = 10'000;

/// Scan strategy backing the loader's `WatchedDirectorySet`. Forwards
/// the base's registered directory list verbatim. The list is kept up
/// to date by `scanAndRegister`, which calls `registerDirectories` with
/// the freshly-resolved XDG paths on every invocation — so watcher-
/// triggered rescans (file edits, parent-watch promotion) reuse the
/// snapshot from the last `scanAndRegister` and avoid redundantly
/// re-resolving XDG paths on every inotify wake.
class ScriptedAlgorithmLoader::LuauScanStrategy : public PhosphorFsLoader::IScanStrategy
{
public:
    explicit LuauScanStrategy(ScriptedAlgorithmLoader& loader)
        : m_loader(&loader)
    {
    }

    QStringList performScan(const QStringList& directoriesInScanOrder) override
    {
        return m_loader->performScan(directoriesInScanOrder);
    }

private:
    ScriptedAlgorithmLoader* m_loader;
};

ScriptedAlgorithmLoader::ScriptedAlgorithmLoader(const QString& subdirectory, ITileAlgorithmRegistry* registry,
                                                 QObject* parent)
    : QObject(parent)
    , m_subdirectory(subdirectory)
    , m_registry(registry)
    , m_watchdog(std::make_shared<PhosphorScripting::LuauWatchdog>())
    , m_strategy(std::make_unique<LuauScanStrategy>(*this))
    , m_watcher(std::make_unique<PhosphorFsLoader::WatchedDirectorySet>(*m_strategy, this))
{
    Q_ASSERT(m_registry);
    // The subdirectory is appended to XDG data roots (system + user),
    // so it must be relative and free of traversal segments. Validated
    // at runtime in BOTH debug and release: a malformed value disables
    // the loader (subdirectory cleared to empty, which is the documented
    // "loader disabled" sentinel) and logs a warning operators can grep
    // for. Empty subdirectory is explicitly supported and short-circuits
    // the validation.
    const auto malformedReason = [](const QString& sub) -> const char* {
        if (sub.isEmpty()) {
            return nullptr;
        }
        if (sub.startsWith(QLatin1Char('/'))) {
            return "absolute path";
        }
        for (const auto& segment : sub.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
            if (segment == QLatin1String("..")) {
                return "contains '..' traversal segment";
            }
        }
        return nullptr;
    }(m_subdirectory);
    if (malformedReason != nullptr) {
        qCWarning(PhosphorTiles::lcTilesLib).nospace()
            << "ScriptedAlgorithmLoader: subdirectory '" << m_subdirectory << "' rejected (" << malformedReason
            << ") — loader disabled. Pass a relative XDG path or the empty string.";
        m_subdirectory.clear();
    }

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
    // keep the watchdog alive until ~LuauTileAlgorithm finally fires;
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

    // `locateAll(GenericDataLocation, sub)` returns existing dirs in
    // descending priority — writable user-dir first (if it has the
    // subdirectory), then XDG_DATA_DIRS entries in spec order
    // (most-important first).
    QStringList rawDirs =
        QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, m_subdirectory, QStandardPaths::LocateDirectory);

    const QString userDir = userAlgorithmDir();

    // Build the result in the convention `performScan` expects:
    // **system-first (lowest XDG priority first), user-last**. After
    // `performScan` reverse-iterates this list (matching
    // `JsonScanStrategy`'s shape), the iteration order becomes
    // `[user, sys-highest, ..., sys-lowest]` — first-registration-wins
    // then yields user > sys-highest > sys-mid > sys-lowest, which
    // is the actual XDG semantic.
    //
    // The previous shape was `[user, sys-highest, ..., sys-lowest]`
    // followed by a plain `std::reverse` in `performScan`. That made
    // `sys-LOWEST` win against `sys-highest` for system-vs-system
    // collisions (a long-standing bug, predates this refactor).
    QStringList systemDirs;
    systemDirs.reserve(rawDirs.size());
    for (const QString& dir : std::as_const(rawDirs)) {
        if (!userDir.isEmpty() && QFileInfo(dir).canonicalFilePath() == QFileInfo(userDir).canonicalFilePath()) {
            continue; // skip user dir here — appended explicitly below
        }
        systemDirs.append(dir);
    }
    // System dirs are currently in descending-priority order; reverse so
    // the FIRST-iterated system dir (after performScan's reverse) is the
    // highest-priority one.
    std::reverse(systemDirs.begin(), systemDirs.end());

    QStringList ordered = systemDirs;
    if (!userDir.isEmpty()) {
        ordered.append(userDir);
    }

    // Deduplicate by canonical path — `locateAll` can return the same
    // physical directory twice via different XDG_DATA_DIRS entries or
    // symlinks; without dedup, scanAndRegister sees redundant
    // registrations and may mistakenly unregister valid algorithms as
    // "stale" during refresh.
    QSet<QString> seen;
    QStringList deduped;
    for (const QString& dir : std::as_const(ordered)) {
        const QString canonical = QFileInfo(dir).canonicalFilePath();
        // For not-yet-existing dirs (canonicalFilePath returns empty),
        // dedup by cleanPath instead — typically only the user dir,
        // and only when ensureUserDirectoryExists hasn't run yet.
        const QString key = canonical.isEmpty() ? QDir::cleanPath(dir) : canonical;
        if (key.isEmpty() || seen.contains(key)) {
            continue;
        }
        seen.insert(key);
        deduped.append(dir);
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

void ScriptedAlgorithmLoader::scanAndRegister(PhosphorFsLoader::LiveReload liveReload)
{
    // Disabled-loader short-circuit. An empty subdirectory is the
    // documented "loader disabled" sentinel, so check it BEFORE
    // ensureUserDirectoryExists — otherwise the unconditional
    // userAlgorithmDir() resolution inside the helper logs a misleading
    // "Cannot determine user data directory" warning every time a
    // disabled loader's owner pokes scanAndRegister.
    if (m_subdirectory.isEmpty()) {
        return;
    }

    // Lazily create user directory on first scan, then re-register the
    // (possibly newly-existing) directory set with the watcher so the
    // freshly-created user dir gets a direct watch instead of staying
    // proxied via parent-watch.
    ensureUserDirectoryExists();

    // Replace the watcher's registered set with the freshly-resolved
    // XDG paths. Using `setDirectories` (rather than the append-only
    // `registerDirectories`) handles the dir-set-shrinks case correctly:
    //
    //   • If a system XDG entry disappeared (package uninstall) or the
    //     user wiped ~/.local/share/<sub>, the missing dir is dropped
    //     from `m_directories` and its watches are torn down — the
    //     watcher won't keep firing rescans against the dead path.
    //   • If `dirs` collapses to empty entirely, the strategy still
    //     runs with an empty list, so any previously-registered scripts
    //     get unregistered as stale (no zombie registry entries).
    //   • If `dirs` is identical to the current set, the call is
    //     effectively a rescan with no churn (added/removed sets both
    //     empty, watches preserved).
    //
    // The strategy is invoked synchronously, which clears and rebuilds
    // m_scriptIdToPath and updates m_lastScriptSignature exactly once;
    // change-detection is exposed via the `algorithmsChanged` signal
    // rather than a return value (no in-tree caller consumed the bool).
    //
    // `algorithmDirectories()` already produces the canonical
    // `[sys-lowest, ..., sys-highest, user]` shape (it builds the list
    // explicitly via locateAll-reverse + user-append), so
    // `LowestPriorityFirst` is the literal-truth declaration.
    m_watcher->setDirectories(algorithmDirectories(), liveReload,
                              PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);
}

QStringList ScriptedAlgorithmLoader::performScan(const QStringList& directoriesInScanOrder)
{
    auto* registry = m_registry;
    // The constructor's Q_ASSERT(m_registry) is compiled out of a release
    // build, where this and loadFromDirectory both dereference the pointer
    // unguarded and would crash on the first valid script rather than assert.
    // The destructor already guards the same way, so match it: a null registry
    // means there is nothing to scan into.
    if (!registry) {
        qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithmLoader::performScan: no registry, skipping scan";
        return {};
    }

    // Track which script IDs we register in this scan
    QSet<QString> newScriptIds;

    // Save old tracking map so we can detect stale entries afterwards
    QHash<QString, QString> oldScriptIdToPath = m_scriptIdToPath;
    m_scriptIdToPath.clear();
    m_refusedFilePaths.clear();

    // Previous scan's per-file identities, consumed by loadFromDirectory to
    // decide whether a file can keep its existing registry entry instead of
    // being re-parsed into a fresh sandboxed VM. Moved out so this scan starts
    // from an empty map and only re-stamps files it actually kept.
    const QHash<QString, ScriptStamp> prevStamps = std::exchange(m_scriptStamps, {});

    // Resolve the user dir's canonical path ONCE. `loadFromDirectory`
    // takes the canonical form as a parameter so the per-iteration
    // duplicate-warning branch doesn't re-stat $HOME on every script
    // dir. Empty when the user dir doesn't exist yet — handled by the
    // truthiness guard at the use-site.
    const QString userDir = userAlgorithmDir();
    const QString canonicalUserDir = QFileInfo(userDir).canonicalFilePath();
    bool capTripped = false;
    // `algorithmDirectories()` returns dirs in [sys-lowest, ...,
    // sys-highest, user] order (system-first / user-last). Reverse-
    // iterate here so we visit [user, sys-highest, ..., sys-lowest] —
    // matches `JsonScanStrategy::performScan`'s shape and lets first-
    // registration-wins yield: user > sys-highest > sys-mid > ... >
    // sys-lowest, which is the actual XDG semantic. `crbegin/crend`
    // matches the strategy-convention used by `JsonScanStrategy` and
    // avoids the QStringList copy + std::reverse the previous shape needed.
    for (auto dirIt = directoriesInScanOrder.crbegin(); dirIt != directoriesInScanOrder.crend(); ++dirIt) {
        // Per-rescan script-count DoS guard. Reverse-iteration scans
        // user-first / system-last, so a cap-trip drops *system* overflow
        // rather than user overrides — matches the user-wins-on-cap-trip
        // property JsonScanStrategy / ShaderRegistry / AnimationShaderRegistry
        // enforce. Re-checked inside loadFromDirectory so a single dir
        // dropping us at the cap stops mid-iteration too.
        if (m_scriptIdToPath.size() >= kMaxScripts) {
            capTripped = true;
            break;
        }
        const QString& dir = *dirIt;
        // Use canonical paths to handle symlinks and relative path differences.
        // Empty canonical (dir doesn't exist yet) cannot match a non-empty
        // canonicalUserDir, and an empty canonicalUserDir cannot misclassify
        // an existing system dir as user.
        const QString canonicalDir = QFileInfo(dir).canonicalFilePath();
        const bool isUserDir = !canonicalUserDir.isEmpty() && canonicalDir == canonicalUserDir;
        loadFromDirectory(dir, isUserDir, canonicalUserDir, prevStamps);
    }

    if (capTripped) {
        qCWarning(PhosphorTiles::lcTilesLib).nospace()
            << "ScriptedAlgorithmLoader: reached script cap (" << kMaxScripts
            << ") — later scripts skipped to protect the GUI thread. Prune the watched algorithm directories or raise "
               "kMaxScripts.";
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

    // qCDebug here, qCInfo only on the changed branch below: this line runs on
    // every inotify wake, including the ones that changed nothing.
    qCDebug(PhosphorTiles::lcTilesLib) << "Scripted algorithms loaded:" << m_scriptIdToPath.size();

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
        qCInfo(PhosphorTiles::lcTilesLib) << "Scripted algorithms changed, now loaded:" << m_scriptIdToPath.size();
        Q_EMIT algorithmsChanged();
    }

    // Tell the base which individual files to install per-file watches
    // on. The base re-arms these every rescan, which closes the
    // atomic-rename inode-replacement window the legacy followup timer
    // used to cover.
    QStringList desiredFileWatches;
    desiredFileWatches.reserve(m_scriptIdToPath.size() + m_refusedFilePaths.size());
    for (auto it = m_scriptIdToPath.constBegin(); it != m_scriptIdToPath.constEnd(); ++it) {
        desiredFileWatches.append(it.value());
    }
    // Refused files (invalid script, duplicate id, built-in collision) stay
    // watched too. They own no registry entry, but an in-place write that
    // fixes the script (repaired source, changed id metadata) must trigger a
    // rescan just like an edit to a registered script — the directory watch
    // alone misses in-place inode writes.
    for (const QString& path : std::as_const(m_refusedFilePaths)) {
        desiredFileWatches.append(path);
    }
    return desiredFileWatches;
}

std::shared_ptr<PhosphorScripting::LuauEngine> ScriptedAlgorithmLoader::ensureSharedEngine()
{
    if (m_sharedEngine) {
        return m_sharedEngine;
    }
    // Latch failure: a VM that couldn't init/sandbox once won't succeed on a
    // later rescan, and retrying would re-log the same error on every inotify
    // wake. Bundled scripts fall back to their own VMs in that case.
    if (m_sharedEngineFailed) {
        return nullptr;
    }
    QString error;
    m_sharedEngine = LuauTileAlgorithm::createSandboxedEngine(m_watchdog, &error);
    if (!m_sharedEngine) {
        m_sharedEngineFailed = true;
        qCWarning(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithmLoader: shared VM setup failed (" << error
                                             << ") — bundled scripts will each use their own VM";
        return nullptr;
    }
    qCInfo(PhosphorTiles::lcTilesLib) << "ScriptedAlgorithmLoader: created shared VM for bundled algorithms";
    return m_sharedEngine;
}

void ScriptedAlgorithmLoader::loadFromDirectory(const QString& dir, bool isUserDir, const QString& canonicalUserDir,
                                                const QHash<QString, ScriptStamp>& prevStamps)
{
    const QStringList validFiles = validatedLuauFiles(dir, MaxWatchedFilesPerDir);

    for (const QString& fullPath : validFiles) {
        // Mid-directory bail when the global cap was reached during this
        // scan (e.g. a single user dir holding > kMaxScripts entries).
        // The outer loop in performScan logs the warning once at the
        // dir boundary; here we silently stop to keep log noise low.
        if (m_scriptIdToPath.size() >= kMaxScripts) {
            return;
        }
        // Two layered checks, intentionally not redundant:
        //   • `validatedLuauFiles` filters by `*.luau` extension AND verifies
        //     symlink containment within `dir`'s canonical tree (path-
        //     traversal defense).
        //   • The regex below restricts the BASENAME — the script id is
        //     derived from it (`script:<basename>`), and a hostile name
        //     like `; rm -rf /` would surface in logs / D-Bus / QML
        //     contexts that don't shell-escape.
        static const QRegularExpression validIdRe(QStringLiteral("^[a-zA-Z0-9_-]+$"));
        const QString baseName = QFileInfo(fullPath).completeBaseName();
        if (!validIdRe.match(baseName).hasMatch()) {
            qCWarning(PhosphorTiles::lcTilesLib) << "Skipping script with invalid filename:" << fullPath;
            continue;
        }
        // Create with nullptr parent, held by unique_ptr so every refusal below
        // can just `continue` and stay leak-free by construction. Ownership is
        // released to the registry on the one success path, which takes it over
        // via the deferred-delete shared_ptr inside its TileAlgorithmEntry
        // (deleteLater() while Qt is alive) in registerAlgorithm(). Pass the
        // watchdog as shared_ptr so the algorithm keeps it alive across
        // deferred-delete teardown (the algorithm dtor can run after the loader
        // is gone — see LuauTileAlgorithm.h for the contract).
        // Reuse the live registry entry when this exact file registered on a
        // previous scan, still has the same (size, mtime), and its id still
        // holds a live scripted entry. Rebuilding would re-run VM setup and a
        // source parse per script per inotify wake, on the GUI thread, for a
        // corpus the signature diff below is about to declare unchanged.
        //
        // The stale-shadow case is covered by construction: a file refused as a
        // duplicate never gets stamped, so when the script that shadowed it
        // disappears the survivor is rebuilt rather than resurrected. The
        // reverse (a new user script shadowing a stamped bundled one) is caught
        // by the first-wins guard below, which runs after the id is known and
        // refuses the bundled file whichever way its id was derived.
        // (size, mtime) is the whole key, so a content edit preserving both —
        // `cp -p`, `touch -r`, or an edit finer than the filesystem's mtime
        // granularity — keeps the OLD compiled algorithm live indefinitely.
        // Before reuse existed every rescan re-parsed the source, so that
        // window closed on the next inotify wake. Accepted: it is the same
        // (size, mtime) pair the change signature already trusts, so a stamp
        // that can fool this can equally fool the signature into reporting no
        // change at all.
        const ScriptStamp prev = prevStamps.value(fullPath);
        const QFileInfo scriptInfo(fullPath);
        const bool stampMatches = !prev.id.isEmpty() && prev.size == scriptInfo.size()
            && prev.mtimeMs == scriptInfo.lastModified().toMSecsSinceEpoch();
        bool reusedExisting = false;
        if (stampMatches) {
            const TilingAlgorithm* live = m_registry->algorithm(prev.id);
            reusedExisting = live != nullptr && live->isScripted();
        }

        // Trusted bundled scripts share one sandboxed VM (prelude + baseline
        // paid once); untrusted user scripts each get their own isolated,
        // per-engine-capped VM. If the shared VM can't be built, bundled
        // scripts fall back to their own VMs (correct, just less memory-frugal).
        std::unique_ptr<LuauTileAlgorithm> algo;
        if (!reusedExisting) {
            if (!isUserDir) {
                if (const std::shared_ptr<PhosphorScripting::LuauEngine> shared = ensureSharedEngine()) {
                    algo = std::make_unique<LuauTileAlgorithm>(fullPath, shared, m_watchdog, nullptr);
                }
            }
            if (!algo) {
                algo = std::make_unique<LuauTileAlgorithm>(fullPath, m_watchdog, nullptr);
            }
            if (!algo->isValid()) {
                qCWarning(PhosphorTiles::lcTilesLib) << "Invalid scripted algorithm, skipping:" << fullPath;
                m_refusedFilePaths.insert(fullPath);
                continue;
            }
        }

        // Use id metadata if present, otherwise default to "script:filename".
        // LuauTileAlgorithm builds the same id for itself, so the prefix is
        // shared rather than spelled twice. A reused entry keeps the id it
        // registered under, which is what the stamp recorded.
        const QString scriptId = reusedExisting
            ? prev.id
            : (algo->id().isEmpty() ? (AutotileDefaults::ScriptIdPrefix + baseName) : algo->id());

        // registerAlgorithm() handles replacement internally (removes old,
        // takes ownership of new) — no need to unregister first.
        if (!reusedExisting) {
            algo->setUserScript(isUserDir);
        }

        // First-registration-wins, scoped to THIS scan. `m_scriptIdToPath`
        // was cleared at the top of `performScan`, so a hit here means
        // an EARLIER `loadFromDirectory` call in the same scan already
        // registered this id. Combined with the dir order the strategy
        // feeds us — `[user, sys-highest, ..., sys-lowest]` after the
        // reverse in `performScan` — first-wins yields the correct XDG
        // semantic: user > sys-highest > sys-mid > ... > sys-lowest.
        //
        // The warning text differentiates the cases by inspecting the
        // EXISTING entry's directory (not the current iteration's
        // `isUserDir`), since the more interesting signal is "user
        // shadows a bundled script" — which now happens when the
        // current iteration is a *system* dir, not user.
        if (m_scriptIdToPath.contains(scriptId)) {
            const QString existingPath = m_scriptIdToPath.value(scriptId);
            const bool existingIsUser = !canonicalUserDir.isEmpty()
                && QFileInfo(existingPath).canonicalFilePath().startsWith(canonicalUserDir + QLatin1Char('/'));
            if (existingIsUser && !isUserDir) {
                qCInfo(PhosphorTiles::lcTilesLib).nospace()
                    << "User script overrides bundled algorithm: " << scriptId << " — kept '" << existingPath
                    << "', shadowed bundled '" << fullPath << "'";
            } else {
                qCWarning(PhosphorTiles::lcTilesLib).nospace()
                    << "Duplicate algorithm '" << scriptId << "' — kept '" << existingPath << "', skipped '" << fullPath
                    << "' (first registration wins)";
            }
            m_refusedFilePaths.insert(fullPath);
            continue;
        }

        // A script may shadow another script (XDG priority, enforced by the
        // first-wins guard above) but must NEVER shadow a C++ built-in:
        // registerAlgorithm() replaces on duplicate id, and the stale sweep
        // in scanAndRegister would later unregister the id outright when the
        // script file disappears — leaving the built-in gone until restart.
        // Probe the live registry: a non-scripted entry under this id is a
        // built-in, so refuse the file. The refusal must keep the id OUT of
        // m_scriptIdToPath so neither the stale sweep nor the file-watch
        // bookkeeping ever sees it.
        if (const TilingAlgorithm* existing = m_registry->algorithm(scriptId); existing && !existing->isScripted()) {
            qCWarning(PhosphorTiles::lcTilesLib).nospace()
                << "Script '" << fullPath << "' declares id '" << scriptId
                << "' which collides with a built-in algorithm — skipped (scripts cannot replace built-ins)";
            m_refusedFilePaths.insert(fullPath);
            continue;
        }

        if (!reusedExisting) {
            m_registry->registerAlgorithm(scriptId, algo.release());
        }
        m_scriptIdToPath[scriptId] = fullPath;
        m_scriptStamps.insert(fullPath,
                              ScriptStamp{scriptId, scriptInfo.size(), scriptInfo.lastModified().toMSecsSinceEpoch()});

        // qCDebug, not qCInfo: this fires once per script on every rescan,
        // including the ones the signature diff proves changed nothing, so at
        // Info it turns every inotify wake into journal noise.
        qCDebug(PhosphorTiles::lcTilesLib)
            << (reusedExisting ? "Kept scripted algorithm:" : "Registered scripted algorithm:") << scriptId
            << "from=" << fullPath << "user=" << isUserDir;
    }
}

QStringList ScriptedAlgorithmLoader::validatedLuauFiles(const QString& dirPath, int maxFiles) const
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
    // `QDir::Name` explicitly, matching JsonScanStrategy and PluginLoader. The
    // default sort is Name|IgnoreCase, and the ordering stopped being cosmetic
    // the moment the cap below started counting files CONSIDERED: in a
    // directory over the cap, sort order decides which files survive.
    const QStringList files = dirObj.entryList({QStringLiteral("*.luau")}, QDir::Files | QDir::Readable, QDir::Name);
    // Count files CONSIDERED, not files kept. Counting survivors would let a
    // directory sprayed with dangling or escaping `*.luau` symlinks spend one
    // `canonicalFilePath()` realpath syscall per entry on the GUI thread while
    // never advancing the counter, so the cap would never trip. Same quantity
    // JsonScanStrategy and MetadataPackScanStrategy count, for the same reason.
    int filesConsidered = 0;
    for (const QString& file : files) {
        if (filesConsidered >= maxFiles) {
            qCWarning(PhosphorTiles::lcTilesLib) << "Reached max file limit (" << maxFiles
                                                 << ") for directory:" << dirPath << "— skipping remaining files";
            break;
        }
        ++filesConsidered;
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
