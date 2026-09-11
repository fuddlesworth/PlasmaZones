// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/ShaderPreset.h>
#include <PhosphorShaders/ShaderPresetParse.h>
#include <PhosphorShaders/phosphorshaders_export.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace PhosphorShaders {

/**
 * @brief The one place a `presetId` on an assignment turns into parameters.
 *
 * Holds both provenances behind a single lookup, so a consumer resolving an
 * assignment never has to care whether the preset came from a pack's
 * `metadata.json` or from a file the user saved:
 *
 *   • Pack-declared presets arrive via `setPackPresets`, called by whoever owns
 *     the family's pack registry whenever that registry reloads (the existing
 *     `effectsChanged` / `shadersChanged` edge).
 *   • User presets arrive via `setUserPresets`, called by `ShaderPresetLoader`
 *     on each rescan of the user preset directories.
 *
 * On a collision the USER preset wins, matching the user-wins layering every
 * other Phosphor loader applies to a user file shadowing a system one.
 *
 * ## Why this lives in phosphor-shaders
 *
 * All three consumers resolve presets: the settings app (to show and edit
 * them), the daemon (overlay and OSD surfaces), and the KWin effect (window
 * animations, decorations, pointer). The effect has no access to the config
 * file — it pulls the assignment trees from the daemon over D-Bus — but it
 * DOES read pack and data directories from disk itself, exactly as it already
 * does for pack registries. Putting the store here, in the one library all
 * three already link, lets every process resolve a preset by the same rule
 * from the same files, instead of inventing a second flattened wire shape.
 *
 * ## Thread safety
 *
 * GUI-thread only, like the pack registries it sits beside.
 */
class PHOSPHORSHADERS_EXPORT ShaderPresetRegistry : public QObject
{
    Q_OBJECT

public:
    explicit ShaderPresetRegistry(QObject* parent = nullptr);
    ~ShaderPresetRegistry() override;

    // ─────── Lookup ───────

    /// Every preset offered for @p packId in @p family, user presets first,
    /// each group sorted by display name so the picker order is stable across
    /// restarts (a QHash iteration order is not).
    QList<ShaderPreset> presetsFor(ShaderFamily family, const QString& packId) const;

    /// The preset @p presetId names, or an invalid preset when it names none.
    ///
    /// A MISS IS NORMAL, not an error: an assignment can outlive the preset it
    /// points at (the user deleted it, or a pack update dropped it). Callers
    /// resolve a miss to the pack's declared defaults plus the assignment's own
    /// deltas, which is the same look the assignment would have had with no
    /// preset — degraded, never broken.
    ShaderPreset preset(ShaderFamily family, const QString& packId, const QString& presetId) const;

    /// The preset @p presetId names, searched across every pack in @p family.
    ///
    /// Ids are unique within a family (a user preset is a UUID, a pack-declared
    /// one is namespaced by its pack), so the pack is recoverable from the
    /// preset rather than required to find it. The WRITE side needs this: it
    /// acts on a preset the user picked by id and has to learn which pack it
    /// belongs to in order to write it back.
    ///
    /// Prefer the pack-scoped `preset()` for RESOLUTION, which must not match a
    /// preset belonging to a different pack than the assignment resolved to.
    ShaderPreset presetById(ShaderFamily family, const QString& presetId) const;

    /// The effective parameter map for an assignment: the preset's parameters
    /// overlaid with @p deltas, the assignment's own edits.
    ///
    /// With an empty @p presetId, or one naming no preset, this is just
    /// @p deltas — so a consumer can route EVERY assignment through here and
    /// stop special-casing "has a preset".
    QVariantMap resolveParams(ShaderFamily family, const QString& packId, const QString& presetId,
                              const QVariantMap& deltas) const;

    // ─────── Population ───────

    /// Replace every pack-declared preset for @p packId in @p family.
    ///
    /// Called on the pack registry's reload edge. Emits `presetsChanged` only
    /// when the resulting set actually differs, because that signal drops the
    /// compiled-pack caches in the compositor and a reload that changed nothing
    /// must not cost a recompile.
    void setPackPresets(ShaderFamily family, const QString& packId, const PackPresets& presets);

    /// Replace every user preset in @p family with @p presets.
    ///
    /// Whole-family replace rather than per-file update, because that is the
    /// shape `DirectoryLoader` commits in: one batch per rescan, carrying every
    /// file currently on disk. Emits `presetsChanged` once per pack whose set
    /// actually changed, so one rescan that touched one file does not
    /// invalidate every pack's compiled shaders.
    void setUserPresets(ShaderFamily family, const QList<ShaderPreset>& presets);

Q_SIGNALS:
    /// The presets offered for (@p family, @p packId) changed: one was added,
    /// removed, renamed, or retuned.
    ///
    /// Consumers hang re-resolution off this. For the compositor that means the
    /// same handler `effectsChanged` already uses, since a retuned preset
    /// changes parameters that are baked into a compiled surface pack.
    void presetsChanged(PhosphorShaders::ShaderFamily family, const QString& packId);

private:
    /// (family, packId) -> presetId -> preset. Two maps rather than one so a
    /// pack reload cannot disturb user presets and a preset-file rescan cannot
    /// disturb pack-declared ones.
    QHash<QString, QHash<QString, ShaderPreset>> m_packDeclared;
    QHash<QString, QHash<QString, ShaderPreset>> m_userDefined;

    static QString scopeKey(ShaderFamily family, const QString& packId);
    QList<ShaderPreset> mergedFor(ShaderFamily family, const QString& packId) const;
};

} // namespace PhosphorShaders

Q_DECLARE_METATYPE(PhosphorShaders::ShaderFamily)
