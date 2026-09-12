// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPresetStore.h>

#include <PhosphorFsLoader/FileLimits.h>
#include <PhosphorFsLoader/IDirectoryLoaderSink.h>
#include <PhosphorFsLoader/ParsedEntry.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QUuid>

#include <algorithm>
#include <any>

namespace PhosphorShaders {

namespace {
Q_LOGGING_CATEGORY(lcPresetStore, "phosphorshaders.presetstore")

/// Every family, in one place, so adding one cannot leave a slot unconsidered.
/// Sized by ShaderFamilyCount and static_asserted below, so this list and the
/// enum cannot drift apart silently.
constexpr std::array<ShaderFamily, ShaderFamilyCount> kAllFamilies{
    ShaderFamily::Animation,
    ShaderFamily::Surface,
    ShaderFamily::Pointer,
    ShaderFamily::Overlay,
};
// A braced list shorter than the array default-initialises the tail, which would
// make a missing family read as Animation rather than fail to compile. Checking
// the last slot catches that: adding an enumerator anywhere moves the highest
// value and leaves this slot holding a default.
static_assert(kAllFamilies.back() == static_cast<ShaderFamily>(ShaderFamilyCount - 1),
              "kAllFamilies must list every ShaderFamily, in enum order");
} // namespace

QString standardUserPresetRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/plasmazones/shader-presets");
}

QString userPresetDirectory(const QString& root, ShaderFamily family)
{
    return root + QLatin1Char('/') + shaderFamilyToken(family);
}

namespace {
/// Namespace UUID for deriving a migrated preset's id from its old filename.
/// Function-local static rather than a namespace-scope QUuid: fromString is not
/// constexpr, and a dynamic-initialised global would be subject to cross-TU
/// static-init ordering. Same idiom as `shaderNamespaceUuid` in the registry.
const QUuid& legacyPresetNamespaceUuid()
{
    static const QUuid uuid = QUuid::fromString(QStringLiteral("{6f2b8d51-4c3a-4e7f-9b10-2d8e4a5c7b63}"));
    return uuid;
}

// The keys the old zone-only save path wrote. Named here rather than inline so
// the mapping onto the current shape reads as a table.
constexpr auto LegacyFieldShaderId = "shaderId";
constexpr auto LegacyFieldShaderParams = "shaderParams";
constexpr auto LegacyFieldName = "name";
} // namespace

int migrateLegacyOverlayPresets(const QString& root)
{
    QDir rootDir(root);
    if (!rootDir.exists()) {
        return 0;
    }
    QStringList candidates = rootDir.entryList({QStringLiteral("*.json")}, QDir::Files);
    if (candidates.isEmpty()) {
        return 0;
    }
    // Same entry cap the rest of the preset path inherits from DirectoryLoader,
    // and for the same reason. The per-file byte cap below bounded how much each
    // file could cost but nothing bounded HOW MANY, and this scan runs at startup
    // in four processes over a user-writable directory: fifty thousand junk JSON
    // files would stall the daemon, the effect and the settings window before any
    // of them drew. Truncating rather than refusing, so a directory that is merely
    // large still migrates what it can.
    if (candidates.size() > PhosphorFsLoader::DirectoryLoader::kMaxEntries) {
        qCWarning(lcPresetStore) << "Legacy preset directory holds" << candidates.size() << "files; migrating the first"
                                 << PhosphorFsLoader::DirectoryLoader::kMaxEntries << "and leaving the rest:" << root;
        candidates = candidates.mid(0, PhosphorFsLoader::DirectoryLoader::kMaxEntries);
    }

    const QString targetDir = userPresetDirectory(root, ShaderFamily::Overlay);
    // Hoisted: the whole migration aborts if this fails, so nothing depended on
    // it running once per candidate file.
    if (!QDir().mkpath(targetDir)) {
        qCWarning(lcPresetStore) << "Cannot create the overlay preset directory, aborting migration:" << targetDir;
        return 0;
    }
    int migrated = 0;

    for (const QString& fileName : candidates) {
        const QString sourcePath = rootDir.absoluteFilePath(fileName);

        QFile source(sourcePath);
        if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCWarning(lcPresetStore) << "Legacy preset will not open, leaving it:" << sourcePath
                                     << source.errorString();
            continue;
        }
        // Same per-file cap the rest of the preset path inherits from
        // DirectoryLoader. This scan runs at startup in four processes over a
        // user-writable directory, and read-all had no bound at all.
        if (source.size() > PhosphorFsLoader::DirectoryLoader::kMaxFileBytes) {
            qCWarning(lcPresetStore) << "Legacy preset is larger than"
                                     << PhosphorFsLoader::DirectoryLoader::kMaxFileBytes
                                     << "bytes, leaving it:" << sourcePath;
            continue;
        }
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(source.readAll(), &parseError);
        source.close();
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcPresetStore) << "Legacy preset is not a JSON object, leaving it:" << sourcePath;
            continue;
        }

        const QJsonObject obj = doc.object();
        const QString packId = obj.value(QLatin1String(LegacyFieldShaderId)).toString();
        if (packId.isEmpty()) {
            // Not a preset this ever wrote. Someone else's file living in the
            // same directory, and not ours to move or delete.
            continue;
        }

        ShaderPreset preset;
        // Derived, not random: two processes racing this migration must land on
        // the same target path, or the user ends up with the preset twice.
        //
        // WithoutBraces because this id becomes a filename below, and CLAUDE.md
        // reserves the braced spelling for everything that is not a filesystem
        // path. It also makes the id identical to the file's stem, so the
        // stem fallback and the declared `id` field agree.
        preset.id = QUuid::createUuidV5(legacyPresetNamespaceUuid(), fileName).toString(QUuid::WithoutBraces);
        preset.name = obj.value(QLatin1String(LegacyFieldName)).toString();
        if (preset.name.isEmpty()) {
            preset.name = QFileInfo(fileName).completeBaseName();
        }
        // Truncate at the write, the same bound ShaderPreset::fromJson applies at
        // the read. Without it the migration produced a file fromJson would then
        // silently shorten, so the name on disk and the name in every picker row
        // disagreed — the asymmetric half of a defensive pair.
        if (preset.name.size() > ShaderPreset::MaxNameChars) {
            preset.name.truncate(ShaderPreset::MaxNameChars);
            // And drop a surrogate the cut split, exactly as fromJson does.
            if (!preset.name.isEmpty() && preset.name.back().isHighSurrogate()) {
                preset.name.chop(1);
            }
        }
        preset.packId = packId;
        preset.params = obj.value(QLatin1String(LegacyFieldShaderParams)).toObject().toVariantMap();

        const QString targetPath = targetDir + QLatin1Char('/') + preset.id + QStringLiteral(".json");

        if (!QFile::exists(targetPath)) {
            // QSaveFile so a crash mid-write cannot leave a truncated preset
            // behind that the scan then refuses on every later pass.
            QSaveFile target(targetPath);
            if (!target.open(QIODevice::WriteOnly | QIODevice::Text)) {
                qCWarning(lcPresetStore) << "Cannot write migrated preset, leaving the original:" << targetPath
                                         << target.errorString();
                continue;
            }
            const QByteArray json = QJsonDocument(preset.toJson()).toJson(QJsonDocument::Indented);
            if (target.write(json) != json.size() || !target.commit()) {
                qCWarning(lcPresetStore) << "Failed writing migrated preset, leaving the original:" << targetPath
                                         << target.errorString();
                continue;
            }
            ++migrated;
        }

        // Remove only once the target is known to exist, so a failure anywhere
        // above leaves the original in place to be retried. A target that
        // already existed means an earlier run wrote it and died before the
        // removal; finishing the job here is why this is not gated on having
        // just written the file.
        if (!QFile::remove(sourcePath)) {
            qCWarning(lcPresetStore) << "Migrated preset but could not remove the original:" << sourcePath;
        }
    }

    if (migrated > 0) {
        qCInfo(lcPresetStore) << "Imported" << migrated << "overlay preset(s) into" << targetDir;
    }
    return migrated;
}

/// Turns one preset file into a `ShaderPreset` and commits each rescan batch to
/// the registry. The registry does its own diffing and per-pack signalling, so
/// this sink keeps no snapshot of its own — unlike `CurveLoader::Sink`, which
/// has to diff because its registry is replace-semantic and unconditionally
/// noisy.
class ShaderPresetStore::Sink : public PhosphorFsLoader::IDirectoryLoaderSink
{
public:
    Sink(ShaderPresetRegistry& reg, ShaderFamily fam)
        : registry(reg)
        , family(fam)
    {
    }

    /// A plain reference, and that is the point of the shape around it. This
    /// used to be a QPointer guarding against the registry being destroyed
    /// first, because both it and the owning loader were QObject children of the
    /// store and Qt frees children in insertion order. The store now owns the
    /// registry as a by-value member declared BEFORE these sinks, so ordinary
    /// member-destruction order makes "destroyed first" unreachable and there is
    /// nothing for a weak pointer to catch. The destructor retracts nothing, so that
    /// declaration order is the whole guarantee.
    ShaderPresetRegistry& registry;
    ShaderFamily family;

    std::optional<PhosphorFsLoader::ParsedEntry> parseFile(const QString& filePath) override
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCWarning(lcPresetStore) << "Skipping preset file that will not open:" << filePath << file.errorString();
            return std::nullopt;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCWarning(lcPresetStore) << "Skipping malformed preset file:" << filePath << parseError.errorString();
            return std::nullopt;
        }
        if (!doc.isObject()) {
            qCWarning(lcPresetStore) << "Skipping preset file whose root is not an object:" << filePath;
            return std::nullopt;
        }

        // The filename stem is the fallback identity, so a hand-written file
        // with no `id` still gets a stable one an assignment can reference.
        const QString fallbackId = QFileInfo(filePath).completeBaseName();
        ShaderPreset preset = ShaderPreset::fromJson(doc.object(), fallbackId);
        preset.sourcePath = filePath;
        preset.readOnly = false;

        if (!preset.isValid()) {
            // isValid() is id + packId. A preset naming no pack cannot be
            // offered anywhere, because parameter ids mean nothing across
            // packs — so this is a refusal, not a degraded load.
            qCWarning(lcPresetStore) << "Skipping preset file with no pack id:" << filePath;
            return std::nullopt;
        }
        if (preset.name.isEmpty()) {
            // A nameless preset would render as a blank row in the picker.
            // Fall back to the id, which is at worst a UUID the user can
            // rename, rather than dropping a preset an assignment may point at.
            preset.name = preset.id;
        }

        PhosphorFsLoader::ParsedEntry parsed;
        parsed.key = preset.id;
        parsed.sourcePath = filePath;
        parsed.payload = std::move(preset);
        return parsed;
    }

    void commitBatch(const QStringList& removedKeys,
                     const QList<PhosphorFsLoader::ParsedEntry>& currentEntries) override
    {
        // `removedKeys` needs no handling of its own: the registry takes the
        // whole family's current set and replaces it, so a key that is no
        // longer in `currentEntries` is dropped by construction. Reading it
        // would be a second, redundant source of truth for the same fact.
        Q_UNUSED(removedKeys);

        QList<ShaderPreset> presets;
        presets.reserve(currentEntries.size());
        for (const auto& parsed : currentEntries) {
            const auto* preset = std::any_cast<ShaderPreset>(&parsed.payload);
            if (!preset) {
                qCWarning(lcPresetStore) << "commitBatch: payload type-mismatch for" << parsed.key;
                continue;
            }
            presets.append(*preset);
        }

        // No null check on the registry, and none is owed now: it is a reference
        // to a member that outlives every sink. The check this replaces existed
        // only because the pointer could dangle.
        registry.setUserPresets(family, presets);
    }
};

ShaderPresetStore::ShaderPresetStore(QObject* parent)
    : QObject(parent)
{
}

ShaderPresetStore::~ShaderPresetStore()
{
    // Tear the publishers down in an order this class chose, and retract
    // NOTHING. See the note inside the loop for why the retraction that used to
    // be here was the hazard rather than the safeguard.
    //
    // The registry needs no protecting either way: it is a member declared
    // before the slots, so it outlives every line below under ordinary
    // member-destruction order.
    for (const ShaderFamily family : kAllFamilies) {
        Publisher& publisher = m_publishers[slotOf(family)];
        if (!publisher.active()) {
            // Nothing to do, and nothing below would misbehave on an empty slot
            // either — resetting a null unique_ptr is a no-op. Skipping is for
            // the reader, not for correctness.
            continue;
        }
        // NO retraction here, and that absence is the point.
        //
        // This used to hand the registry an empty set per family, carried over
        // from the shape where the registry OUTLIVED the loaders. Its stated
        // reason — stop a sequential store inheriting presets no file backs —
        // cannot apply any more: the registry is this store's own by-value
        // member and dies with it, so the next store gets a fresh empty one and
        // has nothing to inherit through.
        //
        // Keeping it was actively harmful rather than merely redundant.
        // `setUserPresets` EMITS `presetsChanged` when the family had any
        // presets, and in the KWin effect that signal is connected with the
        // effect itself as context. Qt severs a QObject's connections in
        // ~QObject, which runs AFTER its members are destroyed — so the emission
        // landed in a handler that then touched `m_pointerPass`, a member
        // declared after the shader manager and therefore already destroyed.
        // That is a write into freed memory holding GL handles: the same fault
        // class this refactor removed from the library, re-created one layer up
        // by the one line that did not need to be here.
        //
        // Loader before sink: the loader's watcher can still call into the sink,
        // so the thing that calls must die before the thing it calls. The field
        // order in Publisher already gives this, but resetting explicitly means
        // the ordering does not silently depend on it.
        publisher.loader.reset();
        publisher.sink.reset();
    }
}

void ShaderPresetStore::load(const QString& root, const QList<ShaderFamily>& families)
{
    // An empty root would resolve against the process working directory:
    // QDir(QString()) is QDir("."), whose exists() is true, so the migration
    // would scan and read the CWD's *.json files, and userPresetDirectory()
    // would hand out filesystem-root paths like "/animation". A relative root
    // is the same hazard with a mkpath that succeeds.
    if (root.isEmpty() || !QDir::isAbsolutePath(root)) {
        qCWarning(lcPresetStore) << "Refusing to load presets from a root that is not an absolute path:" << root;
        return;
    }

    // Refuse a SECOND root once anything is publishing, rather than overwriting
    // m_root. The publisher slots are left alone below when they are already
    // occupied, so a changed root would have moved what `directoryFor()` hands to
    // the bridge while the watcher stayed on the old directory: a save would land
    // in the new tree, the rescan would re-read the old one, and the preset would
    // come back invisible with `rescanNow()` still answering true. Nothing calls
    // load() twice with different roots today, which is exactly why this needs to
    // be stated rather than assumed.
    const bool anyPublishing = std::any_of(m_publishers.cbegin(), m_publishers.cend(), [](const Publisher& p) {
        return p.active();
    });
    if (anyPublishing && root != m_root) {
        qCWarning(lcPresetStore) << "Refusing to re-root an already-publishing preset store; keeping" << m_root
                                 << "and ignoring" << root;
        return;
    }

    m_root = root;

    // Before the scan, or the imported presets would not be seen until the
    // next rescan. Idempotent, so calling it on every startup is free.
    migrateLegacyOverlayPresets(root);

    // An empty list means every family, so an existing caller keeps the old
    // behaviour and a new one opts in to less.
    const QList<ShaderFamily> wanted =
        families.isEmpty() ? QList<ShaderFamily>(kAllFamilies.cbegin(), kAllFamilies.cend()) : families;

    for (const ShaderFamily family : wanted) {
        Publisher& publisher = m_publishers[slotOf(family)];
        if (publisher.active()) {
            // Already publishing: leave it exactly as it is. A caller naming a
            // family twice, or calling load() twice, cannot produce a second
            // publisher — the slot is the publisher, so there is nowhere for a
            // second one to go.
            continue;
        }
        publisher.sink = std::make_unique<Sink>(m_registry, family);
        publisher.loader = std::make_unique<PhosphorFsLoader::DirectoryLoader>(*publisher.sink);
        // LiveReload::On is the point of the whole design: a preset retuned on
        // disk has to reach every process without a restart. The watcher
        // promotes itself to the parent directory when the family directory
        // does not exist yet, so a fresh install picks up the first preset the
        // user saves without anyone having to create the tree up front.
        publisher.loader->loadFromDirectory(userPresetDirectory(root, family), LiveReload::On);
    }
}

ShaderPresetRegistry& ShaderPresetStore::registry()
{
    return m_registry;
}

const ShaderPresetRegistry& ShaderPresetStore::registry() const
{
    return m_registry;
}

bool ShaderPresetStore::publishes(ShaderFamily family) const
{
    return m_publishers[slotOf(family)].active();
}

bool ShaderPresetStore::rescanNow(ShaderFamily family)
{
    Publisher& publisher = m_publishers[slotOf(family)];
    if (!publisher.active()) {
        return false;
    }
    publisher.loader->rescanNow();
    return true;
}

QString ShaderPresetStore::directoryFor(ShaderFamily family) const
{
    // EMPTY when load() never ran, or refused a non-absolute root. Without this the
    // answer was `userPresetDirectory("", family)` — a filesystem-root path like
    // "/animation" — so load()'s refusal was invisible to the one caller that turns
    // this into a write (ShaderPresetBridge::commit, through presetDirectory). It
    // failed loudly on mkpath rather than writing anywhere bad, but a refusal the
    // caller cannot see is the asymmetric half of a defensive pair.
    if (m_root.isEmpty()) {
        return QString();
    }
    return userPresetDirectory(m_root, family);
}

} // namespace PhosphorShaders
