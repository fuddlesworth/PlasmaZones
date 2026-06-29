// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveLoader.h>

#include <PhosphorAnimation/CurveRegistry.h>

#include <PhosphorFsLoader/DirectoryLoader.h>
#include <PhosphorFsLoader/IDirectoryLoaderSink.h>
#include <PhosphorFsLoader/JsonEnvelopeValidator.h>
#include <PhosphorFsLoader/ParsedEntry.h>
#include <PhosphorFsLoader/SchemaValidator.h>

#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QLatin1String>
#include <QLoggingCategory>
#include <QPair>
#include <QUuid>

#include <algorithm>
#include <any>
#include <utility>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcCurveLoader, "phosphoranimation.curveloader")

/// Generate a unique-per-instance owner tag. UUIDs are never reused
/// within a process, so sequential loaders (tests, plugin reloads,
/// embedded shells) never inherit authority over a prior loader's
/// unclaimed registry entries — even when a new loader reuses the
/// same heap slot. Prefixed `"curveloader-"` for debug clarity (matches
/// the `"profileloader-"` separator used by `ProfileLoader`; a colon
/// is fragile in serialised contexts like log filters and dbus paths).
QString makeCurveLoaderOwnerTag()
{
    return QStringLiteral("curveloader-") + QUuid::createUuid().toString();
}

// Process-wide curve schema validator, compiled once from the RCC-embedded
// schema (see qt6_add_resources in CMakeLists). The schema deliberately does
// not require "name" — the envelope check has already enforced and stripped it
// — so it validates the envelope-stripped root the same way CI validates the
// full on-disk file.
const PhosphorFsLoader::SchemaValidator& curveSchemaValidator()
{
    static const PhosphorFsLoader::SchemaValidator validator = PhosphorFsLoader::SchemaValidator::fromResource(
        QStringLiteral(":/phosphoranimation/schemas/curve.schema.json"), lcCurveLoader());
    return validator;
}

} // namespace

namespace internal {
struct CurvePayload
{
    QString displayName;
    std::shared_ptr<const Curve> curve;
    QString wireFormat;
};
} // namespace internal

/**
 * The curve-specific sink — knows the curve JSON schema, knows how to
 * turn it into a `shared_ptr<const Curve>`, and knows how to register /
 * unregister entries against `CurveRegistry`. The directory walk,
 * watching, and debounce live in `PhosphorFsLoader::DirectoryLoader`;
 * this class implements IDirectoryLoaderSink directly with inline
 * diff-and-commit logic.
 */
class CurveLoader::Sink : public PhosphorFsLoader::IDirectoryLoaderSink
{
public:
    Sink(CurveRegistry& reg, QString owner)
        : registry(&reg)
        , m_ownerTag(std::move(owner))
    {
    }

    CurveRegistry* registry; ///< pinned at ctor — no per-call mutation
    QHash<QString, CurveLoader::Entry> entries; ///< name -> entry (for entries() introspection)

    /// Parent-loader-visible flag — read by the lambda bound to
    /// `DirectoryLoader::entriesChanged` to decide whether the consumer
    /// signal (`curvesChanged`) fires. Reset to false at the start of
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
        // helper. On success the helper hands back the parsed root with
        // `name` removed, so the schema-specific code below only deals
        // with curve-typed fields.
        auto envelope = PhosphorFsLoader::validateJsonEnvelope(filePath, lcCurveLoader());
        if (!envelope) {
            return std::nullopt;
        }
        const QJsonObject& obj = envelope->root;
        const QString& name = envelope->name;

        // Structural schema gate: typeId present and non-empty, parameters a
        // numeric object. Catches malformed curve files up front; the registry
        // factory below still does the typeId-specific parameter validation.
        if (const auto errors = curveSchemaValidator().validate(obj)) {
            qCWarning(lcCurveLoader) << "Skipping curve file failing schema validation:" << filePath;
            PhosphorFsLoader::logSchemaErrors(lcCurveLoader(), *errors);
            return std::nullopt;
        }

        // Reject user-authored files whose `name` collides with a
        // built-in typeId. CurveRegistry::registerFactory is
        // replace-semantic, so letting one through would permanently
        // wipe the built-in factory (e.g. `spring`) from the registry
        // until process restart. The user picked the name explicitly,
        // so surface the conflict instead of silently prefixing.
        if (CurveRegistry::isBuiltinTypeId(name)) {
            qCWarning(lcCurveLoader).nospace() << "Skipping " << filePath << ": user curve name '" << name
                                               << "' conflicts with builtin — use a different name";
            return std::nullopt;
        }

        // typeId is guaranteed present and non-empty by the schema gate above
        // (required, minLength 1), which fails closed, so no empty-check here.
        const QString typeId = obj.value(QLatin1String("typeId")).toString();
        const QJsonObject params = obj.value(QLatin1String("parameters")).toObject();

        // Delegate parameter-shape validation to the CurveRegistry's
        // JsonFactory for `typeId`. Keeps the registry as the sole
        // schema authority — third-party curves that registered a JSON
        // factory become JSON-loadable automatically, and the built-in
        // spring / cubic-bezier / elastic / bounce shapes live in
        // exactly one place (curveregistry.cpp's registerBuiltins).
        //
        // A null return means either:
        //   - unknown typeId (no factory registered), or
        //   - known typeId whose JSON factory rejected the parameters
        //     (validation failure — factory emits its own qCWarning).
        auto curve = registry->tryCreateFromJson(typeId, params);
        if (!curve) {
            qCWarning(lcCurveLoader) << "Skipping" << filePath << ": typeId" << typeId
                                     << "unknown or parameters rejected (see curve registry log)";
            return std::nullopt;
        }

        const QString wireFormat = curve->toString();

        internal::CurvePayload payload{obj.value(QLatin1String("displayName")).toString(), std::move(curve),
                                       wireFormat};

        PhosphorFsLoader::ParsedEntry parsed;
        parsed.key = name;
        parsed.sourcePath = filePath;
        parsed.payload = std::move(payload);
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

        // Build the post-rescan set and track which entries need a
        // registry write in a single pass.
        QStringList changedOrAddedKeys;
        changedOrAddedKeys.reserve(currentEntries.size());

        for (const auto& parsed : currentEntries) {
            const auto* payload = std::any_cast<internal::CurvePayload>(&parsed.payload);
            if (!payload) {
                qCWarning(lcCurveLoader) << "commitBatch: payload type-mismatch for" << parsed.key;
                continue;
            }

            // Payload diff: wire-format string comparison.
            const auto snapshotIt = m_lastCommittedPayloads.constFind(parsed.key);
            const bool payloadChanged =
                snapshotIt == m_lastCommittedPayloads.constEnd() || snapshotIt->wireFormat != payload->wireFormat;

            // Source-metadata diff: when the user copy is deleted and the
            // system file re-emerges with byte-identical content, the
            // payload stays equal but source paths shift — the consumer
            // signal must still fire so settings UIs update.
            const QPair<QString, QString> currentSources{parsed.sourcePath, parsed.systemSourcePath};
            const auto sourcesIt = m_lastCommittedSources.constFind(parsed.key);
            const bool sourcesChanged = sourcesIt == m_lastCommittedSources.constEnd() || *sourcesIt != currentSources;

            if (payloadChanged) {
                m_lastCommittedPayloads.insert(parsed.key, *payload);
                changedOrAddedKeys.append(parsed.key);
                lastBatchChanged = true;
            }
            if (sourcesChanged) {
                m_lastCommittedSources.insert(parsed.key, currentSources);
                lastBatchChanged = true;
            }

            // Mirror the parsed entry into the tracked entries map.
            CurveLoader::Entry trackedEntry;
            trackedEntry.name = parsed.key;
            trackedEntry.displayName = payload->displayName;
            trackedEntry.sourcePath = parsed.sourcePath;
            trackedEntry.systemSourcePath = parsed.systemSourcePath;
            entries.insert(parsed.key, std::move(trackedEntry));
        }

        // Registry write: unregister removed keys, re-register changed.
        if (!registry) {
            qCWarning(lcCurveLoader) << "commitBatch: registry not set";
            return;
        }

        for (const QString& key : removedKeys) {
            registry->unregisterFactory(key);
        }

        for (const QString& key : changedOrAddedKeys) {
            const auto it = m_lastCommittedPayloads.constFind(key);
            if (it == m_lastCommittedPayloads.constEnd()) {
                continue;
            }
            const auto curve = it->curve;
            registry->registerFactory(
                key,
                [curve](const QString&, const QString&) {
                    return curve;
                },
                m_ownerTag);
        }
    }

private:
    QString m_ownerTag;

    /// Snapshot of the last-committed payload per key, used for diffing.
    QHash<QString, internal::CurvePayload> m_lastCommittedPayloads;

    /// Snapshot of (sourcePath, systemSourcePath) per key, used for
    /// the source-metadata diff (Phase 1c+1d fix).
    QHash<QString, QPair<QString, QString>> m_lastCommittedSources;
};

CurveLoader::CurveLoader(CurveRegistry& registry, QObject* parent)
    : QObject(parent)
    , m_sink(std::make_unique<Sink>(registry, makeCurveLoaderOwnerTag()))
    , m_loader(std::make_unique<PhosphorFsLoader::DirectoryLoader>(*m_sink))
{
    // Gate `curvesChanged` on the per-batch change flag. DirectoryLoader
    // emits `entriesChanged` unconditionally on every rescan (tests and
    // debug tooling rely on that), but consumers of CurveLoader only
    // care when the tracked curve set or one of its wire-formats
    // actually changed — re-parsing the same files a second time (e.g.
    // a `requestRescan()` with no content change) is invisible at this
    // layer. Matches the header's documented contract.
    connect(m_loader.get(), &PhosphorFsLoader::DirectoryLoader::entriesChanged, this, [this]() {
        if (m_sink->lastBatchChanged) {
            Q_EMIT curvesChanged();
        }
    });
}

CurveLoader::~CurveLoader()
{
    // Bulk-remove every factory this loader registered under its owner
    // tag — mirrors `ProfileLoader::~ProfileLoader` issuing
    // `clearOwner(ownerTag)` against PhosphorProfileRegistry.
    //
    // Owner-tagged partitioning means another `CurveLoader` (or any
    // direct `registerFactory` caller with a different tag) that
    // happens to share a typeId is preserved — the previous per-key
    // unregister loop would have evicted the other registrant's entry
    // silently. Each removal emits an info log line in CurveRegistry
    // so rollback is observable in traces.
    if (m_sink && m_sink->registry && !m_sink->ownerTag().isEmpty()) {
        m_sink->registry->unregisterByOwner(m_sink->ownerTag());
    }
}

int CurveLoader::loadFromDirectory(const QString& directory, LiveReload liveReload)
{
    return m_loader->loadFromDirectory(directory, liveReload);
}

int CurveLoader::loadFromDirectories(const QStringList& directories, LiveReload liveReload,
                                     PhosphorFsLoader::RegistrationOrder order)
{
    return m_loader->loadFromDirectories(directories, liveReload, order);
}

int CurveLoader::loadLibraryBuiltins(LiveReload liveReload)
{
    // Use the install-prefix directory baked in at build time via
    // PHOSPHORANIMATION_INSTALL_DATADIR. Namespacing under the library's
    // own `phosphor-animation/curves` subdir means a consumer's
    // user-local `~/.local/share/<consumer>/curves` pack is NEVER
    // accidentally pulled into the library's built-in load (the old
    // XDG-based `locateAll(GenericDataLocation,
    // "phosphor-animation/curves", ...)` had the reverse property —
    // a user placing files under `~/.local/share/phosphor-animation/
    // curves` would silently shadow the library's immutable pack).
    //
    // When the macro is absent (e.g. a sub-project build that did not
    // propagate the datadir), fall back to a no-op — the caller's
    // consumer-namespaced directories are still loaded via the
    // `loadFromDirectory[ies]` entry points.
#ifdef PHOSPHORANIMATION_INSTALL_DATADIR
    const QString dir = QStringLiteral(PHOSPHORANIMATION_INSTALL_DATADIR "/curves");
    if (!QDir(dir).exists()) {
        return 0;
    }
    return loadFromDirectory(dir, liveReload);
#else
    Q_UNUSED(liveReload);
    return 0;
#endif
}

void CurveLoader::requestRescan()
{
    m_loader->requestRescan();
}

int CurveLoader::registeredCount() const
{
    return m_loader->registeredCount();
}

QString CurveLoader::ownerTag() const
{
    return m_sink ? m_sink->ownerTag() : QString();
}

QList<CurveLoader::Entry> CurveLoader::entries() const
{
    // Match DirectoryLoader::entries() — sort by key for deterministic
    // ordering across platforms and Qt versions. QHash iteration order
    // is intentionally randomised in Qt6, so without the sort snapshot
    // tests and diagnostic logs would differ between runs.
    QList<CurveLoader::Entry> sorted = m_sink->entries.values();
    std::sort(sorted.begin(), sorted.end(), [](const Entry& a, const Entry& b) {
        return a.name < b.name;
    });
    return sorted;
}

} // namespace PhosphorAnimation
