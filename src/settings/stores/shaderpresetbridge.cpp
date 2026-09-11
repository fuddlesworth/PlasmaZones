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

bool ShaderPresetBridge::canUsePresetName(const QString& name) const
{
    const QString trimmed = name.trimmed();
    return !trimmed.isEmpty() && trimmed.size() <= kMaxPresetNameChars;
}

QString ShaderPresetBridge::presetDirectory() const
{
    return m_store->directoryFor(m_family);
}

bool ShaderPresetBridge::commit(const PhosphorShaders::ShaderPreset& preset)
{
    const QString dir = presetDirectory();
    if (!QDir().mkpath(dir)) {
        const QString error = PhosphorI18n::tr("Could not create the preset folder.", "@info");
        qCWarning(lcConfig) << "ShaderPresetBridge: cannot create" << dir;
        Q_EMIT presetWriteFailed(error);
        return false;
    }

    const QString path = dir + QLatin1Char('/') + preset.id + QStringLiteral(".json");
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
    preset.id = QUuid::createUuid().toString();
    preset.name = name.trimmed();
    preset.packId = packId;
    preset.params = params;

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
    preset.params = params;
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
