// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorShaders/ShaderPreset.h>
#include <PhosphorShaders/ShaderPresetParse.h>
#include <PhosphorShaders/phosphorshaders_export.h>

#include <QHash>
#include <QSet>
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
 *   • User presets arrive via `setUserPresets`, called by `ShaderPresetStore`
 *     on each rescan of the user preset directories.
 *
 * On a collision the USER preset wins, matching the user-wins layering every
 * other Phosphor loader applies to a user file shadowing a system one.
 *
 * ## Why this lives in phosphor-shaders
 *
 * All four consumers resolve presets: the settings app (to show and edit them),
 * the daemon (overlay and OSD surfaces), the KWin effect (window animations,
 * decorations, pointer) and the shell (its own surface chrome). The effect has no access to the config
 * file — it pulls the assignment trees from the daemon over D-Bus — but it
 * DOES read pack and data directories from disk itself, exactly as it already
 * does for pack registries. Putting the store here, in the one library all
 * four already link, lets every process resolve a preset by the same rule
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
    /// Ids are MEANT to be unique within a family — the settings app mints a
    /// UUID for each user preset, and a pack-declared id is namespaced by its
    /// pack. Nothing enforces it, though: a user preset's id comes from a
    /// hand-editable file, so two packs can carry the same id. The search is
    /// therefore over sorted keys, so the answer is at least the SAME one on
    /// every run rather than whichever bucket QHash happened to yield first;
    /// `setUserPresets` warns when it sees a duplicate.
    ///
    /// The WRITE side needs this: it acts on a preset the user picked by id and
    /// has to learn which pack it belongs to in order to write it back.
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
    ///
    /// Every value returned is clamped to the pack's declared range when the
    /// seeding consumer supplied one (see `setPackPresets`). This is the single
    /// funnel preset values leave by, so clamping here covers a pack-declared
    /// preset, a hand-written user preset file and an assignment's own deltas
    /// alike. Unclamped, any of the three could drive a pack's GLSL loop bound
    /// past what the shader can afford.
    QVariantMap resolveParams(ShaderFamily family, const QString& packId, const QString& presetId,
                              const QVariantMap& deltas) const;

    // ─────── Population ───────

    /// Replace every pack-declared preset for @p packId in @p family.
    ///
    /// Called on the pack registry's reload edge. Emits `presetsChanged` only
    /// when the resulting set actually differs, because that signal drops the
    /// compiled-pack caches in the compositor and a reload that changed nothing
    /// must not cost a recompile.
    ///
    /// @p bounds are the pack's declared parameter ranges, which `resolveParams`
    /// clamps every value to. Build it with `presetBoundsFrom(info.parameters)`
    /// at the call site — the caller already has the declared parameter list,
    /// and this library deliberately does not depend on the four pack
    /// registries. Passing an empty map leaves that pack's values unclamped.
    void setPackPresets(ShaderFamily family, const QString& packId, const PackPresets& presets,
                        const PresetValueBounds& bounds = {});

    /// Replace every pack-declared preset for EVERY pack in @p family at once.
    ///
    /// Prefer this over looping `setPackPresets`, because a loop driven by the
    /// packs that currently exist can never name a pack that has GONE: it
    /// simply does not visit it, so an uninstalled pack's presets survive for
    /// the process lifetime and keep being offered by `presetsFor` and returned
    /// by `presetById`. This overload diffs against everything already held for
    /// the family, so a vanished pack is retracted by construction — the same
    /// property `setUserPresets` has, and for the same reason.
    ///
    /// Emits `presetsChanged` once per pack whose set actually changed.
    void setPackPresetsForFamily(ShaderFamily family, const QHash<QString, PackPresets>& byPackId,
                                 const QHash<QString, PresetValueBounds>& boundsByPackId = {});

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
    /// (family, packId) -> the pack's declared parameter ranges, which
    /// `resolveParams` clamps to. Seeded beside the pack-declared presets,
    /// because the consumer doing that already holds the declarations.
    QHash<QString, PresetValueBounds> m_packBounds;

    /// "<family>/<id>" for every duplicate preset id already reported, so the
    /// warning is once per clash rather than once per rescan. The watcher rescans
    /// a family on every save and on every external edit, so without this one
    /// hand-written duplicate produced an identical line for the life of the
    /// process and buried everything else in the log. Pruned per family on each
    /// batch to whatever still clashes, so resolving a clash and re-introducing it
    /// warns again.
    QSet<QString> m_reportedIdClashes;

    static QString scopeKey(ShaderFamily family, const QString& packId);
    QList<ShaderPreset> mergedFor(ShaderFamily family, const QString& packId) const;
    /// Apply one pack's declared ranges to @p values in place.
    void clampToBounds(const QString& key, QVariantMap& values) const;
    /// Replace one pack's bucket, returning whether anything changed. Shared by
    /// the per-pack and whole-family seeding paths so they cannot diverge.
    bool applyPackBucket(const QString& key, const QString& packId, const PackPresets& presets,
                         const PresetValueBounds& bounds);
};

/// Seed @p registry with every pack-declared preset in @p effects, for @p family.
///
/// The projection every consumer of this library performed by hand. Three
/// processes each wrote the same loop — build `{packId -> presets}` and
/// `{packId -> bounds}` from a pack registry's effect list, then whole-family
/// replace — and the daemon wrote it three more times, once per family, so a
/// fourth family meant a fourth copy. A fix to the projection had to land in
/// every one of them.
///
/// A template because the four families each keep their own effect type in their
/// own library; all four carry `id`, `presets` and `parameters`, which is all
/// this reads. That is also why it lives here rather than on the STORE: the store
/// deliberately knows nothing about the four pack registries, and this knows
/// nothing about them either — only about the three fields their effects share.
///
/// Whole-family replace, not a per-pack loop, and that is the point: a loop over
/// the packs that still exist cannot name one that has GONE, so an uninstalled
/// pack's presets would survive for the process lifetime. The declared parameter
/// ranges ride along, because `resolveParams` clamps to them, which is what keeps
/// a hand-written preset value out of a pack's GLSL loop bound unchecked.
template<typename EffectList>
void seedPackPresets(ShaderPresetRegistry& registry, ShaderFamily family, const EffectList& effects)
{
    QHash<QString, PackPresets> byPack;
    QHash<QString, PresetValueBounds> bounds;
    for (const auto& effect : effects) {
        byPack.insert(effect.id, effect.presets);
        bounds.insert(effect.id, presetBoundsFrom(effect.parameters));
    }
    registry.setPackPresetsForFamily(family, byPack, bounds);
}

} // namespace PhosphorShaders

Q_DECLARE_METATYPE(PhosphorShaders::ShaderFamily)
