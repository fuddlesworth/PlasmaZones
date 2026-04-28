// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/IScanStrategy.h>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLoggingCategory>

#include <algorithm>
#include <utility>

namespace PhosphorFsLoader {

namespace {
Q_LOGGING_CATEGORY(lcLoader, "phosphorfsloader.directoryloader")
} // namespace

/**
 * @brief JSON-specific scan strategy backing every `DirectoryLoader`.
 *
 * Owns the per-rescan state (tracked entries, sink dispatch, the
 * configurable entry cap) so the loader's public class stays tiny.
 * Defined in the source file rather than the header because no
 * consumer needs the symbol — the loader's public surface is only the
 * sink contract.
 */
class DirectoryLoader::JsonScanStrategy : public IScanStrategy
{
public:
    explicit JsonScanStrategy(IDirectoryLoaderSink& sink)
        : m_sink(&sink)
    {
    }

    QStringList performScan(const QStringList& directoriesInScanOrder) override;

    int registeredCount() const
    {
        return m_entries.size();
    }

    QList<DirectoryLoader::Entry> entries() const
    {
        QList<DirectoryLoader::Entry> sorted = m_entries.values();
        std::sort(sorted.begin(), sorted.end(), [](const DirectoryLoader::Entry& a, const DirectoryLoader::Entry& b) {
            return a.key < b.key;
        });
        return sorted;
    }

    void setMaxEntries(int cap)
    {
        m_maxEntries = cap;
    }

private:
    IDirectoryLoaderSink* m_sink = nullptr;
    QHash<QString, DirectoryLoader::Entry> m_entries; ///< key → tracked entry
    int m_maxEntries = DirectoryLoader::kMaxEntries;
};

QStringList DirectoryLoader::JsonScanStrategy::performScan(const QStringList& directoriesInScanOrder)
{
    // Build the fresh set in REVERSE scan order — first-wins semantics.
    //
    // Callers register dirs system-first / user-last (per the public
    // `loadFromDirectories` docstring); iterating in reverse lets the
    // user dir claim its keys BEFORE the system dir is touched. A
    // subsequent system file with a colliding key updates the
    // already-claimed entry's `systemSourcePath` (records the shadowed
    // file for restore-on-delete introspection) but does not overwrite
    // the entry — so user-wins is preserved by construction.
    //
    // This matters for the entry-count cap: under the previous forward-
    // iteration + overwrite model, a hostile-or-buggy system dir
    // containing more than `m_maxEntries` files would trip the cap
    // BEFORE the user dir was scanned, silently dropping every user
    // override. Reverse-iteration drops *system* entries on cap-trip,
    // never user overrides — assuming the user dir alone fits within
    // the cap. If the user dir contains more than `m_maxEntries`
    // files the cap trips during the user pass and system overrides
    // are then never scanned at all (the warning below makes the
    // pruning-or-bumping decision explicit). User-wins layering is
    // never violated either way.
    QHash<QString, Entry> fresh;
    QHash<QString, ParsedEntry> freshParsedByKey;

    bool capTripped = false;

    for (auto dirIt = directoriesInScanOrder.crbegin(); dirIt != directoriesInScanOrder.crend(); ++dirIt) {
        const QString& directory = *dirIt;
        if (capTripped) {
            break;
        }
        QDir dir(directory);
        if (!dir.exists()) {
            qCDebug(lcLoader) << "rescan: directory does not exist (yet?)" << directory;
            continue;
        }
        // Sort within each directory so duplicate-name resolution is
        // deterministic across platforms (ext4/btrfs/APFS differ on
        // entryList ordering).
        QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);

        // Track keys already seen within THIS directory so we can warn
        // on intra-directory collisions (across directories is legitimate
        // user-wins-over-system layering, which we handle below).
        // Map value is the absolute path of the first file claiming the
        // key so the warning can name both the winning file and the
        // ignored file.
        //
        // INTRA-directory only: lookup key is case-folded so
        // rsync-from-macOS-APFS (case-insensitive) onto ext4
        // (case-sensitive) — where `Curve.json` and `curve.json`
        // coexist on ext4 but collide on APFS — warns consistently
        // on both platforms. The ORIGINAL case is still used for the
        // registered entry key (sinks may care about case for display
        // purposes) — only the collision check itself is case-folded.
        //
        // Cross-directory layering uses the raw key, so user
        // `Curve.json` (key="Foo") in one dir and system `curve.json`
        // (key="foo") in another resolve to two distinct registry
        // entries. That asymmetry is deliberate: case-folding
        // cross-dir would risk silently dropping legitimate user
        // overrides whose keys happen to differ only in case from a
        // system entry, and the only realistic way to hit a mixed-
        // sensitivity collision is rsync APFS → ext4 (already
        // round-tripped via the intra-dir warning above).
        QHash<QString, QString> keysInThisDir;

        for (const QString& file : files) {
            // Entry-count DoS guard — paired with the per-file byte cap
            // below. A directory sprayed with tens of thousands of empty
            // `*.json` files would otherwise parse every one of them on
            // the GUI thread on every watcher fire.
            if (freshParsedByKey.size() >= m_maxEntries) {
                capTripped = true;
                break;
            }

            const QString fullPath = dir.absoluteFilePath(file);

            // DoS / foot-gun guard: untrusted same-user files should not
            // be able to stall the GUI thread with a 2 GB blob. Stat
            // first; skip + warn on oversize. Sinks that want a lower
            // cap enforce their own on top of this.
            const QFileInfo fileInfo(fullPath);
            if (fileInfo.size() > DirectoryLoader::kMaxFileBytes) {
                qCWarning(lcLoader) << "Skipping oversized file" << fullPath << "(" << fileInfo.size() << "bytes, cap"
                                    << DirectoryLoader::kMaxFileBytes << ")";
                continue;
            }

            auto parsed = m_sink->parseFile(fullPath);
            if (!parsed) {
                continue;
            }
            const QString key = parsed->key;
            if (key.isEmpty()) {
                qCWarning(lcLoader) << "parseFile returned entry with empty key from" << fullPath
                                    << "— sinks must set ParsedEntry::key";
                continue;
            }

            // Intra-directory duplicate: two files in the SAME dir with
            // the same key (case-folded). Filesystem enumeration is
            // alphabetic (we sort above), so the first-seen file wins
            // deterministically.
            const QString foldedKey = key.toLower();
            if (auto winnerIt = keysInThisDir.constFind(foldedKey); winnerIt != keysInThisDir.constEnd()) {
                qCWarning(lcLoader).nospace()
                    << "Duplicate key '" << key << "' within directory " << directory << " — kept '" << winnerIt.value()
                    << "', ignored '" << fullPath << "' (winner is alphabetically first)";
                continue;
            }
            keysInThisDir.insert(foldedKey, fullPath);

            // Cross-directory: first-wins (we iterate user-first via
            // the reverse loop above). If this key was already claimed
            // by a higher-priority dir, the currently-scanned (lower-
            // priority) file is shadowed — record its path on the
            // already-tracked entry AND mirror onto the matching
            // ParsedEntry so the sink sees it in commitBatch without a
            // separate propagation pass. The shadowed entry is NOT
            // registered with the sink; only the highest-priority claim is.
            if (auto existing = fresh.find(key); existing != fresh.end()) {
                if (existing->systemSourcePath.isEmpty()) {
                    existing->systemSourcePath = parsed->sourcePath;
                    auto pIt = freshParsedByKey.find(key);
                    if (pIt != freshParsedByKey.end()) {
                        pIt->systemSourcePath = parsed->sourcePath;
                    }
                }
                continue;
            }

            Entry trackedEntry;
            trackedEntry.key = key;
            trackedEntry.sourcePath = parsed->sourcePath;
            trackedEntry.systemSourcePath = parsed->systemSourcePath;
            fresh.insert(key, trackedEntry);

            freshParsedByKey.insert(key, std::move(*parsed));
        }
    }

    if (capTripped) {
        // System dirs may not have been fully scanned (cap trips on
        // count, not on dir boundary). For surviving entries that
        // previously recorded a system shadow, carry the prior
        // `systemSourcePath` forward instead of clearing it — clearing
        // would surface in `BatchedSink` as a metadata-only diff and
        // cause the consumer signal to fan out spuriously every time
        // the cap trips with no actual content change. The next un-
        // tripped scan re-derives the path correctly; until then, the
        // last successfully observed value is the best available
        // estimate.
        for (auto it = fresh.begin(); it != fresh.end(); ++it) {
            if (!it->systemSourcePath.isEmpty()) {
                continue; // shadowed in this scan; current value is authoritative
            }
            const auto prior = m_entries.constFind(it.key());
            if (prior == m_entries.constEnd() || prior->systemSourcePath.isEmpty()) {
                continue; // never had a shadow recorded
            }
            it->systemSourcePath = prior->systemSourcePath;
            auto pIt = freshParsedByKey.find(it.key());
            if (pIt != freshParsedByKey.end()) {
                pIt->systemSourcePath = prior->systemSourcePath;
            }
        }
        qCWarning(lcLoader).nospace()
            << "DirectoryLoader: reached entry cap (" << m_maxEntries
            << ") — later files skipped to protect the GUI thread. Raise kMaxEntries or prune the watched directories.";
    }

    // Materialise the final parsed-entry list from the hash. Sorting
    // by key gives the sink a stable iteration order across platforms
    // and Qt versions (QHash iteration order is randomised in Qt6).
    QList<ParsedEntry> freshParsed;
    freshParsed.reserve(freshParsedByKey.size());
    for (auto it = freshParsedByKey.cbegin(); it != freshParsedByKey.cend(); ++it) {
        freshParsed.append(it.value());
    }
    std::sort(freshParsed.begin(), freshParsed.end(), [](const ParsedEntry& a, const ParsedEntry& b) {
        return a.key < b.key;
    });

    // Diff against the previous tracked set — anything that was
    // registered before but is no longer on disk is a removal.
    QStringList removedKeys;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        if (!fresh.contains(it.key())) {
            removedKeys.append(it.key());
        }
    }

    m_entries = std::move(fresh);

    // Hand the batch to the sink — single call, so the sink can emit
    // one bulk-reload signal on its registry instead of N per-key
    // signals.
    m_sink->commitBatch(removedKeys, freshParsed);

    // Tell the base which paths to install per-file watches on.
    QStringList desiredFileWatches;
    desiredFileWatches.reserve(m_entries.size());
    for (const auto& entry : std::as_const(m_entries)) {
        desiredFileWatches.append(entry.sourcePath);
    }
    return desiredFileWatches;
}

// ─── DirectoryLoader ────────────────────────────────────────────────────────

DirectoryLoader::DirectoryLoader(IDirectoryLoaderSink& sink, QObject* parent)
    : QObject(parent)
    , m_strategy(std::make_unique<JsonScanStrategy>(sink))
    , m_watcher(std::make_unique<WatchedDirectorySet>(*m_strategy, this))
{
    // Forward the base's rescan signal to the loader's public
    // `entriesChanged` so existing consumers keep working without changes.
    connect(m_watcher.get(), &WatchedDirectorySet::rescanCompleted, this, &DirectoryLoader::entriesChanged);
}

DirectoryLoader::~DirectoryLoader() = default;

int DirectoryLoader::loadFromDirectory(const QString& directory, LiveReload liveReload)
{
    return loadFromDirectories(QStringList{directory}, liveReload);
}

int DirectoryLoader::loadFromDirectories(const QStringList& directories, LiveReload liveReload)
{
    m_watcher->registerDirectories(directories, liveReload);
    return m_strategy->registeredCount();
}

void DirectoryLoader::requestRescan()
{
    m_watcher->requestRescan();
}

int DirectoryLoader::registeredCount() const
{
    return m_strategy->registeredCount();
}

QList<DirectoryLoader::Entry> DirectoryLoader::entries() const
{
    return m_strategy->entries();
}

void DirectoryLoader::setDebounceIntervalForTest(int ms)
{
    m_watcher->setDebounceIntervalForTest(ms);
}

void DirectoryLoader::setMaxEntriesForTest(int cap)
{
    m_strategy->setMaxEntries(cap);
}

QString DirectoryLoader::watchedAncestorForTest(const QString& target) const
{
    return m_watcher->watchedAncestorForTest(target);
}

bool DirectoryLoader::hasParentWatchForTest(const QString& path) const
{
    return m_watcher->hasParentWatchForTest(path);
}

} // namespace PhosphorFsLoader
