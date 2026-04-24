// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/CurveLoader.h>

#include <PhosphorAnimation/CurveRegistry.h>

#include <PhosphorJsonLoader/DirectoryLoader.h>
#include <PhosphorJsonLoader/IDirectoryLoaderSink.h>
#include <PhosphorJsonLoader/ParsedEntry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QUuid>

#include <algorithm>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcCurveLoader, "phosphoranimation.curveloader")

/// Generate a unique-per-instance owner tag. UUIDs are never reused
/// within a process, so sequential loaders (tests, plugin reloads,
/// embedded shells) never inherit authority over a prior loader's
/// unclaimed registry entries — even when a new loader reuses the
/// same heap slot. Prefixed `"curveloader:"` for debug clarity.
QString makeCurveLoaderOwnerTag()
{
    return QStringLiteral("curveloader:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace

/**
 * The curve-specific sink — knows the curve JSON schema, knows how to
 * turn it into a `shared_ptr<const Curve>`, and knows how to register /
 * unregister entries against `CurveRegistry`. The directory walk,
 * watching, and debounce live in `PhosphorJsonLoader::DirectoryLoader`.
 */
class CurveLoader::Sink : public PhosphorJsonLoader::IDirectoryLoaderSink
{
public:
    /// Per-key payload shipped through the `ParsedEntry::payload` std::any.
    struct Payload
    {
        QString displayName;
        std::shared_ptr<const Curve> curve;
        /// Captured wire-format string used to build `curve`. Persisted
        /// through the payload so `commitBatch` can diff against the
        /// previous commit and skip re-registering unchanged entries.
        QString wireFormat;
    };

    Sink(CurveRegistry& reg, QString owner)
        : registry(&reg)
        , ownerTag(std::move(owner))
    {
    }

    CurveRegistry* registry; ///< pinned at ctor — no per-call mutation
    QString ownerTag; ///< stable per-instance tag for partitioned factory ownership
    QHash<QString, CurveLoader::Entry> entries; ///< name → entry (for entries() introspection)
    /// Wire-format snapshot per successfully committed key, used by
    /// commitBatch to skip re-registration of keys whose parsed curve
    /// is byte-identical to the previous scan. Empty after teardown —
    /// removedKeys are evicted here first so a later re-add produces
    /// a miss and actually registers.
    QHash<QString, QString> lastCommittedWireFormat;
    /// Set by commitBatch whenever the tracked curve set changes —
    /// either a key was removed, a new key was registered, or an
    /// existing key's wire-format differs from the last commit. Read
    /// by CurveLoader::onEntriesChanged to decide whether to emit
    /// `curvesChanged`. Re-set to false before every commit; `false`
    /// at end-of-batch means a no-op rescan (re-parse of files whose
    /// contents didn't change) and the consumer signal is suppressed.
    bool lastBatchChanged = false;

    std::optional<PhosphorJsonLoader::ParsedEntry> parseFile(const QString& filePath) override
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            qCWarning(lcCurveLoader) << "Skipping unreadable file" << filePath << ":" << file.errorString();
            return std::nullopt;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCWarning(lcCurveLoader) << "Skipping malformed JSON" << filePath << ":" << parseError.errorString();
            return std::nullopt;
        }
        if (!doc.isObject()) {
            qCWarning(lcCurveLoader) << "Skipping non-object root JSON in" << filePath;
            return std::nullopt;
        }
        const QJsonObject obj = doc.object();

        const QString name = obj.value(QLatin1String("name")).toString();
        if (name.isEmpty()) {
            qCWarning(lcCurveLoader) << "Skipping" << filePath << ": missing required 'name' field";
            return std::nullopt;
        }

        // The filename (without extension) is the user's ergonomic
        // handle on the curve; the `name` field is what actually gets
        // registered. If the two diverge — typically because the user
        // copied `widget.fade.json → custom.json` and forgot to rename
        // the inner field — the result is a curve that shadows the
        // original under the inner-name key while the file on disk
        // suggests a different identity. Reject up front with a clear
        // diagnostic naming both sides.
        const QString basename = QFileInfo(filePath).completeBaseName();
        if (name != basename) {
            qCWarning(lcCurveLoader).nospace()
                << "Skipping " << filePath << ": curve name '" << name << "' does not match filename '" << basename
                << "' — rejecting to avoid silent shadowing";
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

        const QString typeId = obj.value(QLatin1String("typeId")).toString();
        if (typeId.isEmpty()) {
            qCWarning(lcCurveLoader) << "Skipping" << filePath << ": missing required 'typeId' field";
            return std::nullopt;
        }
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

        // Capture the canonical wire-format via `curve->toString()` for
        // commitBatch's unchanged-content diff. `toString()` is the
        // Curve-contract canonical form, so two parses of the same JSON
        // file produce the same wire string — the diff stays stable
        // across reloads even if the JSON factory internals change.
        const QString wireFormat = curve->toString();

        Payload payload{obj.value(QLatin1String("displayName")).toString(), std::move(curve), wireFormat};

        PhosphorJsonLoader::ParsedEntry parsed;
        parsed.key = name;
        parsed.sourcePath = filePath;
        parsed.payload = std::move(payload);
        return parsed;
    }

    void commitBatch(const QStringList& removedKeys,
                     const QList<PhosphorJsonLoader::ParsedEntry>& currentEntries) override
    {
        if (!registry) {
            qCWarning(lcCurveLoader) << "commitBatch: registry not set";
            return;
        }

        // Reset the per-batch "anything actually changed" flag. Any
        // remove/add/content-change below flips it to true, which
        // gates the consumer-facing `curvesChanged` signal.
        lastBatchChanged = false;

        // Purge entries whose backing files are no longer on disk.
        // CurveRegistry::registerFactory is replace-semantic, so a
        // future registration can re-use the key — but the current
        // caller wants the key fully unregistered until then. We own
        // each factory under `ownerTag`, so calling `unregisterFactory`
        // here only evicts entries we ourselves registered.
        //
        // Gate `lastBatchChanged` on the actual snapshot delete: a
        // removedKeys entry whose key never made it into our snapshot
        // (parsed and rejected on a prior pass, then deleted from disk)
        // produced no externally-visible registration, so deleting it
        // doesn't change anything a consumer would see. Only count it as
        // a batch-change if we genuinely lose tracked state.
        for (const QString& key : removedKeys) {
            const bool hadEntry = entries.remove(key) > 0;
            const bool hadWire = lastCommittedWireFormat.remove(key) > 0;
            if (hadEntry || hadWire) {
                registry->unregisterFactory(key);
                lastBatchChanged = true;
            }
        }

        // Install the current set. Skip re-registering entries whose
        // wire-format is byte-identical to the previous commit — the
        // curve they'd produce is equivalent, and the extra mutex
        // round-trip through CurveRegistry is pointless churn. Mirrors
        // the shape of PhosphorProfileRegistry::reloadFromOwner, which
        // has the same "only touch entries that actually changed"
        // property.
        for (const auto& p : currentEntries) {
            const auto payload = std::any_cast<Payload>(&p.payload);
            if (!payload) {
                qCWarning(lcCurveLoader) << "commitBatch: payload type-mismatch for" << p.key;
                continue;
            }

            const auto existingIt = lastCommittedWireFormat.constFind(p.key);
            const bool unchanged = existingIt != lastCommittedWireFormat.constEnd()
                && *existingIt == payload->wireFormat && entries.contains(p.key);
            if (!unchanged) {
                const auto curve = payload->curve;
                // registerFactory returns true when it replaced an
                // existing registration. That's expected on every
                // content change — we log at debug only. A false
                // return with a NEW key is also fine (fresh insert).
                // The error case is registerFactory rejecting the
                // input (empty typeId / null factory) — should never
                // happen here since parseFile enforces a non-empty
                // name and builds a non-null curve, but check
                // defensively so future refactors can't silently lose
                // registrations.
                //
                // Pass the owner tag so the dtor's
                // `unregisterByOwner(ownerTag)` only evicts what we
                // installed, preserving other loaders' and direct
                // `registerFactory` callers' entries.
                registry->registerFactory(
                    p.key,
                    [curve](const QString&, const QString&) {
                        return curve;
                    },
                    ownerTag);
                if (!registry->has(p.key)) {
                    qCWarning(lcCurveLoader) << "commitBatch: registerFactory silently rejected" << p.key;
                    continue;
                }
                lastCommittedWireFormat.insert(p.key, payload->wireFormat);
                lastBatchChanged = true;
            }

            CurveLoader::Entry trackedEntry;
            trackedEntry.name = p.key;
            trackedEntry.displayName = payload->displayName;
            trackedEntry.sourcePath = p.sourcePath;
            trackedEntry.systemSourcePath = p.systemSourcePath;
            entries.insert(p.key, std::move(trackedEntry));
        }
    }
};

CurveLoader::CurveLoader(CurveRegistry& registry, QObject* parent)
    : QObject(parent)
    , m_sink(std::make_unique<Sink>(registry, makeCurveLoaderOwnerTag()))
    , m_loader(std::make_unique<PhosphorJsonLoader::DirectoryLoader>(*m_sink))
{
    // Gate `curvesChanged` on the per-batch change flag. DirectoryLoader
    // emits `entriesChanged` unconditionally on every rescan (tests and
    // debug tooling rely on that), but consumers of CurveLoader only
    // care when the tracked curve set or one of its wire-formats
    // actually changed — re-parsing the same files a second time (e.g.
    // a `requestRescan()` with no content change) is invisible at this
    // layer. Matches the header's documented contract.
    connect(m_loader.get(), &PhosphorJsonLoader::DirectoryLoader::entriesChanged, this, [this]() {
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
    if (m_sink && m_sink->registry && !m_sink->ownerTag.isEmpty()) {
        m_sink->registry->unregisterByOwner(m_sink->ownerTag);
    }
}

int CurveLoader::loadFromDirectory(const QString& directory, LiveReload liveReload)
{
    return m_loader->loadFromDirectory(directory, liveReload);
}

int CurveLoader::loadFromDirectories(const QStringList& directories, LiveReload liveReload)
{
    return m_loader->loadFromDirectories(directories, liveReload);
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
    return m_sink ? m_sink->ownerTag : QString();
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
