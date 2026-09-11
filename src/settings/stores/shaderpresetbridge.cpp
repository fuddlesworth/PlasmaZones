// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "shaderpresetbridge.h"

#include "core/platform/logging.h"
#include "phosphor_i18n.h"

#include <PhosphorShaders/ShaderPresetLoader.h>
#include <PhosphorShaders/ShaderPresetRegistry.h>
#include <PhosphorShaders/ShaderPresetStore.h>

#include <QDir>
#include <QLoggingCategory>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

namespace PlasmaZones {

namespace {
/// A display name is a label, not a path or an id. Well above anything a user
/// would type and far below the cost of letting an unbounded string reach disk.
constexpr int kMaxPresetNameChars = 128;

/// Caps on a preset's parameter map, mirroring the assignment trees' schema
/// bounds. A preset file is read back and merged into an assignment's effective
/// values, so it is the same input boundary the animation writer bounds its map
/// at for the same stated reason: persisted close to verbatim, and copied back in
/// without validation on read.
constexpr int kMaxPresetParams = 64;
constexpr int kMaxPresetStringChars = 1024;

/// @p in with over-long keys and values dropped, non-scalar values dropped, and
/// the whole map capped. Every parameter type a pack can declare is one scalar; a
/// map or a list is nesting no pack produces.
QVariantMap boundedPresetParams(const QVariantMap& in)
{
    QVariantMap out;
    for (auto it = in.cbegin(); it != in.cend(); ++it) {
        if (out.size() >= kMaxPresetParams) {
            break;
        }
        if (it.key().size() > kMaxPresetStringChars) {
            continue;
        }
        const int type = it.value().typeId();
        if (type == QMetaType::QVariantMap || type == QMetaType::QVariantList) {
            continue;
        }
        if (type == QMetaType::QString && it.value().toString().size() > kMaxPresetStringChars) {
            continue;
        }
        out.insert(it.key(), it.value());
    }
    return out;
}

void fillRow(const PhosphorShaders::ShaderPreset& preset, QVariantMap& out)
{
    out.insert(QStringLiteral("id"), preset.id);
    out.insert(QStringLiteral("name"), preset.name);
    out.insert(QStringLiteral("readOnly"), preset.readOnly);
}
} // namespace

ShaderPresetBridge::ShaderPresetBridge(PhosphorShaders::ShaderPresetStore& store, PhosphorShaders::ShaderFamily family,
                                       QObject* parent)
    : QObject(parent)
    , m_store(&store)
    , m_family(family)
{
    // Relayed rather than re-derived, so a preset edited in ANOTHER process
    // (the daemon's copy, a text editor) reaches the open settings window the
    // same way one edited here does. Filtered to this bridge's family, since a
    // decoration editor has no use for a pointer preset changing.
    connect(&store.registry(), &PhosphorShaders::ShaderPresetRegistry::presetsChanged, this,
            [this](PhosphorShaders::ShaderFamily changed, const QString& packId) {
                if (changed == m_family) {
                    Q_EMIT presetsChanged(packId);
                }
            });
}

ShaderPresetBridge::~ShaderPresetBridge() = default;

QVariantList ShaderPresetBridge::presetsFor(const QString& packId) const
{
    QVariantList rows;
    const QList<PhosphorShaders::ShaderPreset> presets = m_store->registry().presetsFor(m_family, packId);
    rows.reserve(presets.size());
    for (const PhosphorShaders::ShaderPreset& preset : presets) {
        QVariantMap row;
        fillRow(preset, row);
        rows.append(row);
    }
    return rows;
}

QVariantMap ShaderPresetBridge::presetParams(const QString& packId, const QString& presetId) const
{
    return m_store->registry().preset(m_family, packId, presetId).params;
}

QVariantMap ShaderPresetBridge::effectiveParams(const QString& packId, const QString& presetId,
                                                const QVariantMap& deltas) const
{
    return m_store->registry().resolveParams(m_family, packId, presetId, deltas);
}

bool ShaderPresetBridge::canUsePresetName(const QString& name) const
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > kMaxPresetNameChars) {
        return false;
    }
    // Content, not just length. The name is rendered in the picker combo and the
    // rename dialog, so a newline, a control character or a bidi override would
    // mangle a row rather than merely look odd — and this is the one validation the
    // dialog's Ok button gates on.
    for (const QChar ch : trimmed) {
        if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format) {
            return false;
        }
    }
    return true;
}

QString ShaderPresetBridge::presetDirectory() const
{
    return m_store->directoryFor(m_family);
}

bool ShaderPresetBridge::commit(const PhosphorShaders::ShaderPreset& preset)
{
    // Guard the id here as well as at the parse boundary. The id reaching this
    // point came from the registry, which means it came from a file the user can
    // hand-edit, and the next line concatenates it into a filesystem path — so
    // `"id": "../../../../.config/plasmazones/config"` would have written preset
    // JSON over an arbitrary file the moment the user pressed Rename. The
    // library refuses such an id on load now, but this bridge must not depend on
    // an invariant it cannot see, the same reason acceptableShaderEffectId()
    // exists alongside the schema's own bound.
    if (!PhosphorShaders::ShaderPreset::isUsableId(preset.id)) {
        const QString error = PhosphorI18n::tr("That preset has an unusable name on disk.", "@info");
        qCWarning(lcConfig) << "ShaderPresetBridge: refusing to write a preset whose id is not a safe path component:"
                            << preset.id;
        Q_EMIT presetWriteFailed(error);
        return false;
    }

    // An UPDATE or RENAME must not RE-CREATE a preset that is already gone. Each
    // settings window owns its own store, so after another window deletes a preset
    // this one still holds it until its watcher fires — and writing then silently
    // resurrected the file, undoing the delete while keeping the id, so every
    // assignment snapped back to it.
    if (!preset.sourcePath.isEmpty() && !QFile::exists(preset.sourcePath)) {
        const QString error = PhosphorI18n::tr("That preset no longer exists.", "@info");
        qCWarning(lcConfig) << "ShaderPresetBridge: refusing to re-create a deleted preset" << preset.id;
        Q_EMIT presetWriteFailed(error);
        return false;
    }

    const QString dir = presetDirectory();
    if (!QDir().mkpath(dir)) {
        const QString error = PhosphorI18n::tr("Could not create the preset folder.", "@info");
        qCWarning(lcConfig) << "ShaderPresetBridge: cannot create" << dir;
        Q_EMIT presetWriteFailed(error);
        return false;
    }

    // Prefer the file the record actually came from. The loader documents a
    // hand-written file whose id comes from the filename STEM, and fromJson uses
    // the stem only when the `id` field is absent — so `my-preset.json` carrying
    // `"id": "abc"` is legal, and deriving the path from the id alone wrote a
    // second file named after the id while leaving the original in place. The next
    // rescan then saw two files claiming one id, and which won came down to the
    // loader's dedup order.
    //
    // Containment-checked first: sourcePath is loader-stamped, but a symlink in the
    // preset directory pointing out of it would otherwise let QSaveFile follow it.
    QString path = dir + QLatin1Char('/') + preset.id + QStringLiteral(".json");
    if (!preset.sourcePath.isEmpty()) {
        const QString canonicalDir = QFileInfo(dir).canonicalFilePath();
        const QString canonicalSource = QFileInfo(preset.sourcePath).canonicalFilePath();
        if (!canonicalDir.isEmpty() && !canonicalSource.isEmpty()
            && canonicalSource.startsWith(canonicalDir + QLatin1Char('/'))) {
            path = preset.sourcePath;
        } else {
            qCWarning(lcConfig)
                << "ShaderPresetBridge: preset source path is outside the preset directory, writing by id"
                << preset.sourcePath;
        }
    }
    // QSaveFile so a crash mid-write cannot leave a truncated preset behind
    // that the loader then refuses on every later scan.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        const QString error = PhosphorI18n::tr("Could not write the preset to disk.", "@info");
        qCWarning(lcConfig) << "ShaderPresetBridge: cannot open" << path << file.errorString();
        Q_EMIT presetWriteFailed(error);
        return false;
    }
    const QByteArray json = QJsonDocument(preset.toJson()).toJson(QJsonDocument::Indented);
    if (file.write(json) != json.size() || !file.commit()) {
        const QString error = PhosphorI18n::tr("Could not write the preset to disk.", "@info");
        qCWarning(lcConfig) << "ShaderPresetBridge: write failed for" << path << file.errorString();
        Q_EMIT presetWriteFailed(error);
        return false;
    }

    // Synchronous, not the debounced requestRescan: the caller is about to
    // select what it just saved, and a picker that does not yet list it would
    // silently select nothing.
    if (auto* loader = m_store->loader(m_family)) {
        loader->rescanNow();
    }
    return true;
}

QString ShaderPresetBridge::savePreset(const QString& packId, const QString& name, const QVariantMap& params)
{
    if (packId.isEmpty()) {
        Q_EMIT presetWriteFailed(PhosphorI18n::tr("No shader pack selected.", "@info"));
        return {};
    }
    if (!canUsePresetName(name)) {
        Q_EMIT presetWriteFailed(PhosphorI18n::tr("Enter a name for the preset.", "@info"));
        return {};
    }

    PhosphorShaders::ShaderPreset preset;
    // A fresh UUID, never derived from the name: renaming must not break the
    // assignments pointing at this preset, so the identity cannot come from
    // anything the user can edit.
    //
    // WithoutBraces because this id is also the filename `commit()` builds, and
    // CLAUDE.md reserves the braced spelling for everything that is not a
    // filesystem path. It also makes the id equal the file's stem, so the
    // loader's stem fallback and the declared `id` field cannot disagree.
    preset.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    preset.name = name.trimmed();
    preset.packId = packId;
    preset.params = boundedPresetParams(params);

    return commit(preset) ? preset.id : QString();
}

bool ShaderPresetBridge::updatePreset(const QString& presetId, const QVariantMap& params)
{
    // By id, not pack-scoped: the caller acts on a preset the user picked and
    // the pack comes back with the record. A read-only one belongs to its pack
    // and cannot be written here.
    PhosphorShaders::ShaderPreset preset = m_store->registry().presetById(m_family, presetId);
    if (!preset.isValid()) {
        Q_EMIT presetWriteFailed(PhosphorI18n::tr("That preset no longer exists.", "@info"));
        return false;
    }
    if (preset.readOnly) {
        Q_EMIT presetWriteFailed(PhosphorI18n::tr("This preset comes with its pack and cannot be changed.", "@info"));
        return false;
    }
    preset.params = boundedPresetParams(params);
    return commit(preset);
}

bool ShaderPresetBridge::renamePreset(const QString& presetId, const QString& name)
{
    if (!canUsePresetName(name)) {
        Q_EMIT presetWriteFailed(PhosphorI18n::tr("Enter a name for the preset.", "@info"));
        return false;
    }
    PhosphorShaders::ShaderPreset preset = m_store->registry().presetById(m_family, presetId);
    if (!preset.isValid()) {
        Q_EMIT presetWriteFailed(PhosphorI18n::tr("That preset no longer exists.", "@info"));
        return false;
    }
    if (preset.readOnly) {
        Q_EMIT presetWriteFailed(PhosphorI18n::tr("This preset comes with its pack and cannot be renamed.", "@info"));
        return false;
    }
    preset.name = name.trimmed();
    // The file is named after the id, so a rename rewrites it in place rather
    // than moving it. Nothing pointing at this preset notices.
    return commit(preset);
}

bool ShaderPresetBridge::deletePreset(const QString& presetId)
{
    const PhosphorShaders::ShaderPreset preset = m_store->registry().presetById(m_family, presetId);
    if (!preset.isValid()) {
        return false;
    }
    if (preset.readOnly) {
        Q_EMIT presetWriteFailed(PhosphorI18n::tr("This preset comes with its pack and cannot be deleted.", "@info"));
        return false;
    }
    // A file that is already gone is a delete that already happened — another
    // window, or a text editor. Reporting failure for it told the user something
    // went wrong when the end state is exactly what they asked for, and skipped the
    // rescan, so the stale row lingered until the watcher fired.
    if (!preset.sourcePath.isEmpty() && !QFile::exists(preset.sourcePath)) {
        if (auto* loader = m_store->loader(m_family)) {
            loader->rescanNow();
        }
        return true;
    }
    if (preset.sourcePath.isEmpty() || !QFile::remove(preset.sourcePath)) {
        const QString error = PhosphorI18n::tr("Could not delete the preset.", "@info");
        qCWarning(lcConfig) << "ShaderPresetBridge: cannot remove" << preset.sourcePath;
        Q_EMIT presetWriteFailed(error);
        return false;
    }
    if (auto* loader = m_store->loader(m_family)) {
        loader->rescanNow();
    }
    return true;
}

QString ShaderPresetBridge::duplicatePreset(const QString& packId, const QString& presetId, const QString& name)
{
    const PhosphorShaders::ShaderPreset source = m_store->registry().preset(m_family, packId, presetId);
    if (!source.isValid()) {
        Q_EMIT presetWriteFailed(PhosphorI18n::tr("That preset no longer exists.", "@info"));
        return {};
    }
    return savePreset(source.packId, name, source.params);
}

} // namespace PlasmaZones
