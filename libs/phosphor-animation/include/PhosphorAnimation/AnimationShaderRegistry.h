// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/AnimationShaderEffect.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <PhosphorFsLoader/MetadataPackRegistryBase.h>
#include <PhosphorFsLoader/MetadataPackScanStrategy.h>

#include <QList>
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
 * Search-path management (`addSearchPath`, `addSearchPaths`,
 * `searchPaths`, `setUserPath`, `refresh`) is inherited from
 * `PhosphorFsLoader::MetadataPackRegistryBase`. The first
 * `addSearchPath[s]` call with `LiveReload::On` (the default) installs a
 * `QFileSystemWatcher` with the standard 50 ms debounce, parent-watch
 * promotion for missing user-data dirs, per-file watches re-armed on
 * every rescan, and rescan-during-rescan race protection. Forbidden
 * roots (`$HOME`, `/`, XDG data/config/cache/temp/runtime, Documents,
 * Downloads) are refused.
 *
 * `effectsChanged` is gated on a SHA-1 signature change inside the
 * strategy — emits exactly when the discovered set or any payload
 * fingerprint actually differs from the previous scan, including any
 * `metadata.json` field edit (the strategy mixes the metadata file's
 * size+mtime into the per-rescan signature).
 *
 * ## Thread safety
 *
 * GUI-thread only for both reads and mutations. The pack map lives
 * inside the strategy and is rebuilt on the GUI thread inside the
 * rescan; the public lookup methods read it without synchronisation.
 *
 * `searchPaths()` is the one exception: it returns a by-value snapshot
 * of an implicitly-shared QStringList, so a GUI-thread caller can
 * snapshot it and propagate the result to worker threads. Calling it
 * *from* a worker thread concurrently with a GUI-thread mutation is a
 * data race; snapshot on the GUI thread first.
 */
class PHOSPHORANIMATION_EXPORT AnimationShaderRegistry : public PhosphorFsLoader::MetadataPackRegistryBase
{
    Q_OBJECT

public:
    explicit AnimationShaderRegistry(QObject* parent = nullptr);
    ~AnimationShaderRegistry() override;

    // Lookup -------------------------------------------------------------------

    QList<AnimationShaderEffect> availableEffects() const;
    AnimationShaderEffect effect(const QString& id) const;
    bool hasEffect(const QString& id) const;
    QStringList effectIds() const;

    /// Translate a friendly parameter map into the canonical
    /// `customParams<N>_<x|y|z|w>` slot keys consumed by both runtimes
    /// that drive animation shaders. Slots are allocated in metadata
    /// declaration order — the first declared float/int/bool param fills
    /// `customParams1_x`, the second `customParams1_y`, …, the fifth
    /// `customParams2_x`, etc., up to 32 slots
    /// (`AnimationShaderContract::kMaxParameterSlots`).
    ///
    /// Bool values become `0.0` or `1.0`. Missing keys fall back to the
    /// metadata-declared default, then to `0.0`. Unknown parameter ids
    /// in `friendlyParams` are silently ignored. Color parameters are
    /// **not** currently translated — animation effects don't declare
    /// them today; if they do, this helper will need a separate
    /// `customColor<N>` mapping (see `AnimationShaderContract`).
    ///
    /// Returns an empty map when `effectId` resolves to no registered
    /// effect or when the effect declares no parameters.
    QVariantMap translateAnimationParams(const QString& effectId, const QVariantMap& friendlyParams) const;

    /// Static counterpart for callers that already hold the resolved
    /// `AnimationShaderEffect` (e.g. the kwin-effect's per-frame
    /// translation). Same slot-allocation contract as the registry-keyed
    /// overload.
    static QVariantMap translateAnimationParams(const AnimationShaderEffect& effect, const QVariantMap& friendlyParams);

    /// Rewrite the canonical `layout(std140, binding = 0) uniform
    /// AnimationUniforms { ... };` block (from
    /// `data/animations/shared/animation_uniforms.glsl`) into default-block
    /// uniform declarations a classic-GL pipeline can bind. Used by
    /// runtimes that cannot bind UBOs through their shader-program API
    /// (notably `KWin::GLShader`, which addresses default-block uniforms
    /// only).
    ///
    /// Input must already have `#include` directives expanded so the
    /// canonical UBO block is in the source as a literal (the rewriter is
    /// line-based; it does not run the GLSL preprocessor itself). Fields
    /// that are pure-padding on the classic-GL path — `qt_Matrix` /
    /// `qt_Opacity` (KWin manages its own scene-graph transform/opacity)
    /// and the `_appField0` / `_appField1` std140 alignment slots — are
    /// dropped during rewrite.
    ///
    /// Idempotent on input that has no canonical UBO block (returns the
    /// source unchanged). Returns the rewritten source as UTF-8 bytes
    /// since classic-GL shader compilers accept `const char*` /
    /// `QByteArray` directly without an extra UTF-16 round trip.
    static QByteArray rewriteCanonicalUboToDefaultBlock(const QString& expandedShaderSource);

Q_SIGNALS:
    void effectsChanged();

protected:
    void onUserPathChanged(const QString& path) override;

private:
    using ScanStrategy = PhosphorFsLoader::MetadataPackScanStrategy<AnimationShaderEffect>;

    /// Build + configure the scan strategy. Returns the base type so
    /// the helper can be invoked from the ctor's member-init list while
    /// staying agnostic of the subclass-private `ScanStrategy` typedef.
    static std::unique_ptr<PhosphorFsLoader::IScanStrategy> buildScanStrategy(AnimationShaderRegistry* self);

    // Non-owning typed alias for the strategy the base owns. Populated
    // in the ctor's member-init list via `static_cast<ScanStrategy*>(strategy())`.
    // Named distinctly from the base's private `m_strategy` to make the
    // shadowing explicit at the field declaration.
    ScanStrategy* m_typedStrategy;
};

} // namespace PhosphorAnimationShaders
