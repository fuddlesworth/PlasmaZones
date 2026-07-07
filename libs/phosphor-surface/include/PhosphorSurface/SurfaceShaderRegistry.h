// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorSurface/SurfaceShaderEffect.h>
#include <PhosphorSurface/phosphorsurface_export.h>

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

namespace PhosphorSurfaceShaders {

/**
 * @brief Registry of available surface shader effects.
 *
 * Discovers shader packs from configured search paths. Each pack is a
 * subdirectory containing a `metadata.json` describing the effect plus
 * the shader source files it references.
 *
 * ## Directory layout
 *
 * ```
 * <search-path>/
 *   border/
 *     metadata.json    ← { "id": "border", "fragmentShader": "effect.frag", ... }
 *     effect.frag
 *   rounded-corners/
 *     metadata.json
 *     effect.frag
 * ```
 *
 * The subdirectory name is decorative — the `id` field in metadata.json
 * is the registry key. This matches `PhosphorShaders::ShaderRegistry`'s
 * and `AnimationShaderRegistry`'s convention.
 *
 * ## Search paths and live reload
 *
 * Storage + change-notify is the generic `PhosphorRegistry::Registry<SurfacePack>`
 * (one SurfacePack wraps one discovered SurfaceShaderEffect); the on-disk
 * scan + hot-reload is a `PhosphorRegistry::MetadataPackLoader<SurfacePack>`.
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
class PHOSPHORSURFACE_EXPORT SurfaceShaderRegistry : public QObject
{
    Q_OBJECT

public:
    /// Registry entry: a discovered surface effect as a `PhosphorRegistry`
    /// factory. `Registry<SurfacePack>` keys on `id()`; `displayName()` is
    /// the effect's human name. Wraps the parsed `SurfaceShaderEffect` by
    /// value (consumers read it back via `effect()`). Effects carry no
    /// capability metadata, so the `IFactoryBase` default `capabilities()`
    /// (`{}`) applies unchanged.
    class SurfacePack final : public PhosphorRegistry::IFactoryBase
    {
    public:
        explicit SurfacePack(SurfaceShaderEffect effect)
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
        [[nodiscard]] const SurfaceShaderEffect& effect() const
        {
            return m_effect;
        }

    private:
        SurfaceShaderEffect m_effect;
    };

    explicit SurfaceShaderRegistry(QObject* parent = nullptr);
    ~SurfaceShaderRegistry() override;

    // ── Search paths (forwarded to the internal MetadataPackLoader) ───
    void addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On);
    void addSearchPaths(
        const QStringList& paths, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On,
        PhosphorFsLoader::RegistrationOrder order = PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);
    [[nodiscard]] QStringList searchPaths() const;
    void setUserPath(const QString& path);
    void refresh();

    // Lookup -------------------------------------------------------------------

    QList<SurfaceShaderEffect> availableEffects() const;
    SurfaceShaderEffect effect(const QString& id) const;
    bool hasEffect(const QString& id) const;
    QStringList effectIds() const;

    /// Translate a friendly parameter map into the canonical slot keys
    /// consumed by both runtimes that drive surface shaders. Two
    /// independent allocators advance in metadata declaration order:
    ///
    ///   • Float / int / bool parameters fill `customParams<N>_<x|y|z|w>`
    ///     — the first declared param lands in `customParams1_x`, the
    ///     second in `customParams1_y`, …, the fifth in `customParams2_x`,
    ///     up to 32 slots (`SurfaceShaderContract::kMaxParameterSlots`).
    ///
    ///   • Color parameters fill `customColor<N>` — the first declared
    ///     color lands in `customColor1`, the second in `customColor2`,
    ///     up to 16 slots (`SurfaceShaderContract::kMaxCustomColors`).
    ///     Color values are coerced to `QColor` at this boundary —
    ///     anything `QColor::QColor(QString)` accepts is accepted here
    ///     too (the common cases are `"#rgb"`, `"#rrggbb"`, and
    ///     `"#aarrggbb"` with alpha FIRST per Qt's convention) —
    ///     alongside `QColor` instances. Everything else falls back to
    ///     the declared default, then `Qt::transparent`. CSS-style
    ///     `"#rrggbbaa"` (alpha LAST, 9 chars) is NOT accepted — any
    ///     9-char hex string is ambiguous between Qt-form and CSS-form,
    ///     so a rewrite would silently corrupt configs that already use
    ///     Qt's order; settings UI / config writers MUST emit Qt-form.
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
    /// Returns an empty map only when `effect` is invalid. A valid effect
    /// with no declared parameters can still return user-texture keys
    /// (`uTextureN`) when its metadata declares textures or `friendlyParams`
    /// supplies texture overrides, so callers must not treat a non-empty
    /// result as proof the effect declares parameters.
    QVariantMap translateSurfaceParams(const QString& effectId, const QVariantMap& friendlyParams) const;

    /// Static counterpart for callers that already hold the resolved
    /// `SurfaceShaderEffect` (e.g. the kwin-effect's per-frame
    /// translation). Same slot-allocation contract as the registry-keyed
    /// overload.
    static QVariantMap translateSurfaceParams(const SurfaceShaderEffect& effect, const QVariantMap& friendlyParams);

    /// Build the generated `#define p_<id> <glsl-accessor>` preamble for
    /// @p effect's declared parameters so authors read a parameter by name
    /// (`p_glow`) instead of hand-decoding a `customParams[N].xyzw` lane.
    ///
    /// The slot allocation mirrors `translateSurfaceParams` exactly — color
    /// params fill `customColors[N]`, everything else fills
    /// `customParams[N].<xyzw>`, both in metadata declaration order — so the
    /// macro a shader reads resolves to the same UBO lane the value is
    /// uploaded to. Both runtimes splice the result after the shader's
    /// `#version` line (via `PhosphorShaders::spliceAfterVersion`). Returns an
    /// empty string when the effect declares no parameters.
    static QString paramPreamble(const SurfaceShaderEffect& effect);

    /// Entry-point scaffold, matching the overlay (`pZone`/`pImage`) and
    /// animation (`pTransition`/`pIn`+`pOut`) categories. A pack may define
    /// `vec4 pSurface(vec2 uv)` and omit `main()`; the harness generates
    /// `void main() { fragColor = pSurface(vTexCoord); }` and prepends the
    /// prologue (`#version`, `#include <surface_lib.glsl>`, the vertex-in /
    /// fragColor-out declarations). A pack that writes its own `main()` is
    /// passed through unchanged. Shared by the daemon (via
    /// ShaderEffect::setEntryScaffold) and the kwin-effect / validator paths
    /// (via PhosphorShaders::assembleEntryPoint) so all three compile the
    /// identical source.
    static QString surfaceEntryPrologue();
    static QList<PhosphorShaders::EntryCandidate> surfaceEntryCandidates();

Q_SIGNALS:
    void effectsChanged();

private:
    // Generic id-keyed storage + change-notify for the discovered effect
    // packs. m_loader (below) populates it from disk; the lookup methods
    // read it. Declared before m_loader so it is destroyed AFTER the loader:
    // the loader holds a borrowed Registry pointer (used during live rescans
    // in reconcile(), not at teardown), which must stay valid for the loader's
    // whole lifetime.
    PhosphorRegistry::Registry<SurfacePack> m_registry;
    // On-disk scan + hot-reload. Parses each pack's metadata.json into a
    // SurfacePack and reconciles m_registry; its onCommitted hook
    // re-emits effectsChanged.
    std::unique_ptr<PhosphorRegistry::MetadataPackLoader<SurfacePack>> m_loader;
};

} // namespace PhosphorSurfaceShaders
