// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/MetadataPackLoader.h>
#include <PhosphorRegistry/Registry.h>

#include <PhosphorShaders/ShaderEntryPoint.h>

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

namespace PhosphorAnimationShaders {

/**
 * @brief Registry of available animation shader transition effects.
 *
 * Discovers shader packs from configured search paths. Each pack is a
 * subdirectory containing a `metadata.json` describing the effect plus
 * the shader source files it references.
 *
 * ## Directory layout
 *
 * ```
 * <search-path>/
 *   dissolve/
 *     metadata.json    ← { "id": "dissolve", "fragmentShader": "effect.frag", ... }
 *     effect.frag
 *   slide/
 *     metadata.json
 *     effect.frag
 * ```
 *
 * The subdirectory name is decorative — the `id` field in metadata.json
 * is the registry key. This matches `PhosphorShaders::ShaderRegistry`'s
 * convention for zone shaders.
 *
 * ## Search paths and live reload
 *
 * Storage + change-notify is the generic `PhosphorRegistry::Registry<AnimationPack>`
 * (one AnimationPack wraps one discovered AnimationShaderEffect); the on-disk
 * scan + hot-reload is a `PhosphorRegistry::MetadataPackLoader<AnimationPack>`.
 * Search-path management (`addSearchPath`, `addSearchPaths`,
 * `searchPaths`, `setUserPath`, `refresh`) forwards to that loader. The first
 * `addSearchPath[s]` call with `LiveReload::On` (the default) installs a
 * `QFileSystemWatcher` with the standard 50 ms debounce, parent-watch
 * promotion for missing user-data dirs, per-file watches re-armed on
 * every rescan, and rescan-during-rescan race protection. Forbidden
 * roots (`$HOME`, `/`, XDG data/config/cache/temp/runtime, Documents,
 * Downloads) are refused.
 *
 * `effectsChanged` fires on every committed rescan (the loader's coarse
 * onCommitted hook) — i.e. whenever the discovered set or any watched-file
 * fingerprint differs from the previous scan, including any `metadata.json`
 * field edit or shader-source edit.
 *
 * ## Thread safety
 *
 * GUI-thread only for both reads and mutations. The pack map lives
 * inside the registry and is rebuilt on the GUI thread inside the
 * rescan; the public lookup methods read it without synchronisation.
 *
 * `searchPaths()` is the one exception: it returns a by-value snapshot
 * of an implicitly-shared QStringList, so a GUI-thread caller can
 * snapshot it and propagate the result to worker threads. Calling it
 * *from* a worker thread concurrently with a GUI-thread mutation is a
 * data race; snapshot on the GUI thread first.
 */
class PHOSPHORANIMATION_EXPORT AnimationShaderRegistry : public QObject
{
    Q_OBJECT

public:
    /// Registry entry: a discovered animation effect as a `PhosphorRegistry`
    /// factory. `Registry<AnimationPack>` keys on `id()`; `displayName()` is
    /// the effect's human name. Wraps the parsed `AnimationShaderEffect` by
    /// value (consumers read it back via `effect()`). Effects carry no
    /// capability metadata, so the `IFactoryBase` default `capabilities()`
    /// (`{}`) applies unchanged.
    class AnimationPack final : public PhosphorRegistry::IFactoryBase
    {
    public:
        explicit AnimationPack(AnimationShaderEffect effect)
            : m_effect(std::move(effect))
        {
        }
        [[nodiscard]] QString id() const override
        {
            return m_effect.id;
        }
        [[nodiscard]] QString displayName() const override
        {
            return m_effect.name;
        }
        [[nodiscard]] const AnimationShaderEffect& effect() const
        {
            return m_effect;
        }

    private:
        AnimationShaderEffect m_effect;
    };

    explicit AnimationShaderRegistry(QObject* parent = nullptr);
    ~AnimationShaderRegistry() override;

    // ── Search paths (forwarded to the internal MetadataPackLoader) ───
    void addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On);
    void addSearchPaths(
        const QStringList& paths, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On,
        PhosphorFsLoader::RegistrationOrder order = PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);
    [[nodiscard]] QStringList searchPaths() const;
    void setUserPath(const QString& path);
    void refresh();

    // Lookup -------------------------------------------------------------------

    QList<AnimationShaderEffect> availableEffects() const;
    AnimationShaderEffect effect(const QString& id) const;
    bool hasEffect(const QString& id) const;
    QStringList effectIds() const;

    /// Translate a friendly parameter map into the canonical slot keys
    /// consumed by both runtimes that drive animation shaders. Two
    /// independent allocators advance in metadata declaration order:
    ///
    ///   • Float / int / bool parameters fill `customParams<N>_<x|y|z|w>`
    ///     — the first declared param lands in `customParams1_x`, the
    ///     second in `customParams1_y`, …, the fifth in `customParams2_x`,
    ///     up to 32 slots (`AnimationShaderContract::kMaxParameterSlots`).
    ///
    ///   • Color parameters fill `customColor<N>` — the first declared
    ///     color lands in `customColor1`, the second in `customColor2`,
    ///     up to 16 slots (`AnimationShaderContract::kMaxCustomColors`).
    ///     Color values are coerced to `QColor` at this boundary —
    ///     anything `QColor::QColor(QString)` accepts is accepted here
    ///     too (the common cases are `"#rgb"`, `"#rrggbb"`, and
    ///     `"#aarrggbb"` with alpha FIRST per Qt's convention; the
    ///     parser also recognises higher-bit-depth `"#rrrgggbbb"` /
    ///     `"#rrrrggggbbbb"`, SVG colour names like `"red"`, and the
    ///     `"transparent"` keyword) — alongside `QColor` instances.
    ///     Everything else falls back to the declared default, then
    ///     `Qt::transparent`. CSS-style `"#rrggbbaa"` (alpha LAST, 9
    ///     chars) is NOT accepted — any 9-char hex string is
    ///     ambiguous between Qt-form and CSS-form, so a rewrite
    ///     would silently corrupt configs that already use Qt's
    ///     order; settings UI / config writers MUST emit Qt-form.
    ///
    /// The two allocators advance independently — a color parameter does
    /// NOT consume a `customParams` sub-slot, so a `[color, float]`
    /// declaration produces `customColor1` + `customParams1_x`, not
    /// `customColor1` + `customParams1_y`.
    ///
    /// Bool values become `0.0` or `1.0`. Missing float keys fall back
    /// to the metadata-declared default, then to `0.0`. Unknown parameter
    /// ids in `friendlyParams` are silently ignored. Slot-budget overflows
    /// log a warning and drop the offending parameter.
    ///
    /// Returns an empty map when `effectId` resolves to no registered
    /// effect or when the effect declares no parameters.
    QVariantMap translateAnimationParams(const QString& effectId, const QVariantMap& friendlyParams) const;

    /// Static counterpart for callers that already hold the resolved
    /// `AnimationShaderEffect` (e.g. the kwin-effect's per-frame
    /// translation). Same slot-allocation contract as the registry-keyed
    /// overload.
    static QVariantMap translateAnimationParams(const AnimationShaderEffect& effect, const QVariantMap& friendlyParams);

    /// Build the generated `#define p_<id> <glsl-accessor>` preamble for
    /// @p effect's declared parameters so authors read a parameter by name
    /// (`p_speed`) instead of hand-decoding a `customParams[N].xyzw` lane.
    ///
    /// The slot allocation mirrors `translateAnimationParams` exactly — color
    /// params fill `customColors[N]`, everything else fills
    /// `customParams[N].<xyzw>`, both in metadata declaration order — so the
    /// macro a shader reads resolves to the same UBO lane the value is
    /// uploaded to. Both runtimes splice the result after the shader's
    /// `#version` line (via `PhosphorShaders::spliceAfterVersion`). Returns an
    /// empty string when the effect declares no parameters.
    static QString paramPreamble(const AnimationShaderEffect& effect);

    /// The T1.4/T1.5 entry-point prologue for animation shaders: `#version`,
    /// the `animation_uniforms.glsl` include (uniforms + `legProgress()` /
    /// `p_reversed` / `surfaceColor()`), and the `vTexCoord` in / `fragColor`
    /// out. Prepended to an entry-only pack before include expansion; identical
    /// on both runtimes (the include's `#ifdef PLASMAZONES_KWIN` branch handles
    /// the binding difference).
    static QString animationEntryPrologue();

    /// The direction-dispatched entry candidates (T1.5), in priority order:
    ///   • `vec4 pTransition(vec2 uv, float t)` — symmetric; the harness keeps
    ///     the runtime's `iTime` flip so it auto-mirrors (`t` is raw `iTime`).
    ///   • `vec4 pIn(vec2,float)` + `vec4 pOut(vec2,float)` — asymmetric; the
    ///     harness applies `legProgress()` (so `t` is always forward 0→1) and
    ///     calls the right one by `p_reversed`. Requires BOTH to be defined.
    /// A pack defining its own `main()` is passed through. Install via
    /// `ShaderEffect::setEntryScaffold` (daemon) and apply via
    /// `PhosphorShaders::assembleEntryPoint` (kwin) so both runtimes agree.
    static QList<PhosphorShaders::EntryCandidate> animationEntryCandidates();

Q_SIGNALS:
    void effectsChanged();

private:
    // Generic id-keyed storage + change-notify for the discovered effect
    // packs. m_loader (below) populates it from disk; the lookup methods
    // read it. Declared before m_loader so it is destroyed AFTER the loader:
    // the loader holds a borrowed Registry pointer (used during live rescans
    // in reconcile(), not at teardown), which must stay valid for the loader's
    // whole lifetime.
    PhosphorRegistry::Registry<AnimationPack> m_registry;
    // On-disk scan + hot-reload. Parses each pack's metadata.json into an
    // AnimationPack and reconciles m_registry; its onCommitted hook
    // re-emits effectsChanged.
    std::unique_ptr<PhosphorRegistry::MetadataPackLoader<AnimationPack>> m_loader;
};

} // namespace PhosphorAnimationShaders
