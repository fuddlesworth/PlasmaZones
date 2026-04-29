// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimationShaders/AnimationShaderEffect.h>
#include <PhosphorAnimationShaders/phosphoranimationshaders_export.h>

#include <PhosphorFsLoader/MetadataPackRegistryBase.h>
#include <PhosphorFsLoader/MetadataPackScanStrategy.h>

#include <QList>
#include <QString>
#include <QStringList>

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
class PHOSPHORANIMATIONSHADERS_EXPORT AnimationShaderRegistry : public PhosphorFsLoader::MetadataPackRegistryBase
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
    ScanStrategy* m_strategy;
};

} // namespace PhosphorAnimationShaders
