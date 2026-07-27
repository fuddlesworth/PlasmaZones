// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/ProfileLoader.h>

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/IDirectoryLoaderSink.h>
#include <PhosphorFsLoader/JsonEnvelopeValidator.h>
#include <PhosphorFsLoader/ParsedEntry.h>

#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPair>
#include <QScopeGuard>
#include <QUuid>

#include <algorithm>
#include <any>
#include <utility>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcProfileLoader, "phosphoranimation.profileloader")
} // namespace

class ProfileLoader::Sink : public PhosphorFsLoader::IDirectoryLoaderSink
{
public:
    Sink(PhosphorProfileRegistry& reg, CurveRegistry& curveReg, QString owner)
        : registry(&reg)
        , curveRegistry(&curveReg)
        , m_ownerTag(std::move(owner))
    {
    }

    PhosphorProfileRegistry* registry; ///< pinned at ctor
    CurveRegistry* curveRegistry; ///< pinned at ctor — used by parseFile
    QHash<QString, ProfileLoader::Entry> entries; ///< path -> entry

    /// Parent-loader-visible flag — read by the lambda bound to
    /// `DirectoryLoader::entriesChanged` to decide whether the consumer
    /// signal (`profilesChanged`) fires. Flipped to true on any
    /// tracked-state change, and cleared only when the OUTERMOST
    /// `commitBatch` starts.
    ///
    /// Depth-scoped rather than unconditionally cleared: `commitBatch`
    /// ends in `reloadFromOwner`, which emits `profileChanged` on the
    /// caller's stack, and `ProfileLoader::rescanNow()` is public, so a
    /// consumer can re-enter a scan from such a handler. An
    /// unconditional clear at the top of the nested batch would discard
    /// the outer batch's evidence, and the outer `entriesChanged` would
    /// then silently suppress `profilesChanged` for a real change.
    ///
    /// The accepted cost is a duplicate: in the nested case the inner
    /// rescan's `entriesChanged` reads the flag while it still carries the
    /// OUTER batch's evidence, so `profilesChanged` fires for both. A
    /// redundant consumer refresh is the right side to err on — the
    /// alternative is a missed one.
    bool lastBatchChanged = false;

    /// Reentry depth for `commitBatch`. See `lastBatchChanged`.
    int commitDepth = 0;

    const QString& ownerTag() const
    {
        return m_ownerTag;
    }

    std::optional<PhosphorFsLoader::ParsedEntry> parseFile(const QString& filePath) override
    {
        // Common envelope checks (size cap, read, parse, root-is-object,
        // non-empty `name`, name-matches-filename) live in the shared
        // helper, which also strips `name` from the returned root: it is
        // that layer's routing key, not part of this schema. The registry
        // path and `Profile::presetName` are distinct concepts and stay
        // distinct — `Profile::fromJson` reads only `presetName`.
        auto envelope = PhosphorFsLoader::validateJsonEnvelope(filePath, lcProfileLoader());
        if (!envelope) {
            return std::nullopt;
        }

        // Profile::fromJson reads the remaining fields (curve / duration /
        // minDistance / sequenceMode / staggerInterval / presetName).
        const Profile profile = Profile::fromJson(envelope->root, *curveRegistry);

        PhosphorFsLoader::ParsedEntry parsed;
        parsed.key = envelope->name;
        parsed.sourcePath = filePath;
        parsed.payload = profile;
        return parsed;
    }

    void commitBatch(const QStringList& removedKeys,
                     const QList<PhosphorFsLoader::ParsedEntry>& currentEntries) override
    {
        // Only the outermost batch starts from a clean slate — a nested one
        // (reached through `reloadFromOwner`'s synchronous `profileChanged`
        // into a consumer that calls `rescanNow`) accumulates into the
        // evidence the outer batch has already gathered.
        if (commitDepth == 0) {
            lastBatchChanged = false;
        }
        ++commitDepth;
        const auto leaveBatch = qScopeGuard([this] {
            --commitDepth;
        });

        // Walk removals first so a re-add of the same key on the same
        // pass sees a clean snapshot and registers fresh.
        for (const QString& key : removedKeys) {
            const bool hadPayload = m_lastCommittedPayloads.remove(key) > 0;
            const bool hadSources = m_lastCommittedSources.remove(key) > 0;
            if (hadPayload || hadSources) {
                lastBatchChanged = true;
            }
            entries.remove(key);
        }

        // Build the post-rescan profile map for the bulk registry call,
        // diffing each entry against the snapshot to set lastBatchChanged.
        QHash<QString, Profile> currentMap;
        currentMap.reserve(currentEntries.size());

        for (const auto& parsed : currentEntries) {
            const auto* payload = std::any_cast<Profile>(&parsed.payload);
            if (!payload) {
                qCWarning(lcProfileLoader) << "commitBatch: payload type-mismatch for" << parsed.key;
                // Both snapshots must forget this key, and the batch must report
                // as changed. Skipping bare leaves the key out of `currentMap` —
                // so `reloadFromOwner` unregisters it — while this sink still
                // claims to track it at its old value. A later successful parse
                // of the same key with the same value would then diff EQUAL and
                // suppress `profilesChanged` a second time, even though the
                // registry entry disappeared and came back. Unreachable today
                // (parseFile always stores a Profile), but this is the only
                // partial-failure path here and it must fail coherently.
                m_lastCommittedPayloads.remove(parsed.key);
                m_lastCommittedSources.remove(parsed.key);
                // `entries` too, or the coherence claim above is not met: the key is
                // absent from currentMap so reloadFromOwner unregisters it, and a
                // stale Entry left here would keep `ProfileLoader::entries()`
                // advertising a path the registry no longer serves.
                entries.remove(parsed.key);
                lastBatchChanged = true;
                continue;
            }

            // Payload diff: Profile::operator== value comparison.
            const auto snapshotIt = m_lastCommittedPayloads.constFind(parsed.key);
            const bool payloadChanged = snapshotIt == m_lastCommittedPayloads.constEnd() || !(*snapshotIt == *payload);

            // Source-metadata diff: when the user copy is deleted and the
            // system file re-emerges with byte-identical content, the
            // payload stays equal but source paths shift — the consumer
            // signal must still fire so settings UIs update.
            const QPair<QString, QString> currentSources{parsed.sourcePath, parsed.systemSourcePath};
            const auto sourcesIt = m_lastCommittedSources.constFind(parsed.key);
            const bool sourcesChanged = sourcesIt == m_lastCommittedSources.constEnd() || *sourcesIt != currentSources;

            if (payloadChanged) {
                m_lastCommittedPayloads.insert(parsed.key, *payload);
                lastBatchChanged = true;
            }
            if (sourcesChanged) {
                m_lastCommittedSources.insert(parsed.key, currentSources);
                lastBatchChanged = true;
            }

            // Mirror the parsed entry into the tracked entries map.
            ProfileLoader::Entry e;
            e.path = parsed.key;
            e.sourcePath = parsed.sourcePath;
            e.systemSourcePath = parsed.systemSourcePath;
            entries.insert(parsed.key, std::move(e));

            currentMap.insert(parsed.key, *payload);
        }

        // Single reloadFromOwner -> AT MOST one `ownerReloaded` for the whole
        // batch however many files changed (decision W: coalesce), alongside a
        // per-path `profileChanged` for each entry that actually moved. None
        // of either when the registry's own diff comes out empty, i.e. when
        // the batch neither adds nor removes anything this loader owns.
        // That diff is independent of this sink's lastBatchChanged,
        // so the two signal families do not imply one another in either
        // direction. (`profilesReloaded` is a third signal, fired only by the
        // registry's wholesale reloadAll / clear.) The partitioning ensures
        // daemon-direct entries at other paths survive this rescan.
        // `registry` is bound from a reference in the ctor, so it is never
        // null and needs no guard.
        registry->reloadFromOwner(m_ownerTag, currentMap);
    }

private:
    QString m_ownerTag;

    /// Snapshot of the last-committed payload per key, used for diffing.
    QHash<QString, Profile> m_lastCommittedPayloads;

    /// Snapshot of (sourcePath, systemSourcePath) per key, used for
    /// the source-metadata diff.
    QHash<QString, QPair<QString, QString>> m_lastCommittedSources;
};

namespace {
/// Generate a unique-per-instance owner tag when the caller didn't
/// specify one. Uses a fresh UUID rather than the object's `this`
/// pointer — address reuse across sequential loader construction
/// (RAII-scoped loaders in unit tests, plugin reloads that tear down
/// and rebuild a loader at the same heap slot) would otherwise let the
/// new loader briefly inherit authority over the prior loader's
/// unclaimed partition entries before its first commitBatch runs.
/// UUIDs are never reused within a process.
QString defaultOwnerTag()
{
    return QStringLiteral("profileloader-") + QUuid::createUuid().toString();
}
} // namespace

ProfileLoader::ProfileLoader(PhosphorProfileRegistry& registry, CurveRegistry& curveRegistry, const QString& ownerTag,
                             QObject* parent)
    : QObject(parent)
    , m_sink(std::make_unique<Sink>(registry, curveRegistry, ownerTag.isEmpty() ? defaultOwnerTag() : ownerTag))
    , m_loader(std::make_unique<PhosphorFsLoader::DirectoryLoader>(*m_sink))
{
    // Gate `profilesChanged` on the per-batch change flag — same
    // contract as CurveLoader::curvesChanged. DirectoryLoader emits
    // entriesChanged on every rescan, but ProfileLoader consumers only
    // care when the tracked set or a Profile's value actually changed.
    connect(m_loader.get(), &PhosphorFsLoader::DirectoryLoader::entriesChanged, this, [this]() {
        if (m_sink->lastBatchChanged) {
            Q_EMIT profilesChanged();
        }
    });
}

ProfileLoader::~ProfileLoader()
{
    // Sever the entriesChanged wire FIRST: clearOwner below emits the
    // registry's profileChanged synchronously, and a consumer slot that
    // reacted by calling rescanNow() would re-enter commitBatch →
    // reloadFromOwner on a half-destroyed loader and RE-REGISTER the very
    // entries this destructor exists to remove (the test suite defends its
    // own slots against this with a scope guard; production consumers get
    // the guarantee here instead).
    disconnect(m_loader.get(), nullptr, this, nullptr);
    m_destroying = true;
    // Clean up any registry entries we own so a process hosting multiple
    // sequential loaders (tests, especially) doesn't accumulate ghosts
    // from destroyed loaders.
    m_sink->registry->clearOwner(m_sink->ownerTag());
}

int ProfileLoader::loadFromDirectory(const QString& directory, LiveReload liveReload)
{
    // Same teardown guard as rescanNow(): a consumer slot reacting to the
    // destructor's clearOwner emission must not be able to re-register entries
    // through any load entry point either, not just the rescan pair.
    if (m_destroying) {
        return 0;
    }
    return m_loader->loadFromDirectory(directory, liveReload);
}

int ProfileLoader::loadFromDirectories(const QStringList& directories, LiveReload liveReload,
                                       PhosphorFsLoader::RegistrationOrder order)
{
    if (m_destroying) {
        return 0;
    }
    return m_loader->loadFromDirectories(directories, liveReload, order);
}

int ProfileLoader::loadLibraryBuiltins(LiveReload liveReload)
{
    // Use the install-prefix directory baked in at build time via
    // PHOSPHORANIMATION_INSTALL_DATADIR. Namespacing under the library's
    // own `phosphor-animation/profiles` subdir means a consumer's
    // user-local `~/.local/share/<consumer>/profiles` pack is NEVER
    // accidentally pulled into the library's built-in load — the old
    // XDG-based `locateAll(GenericDataLocation, ...)` had the reverse
    // property where a user placing files under
    // `~/.local/share/phosphor-animation/profiles` would silently
    // shadow the library's immutable pack.
    //
    // When the macro is absent (sub-project builds that did not
    // propagate the datadir), fall back to a no-op — the caller's
    // consumer-namespaced directories are still loaded via the
    // `loadFromDirectory[ies]` entry points.
    if (m_destroying) {
        return 0;
    }
#ifdef PHOSPHORANIMATION_INSTALL_DATADIR
    const QString dir = QStringLiteral(PHOSPHORANIMATION_INSTALL_DATADIR "/profiles");
    if (!QDir(dir).exists()) {
        return 0;
    }
    return loadFromDirectory(dir, liveReload);
#else
    Q_UNUSED(liveReload);
    return 0;
#endif
}

QString ProfileLoader::ownerTag() const
{
    // No null guard: m_sink is set in the ctor init-list, never reset, and the
    // ctor itself already dereferences it. Same invariant as Sink::registry.
    return m_sink->ownerTag();
}

void ProfileLoader::requestRescan()
{
    if (m_destroying) {
        return;
    }
    m_loader->requestRescan();
}

void ProfileLoader::rescanNow()
{
    // Guarded (like requestRescan) so a call re-entering during the
    // destructor's clearOwner emission cannot re-register the entries the
    // teardown just removed.
    if (m_destroying) {
        return;
    }
    m_loader->rescanNow();
}

int ProfileLoader::registeredCount() const
{
    return m_loader->registeredCount();
}

QList<ProfileLoader::Entry> ProfileLoader::entries() const
{
    // Sort by path for deterministic ordering — same rationale as
    // DirectoryLoader::entries(). QHash iteration order is randomised
    // in Qt6.
    QList<ProfileLoader::Entry> sorted = m_sink->entries.values();
    std::sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
        return a.path < b.path;
    });
    return sorted;
}

} // namespace PhosphorAnimation
