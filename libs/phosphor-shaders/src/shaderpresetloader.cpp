// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderPresetLoader.h>

#include <PhosphorShaders/ShaderPresetRegistry.h>

#include <PhosphorFsLoader/DirectoryLoader.h>
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
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <any>

namespace PhosphorShaders {

namespace {
Q_LOGGING_CATEGORY(lcPresetLoader, "phosphorshaders.presetloader")
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
    const QStringList candidates = rootDir.entryList({QStringLiteral("*.json")}, QDir::Files);
    if (candidates.isEmpty()) {
        return 0;
    }

    const QString targetDir = userPresetDirectory(root, ShaderFamily::Overlay);
    int migrated = 0;

    for (const QString& fileName : candidates) {
        const QString sourcePath = rootDir.absoluteFilePath(fileName);

        QFile source(sourcePath);
        if (!source.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCWarning(lcPresetLoader) << "Legacy preset will not open, leaving it:" << sourcePath
                                      << source.errorString();
            continue;
        }
        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(source.readAll(), &parseError);
        source.close();
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            qCWarning(lcPresetLoader) << "Legacy preset is not a JSON object, leaving it:" << sourcePath;
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
        preset.id = QUuid::createUuidV5(legacyPresetNamespaceUuid(), fileName).toString();
        preset.name = obj.value(QLatin1String(LegacyFieldName)).toString();
        if (preset.name.isEmpty()) {
            preset.name = QFileInfo(fileName).completeBaseName();
        }
        preset.packId = packId;
        preset.params = obj.value(QLatin1String(LegacyFieldShaderParams)).toObject().toVariantMap();

        if (!QDir().mkpath(targetDir)) {
            qCWarning(lcPresetLoader) << "Cannot create the overlay preset directory, aborting migration:" << targetDir;
            return migrated;
        }
        const QString targetPath = targetDir + QLatin1Char('/') + preset.id + QStringLiteral(".json");

        if (!QFile::exists(targetPath)) {
            // QSaveFile so a crash mid-write cannot leave a truncated preset
            // behind that the loader then refuses on every later scan.
            QSaveFile target(targetPath);
            if (!target.open(QIODevice::WriteOnly | QIODevice::Text)) {
                qCWarning(lcPresetLoader)
                    << "Cannot write migrated preset, leaving the original:" << targetPath << target.errorString();
                continue;
            }
            const QByteArray json = QJsonDocument(preset.toJson()).toJson(QJsonDocument::Indented);
            if (target.write(json) != json.size() || !target.commit()) {
                qCWarning(lcPresetLoader)
                    << "Failed writing migrated preset, leaving the original:" << targetPath << target.errorString();
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
            qCWarning(lcPresetLoader) << "Migrated preset but could not remove the original:" << sourcePath;
        }
    }

    if (migrated > 0) {
        qCInfo(lcPresetLoader) << "Imported" << migrated << "overlay preset(s) into" << targetDir;
    }
    return migrated;
}

/// Turns one preset file into a `ShaderPreset` and commits each rescan batch to
/// the registry. The registry does its own diffing and per-pack signalling, so
/// this sink keeps no snapshot of its own — unlike `CurveLoader::Sink`, which
/// has to diff because its registry is replace-semantic and unconditionally
/// noisy.
class ShaderPresetLoader::Sink : public PhosphorFsLoader::IDirectoryLoaderSink
{
public:
    Sink(ShaderPresetRegistry& reg, ShaderFamily fam)
        : registry(&reg)
        , family(fam)
    {
    }

    // QPointer, not a raw pointer: the registry can be destroyed before this
    // sink's owning loader is. `ShaderPresetStore` parents both to itself, and
    // QObject frees children in insertion order, so the registry — constructed
    // first — dies first. A raw pointer left the destructor below dereferencing
    // freed memory, and a null check could not see it, because a freed pointer
    // is not a null one. The store now deletes its loaders up front, so the
    // ordering is no longer the only thing holding this up.
    QPointer<ShaderPresetRegistry> registry;
    ShaderFamily family;

    std::optional<PhosphorFsLoader::ParsedEntry> parseFile(const QString& filePath) override
    {
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            qCWarning(lcPresetLoader) << "Skipping preset file that will not open:" << filePath << file.errorString();
            return std::nullopt;
        }

        QJsonParseError parseError{};
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            qCWarning(lcPresetLoader) << "Skipping malformed preset file:" << filePath << parseError.errorString();
            return std::nullopt;
        }
        if (!doc.isObject()) {
            qCWarning(lcPresetLoader) << "Skipping preset file whose root is not an object:" << filePath;
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
            qCWarning(lcPresetLoader) << "Skipping preset file with no pack id:" << filePath;
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
                qCWarning(lcPresetLoader) << "commitBatch: payload type-mismatch for" << parsed.key;
                continue;
            }
            presets.append(*preset);
        }

        if (!registry) {
            qCWarning(lcPresetLoader) << "commitBatch: registry not set";
            return;
        }
        registry->setUserPresets(family, presets);
    }
};

ShaderPresetLoader::ShaderPresetLoader(ShaderPresetRegistry& registry, ShaderFamily family, QObject* parent)
    : QObject(parent)
    , m_sink(std::make_unique<Sink>(registry, family))
    , m_loader(std::make_unique<PhosphorFsLoader::DirectoryLoader>(*m_sink))
{
}

ShaderPresetLoader::~ShaderPresetLoader()
{
    // Hand the registry an empty set for this family, so tearing a loader down
    // retracts what it published rather than leaving presets behind that no
    // file backs any more. Sequential loaders (tests, a settings window opened
    // twice) therefore never inherit a prior loader's entries.
    //
    // The retraction is whole-family rather than owner-scoped the way
    // `CurveLoader`'s is, so it is only correct while a family has exactly one
    // publisher. `ShaderPresetStore::load()` is what guarantees that: it builds
    // one loader per family and refuses to build a second set. A caller that
    // constructs its own second loader for a family that already has one would
    // have the first one's teardown wipe the second one's presets.
    //
    // `registry` is a QPointer, so a registry already destroyed reads as null
    // here instead of being dereferenced.
    if (m_sink && !m_sink->registry.isNull()) {
        m_sink->registry->setUserPresets(m_sink->family, {});
    }
}

int ShaderPresetLoader::loadFromDirectory(const QString& directory, LiveReload liveReload)
{
    return m_loader->loadFromDirectory(directory, liveReload);
}

int ShaderPresetLoader::loadFromDirectories(const QStringList& directories, LiveReload liveReload,
                                            PhosphorFsLoader::RegistrationOrder order)
{
    return m_loader->loadFromDirectories(directories, liveReload, order);
}

void ShaderPresetLoader::requestRescan()
{
    m_loader->requestRescan();
}

void ShaderPresetLoader::rescanNow()
{
    m_loader->rescanNow();
}

ShaderFamily ShaderPresetLoader::family() const
{
    return m_sink->family;
}

} // namespace PhosphorShaders
