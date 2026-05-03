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
    /// signal (`profilesChanged`) fires. Reset to false at the start of
    /// every `commitBatch`; flipped to true on any tracked-state change.
    bool lastBatchChanged = false;

    const QString& ownerTag() const
    {
        return m_ownerTag;
    }

    std::optional<PhosphorFsLoader::ParsedEntry> parseFile(const QString& filePath) override
    {
        // Common envelope checks (read, parse, root-is-object,
        // non-empty `name`, name-matches-filename) live in the shared
        // helper. The helper strips `name` from the returned root, so
        // it can't leak into Profile::presetName — the two concepts
        // are distinct (registry path vs. user-assigned preset label).
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
        lastBatchChanged = false;

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

        // Single reloadFromOwner -> one profilesReloaded signal regardless
        // of how many files changed (decision W: coalesce). The partitioning
        // ensures daemon-direct entries at other paths survive this rescan.
        Q_ASSERT(registry);
        registry->reloadFromOwner(m_ownerTag, currentMap);
    }

private:
    QString m_ownerTag;

    /// Snapshot of the last-committed payload per key, used for diffing.
    QHash<QString, Profile> m_lastCommittedPayloads;

    /// Snapshot of (sourcePath, systemSourcePath) per key, used for
    /// the source-metadata diff (Phase 1c+1d fix).
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
    // Clean up any registry entries we own so a process hosting multiple
    // sequential loaders (tests, especially) doesn't accumulate ghosts
    // from destroyed loaders.
    if (m_sink && m_sink->registry) {
        m_sink->registry->clearOwner(m_sink->ownerTag());
    }
}

int ProfileLoader::loadFromDirectory(const QString& directory, LiveReload liveReload)
{
    return m_loader->loadFromDirectory(directory, liveReload);
}

int ProfileLoader::loadFromDirectories(const QStringList& directories, LiveReload liveReload,
                                       PhosphorFsLoader::RegistrationOrder order)
{
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
    return m_sink ? m_sink->ownerTag() : QString();
}

void ProfileLoader::requestRescan()
{
    m_loader->requestRescan();
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

bool ProfileLoader::hasPath(const QString& path) const
{
    return m_sink->entries.contains(path);
}

} // namespace PhosphorAnimation
