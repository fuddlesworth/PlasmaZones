// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorPointer/PointerShaderEffect.h>
#include <PhosphorPointer/phosphorpointer_export.h>

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

namespace PhosphorPointerShaders {

/**
 * @brief Registry of available pointer shader effects.
 *
 * Discovers packs from configured search paths: each pack is a subdirectory
 * holding a `metadata.json` plus the shader sources it references. The `id`
 * field is the registry key. Storage and change-notify are the generic
 * `PhosphorRegistry::Registry<PointerPack>`; the on-disk scan and hot reload
 * are a `PhosphorRegistry::MetadataPackLoader<PointerPack>`, exactly like
 * the surface family. `effectsChanged` fires on every committed rescan.
 *
 * GUI-thread only for both reads and mutations.
 */
class PHOSPHORPOINTER_EXPORT PointerShaderRegistry : public QObject
{
    Q_OBJECT

public:
    /// Registry entry: a discovered pointer effect as a `PhosphorRegistry`
    /// factory, wrapping the parsed `PointerShaderEffect` by value.
    class PointerPack final : public PhosphorRegistry::IFactoryBase
    {
    public:
        explicit PointerPack(PointerShaderEffect effect)
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
        [[nodiscard]] const PointerShaderEffect& effect() const
        {
            return m_effect;
        }

    private:
        PointerShaderEffect m_effect;
    };

    explicit PointerShaderRegistry(QObject* parent = nullptr);
    ~PointerShaderRegistry() override;

    // ── Search paths (forwarded to the internal MetadataPackLoader) ───
    /// Single-path form of `addSearchPaths`. Exists for parity with the
    /// other shader registries, which consumers construct generically.
    void addSearchPath(const QString& path, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On);
    void addSearchPaths(
        const QStringList& paths, PhosphorFsLoader::LiveReload liveReload = PhosphorFsLoader::LiveReload::On,
        PhosphorFsLoader::RegistrationOrder order = PhosphorFsLoader::RegistrationOrder::LowestPriorityFirst);
    [[nodiscard]] QStringList searchPaths() const;
    /// Classify packs found under @p path as user-owned. Exists for parity
    /// with the other shader registries, which consumers construct generically.
    void setUserPath(const QString& path);
    /// Rescan the search paths now. Exists for parity with the other shader
    /// registries, which consumers construct generically.
    void refresh();

    // ── Lookup ─────────────────────────────────────────────────────────
    [[nodiscard]] QList<PointerShaderEffect> availableEffects() const;
    [[nodiscard]] PointerShaderEffect effect(const QString& id) const;
    [[nodiscard]] bool hasEffect(const QString& id) const;

    /// Translate a friendly parameter map into the slot keys both runtimes
    /// consume. Float / int / bool parameters fill `customParams<N>_<xyzw>`
    /// and colour parameters fill `customColor<N>`, both in declaration
    /// order with independent allocators. Colour values are coerced to
    /// QColor (QColor instances and any string QColor parses); bools become
    /// 0.0 / 1.0; missing scalars fall back to the declared default, then
    /// 0.0. User textures emit `uTexture<N>` (and `uTexture<N>_wrap`) from
    /// the pack's declared slots, overridable through the same keys in
    /// @p friendlyParams with the same traversal guard the loader applies.
    /// Returns an empty map for an invalid effect.
    ///
    /// The result is a snapshot baked per compile. It must be recomputed
    /// whenever the engaged layer's parameters change, and the host, not
    /// this function, owns that re-resolution.
    [[nodiscard]] static QVariantMap translatePointerParams(const PointerShaderEffect& effect,
                                                            const QVariantMap& friendlyParams);

    /// Registry-keyed convenience overload. Empty map when @p effectId is
    /// not registered.
    [[nodiscard]] QVariantMap translatePointerParams(const QString& effectId, const QVariantMap& friendlyParams) const;

    /// The generated `#define p_<id> <glsl-accessor>` preamble for
    /// @p effect's declared parameters, spliced after `#version` by both
    /// runtimes. Slot allocation mirrors `translatePointerParams` exactly,
    /// and like it the result is baked per compile: the host recomputes it
    /// whenever the engaged layer's parameters change.
    [[nodiscard]] static QString paramPreamble(const PointerShaderEffect& effect);

    /// Entry-point scaffold: a pack defines `vec4 pPointer(vec2 uv)` and the
    /// harness prepends this prologue (`#version 450`, `#include
    /// <pointer_lib.glsl>`, the vTexCoord in and fragColor out) and appends a
    /// generated `main()` from the candidates.
    [[nodiscard]] static QString pointerEntryPrologue();
    [[nodiscard]] static QList<PhosphorShaders::EntryCandidate> pointerEntryCandidates();

    /// The vertex stage the COMPOSITOR compiles for every pointer pack's main
    /// pass and buffer stages (classic GL: positions in the viewport's device
    /// space, projected by KWin's own matrix). Lives here rather than in the
    /// effect so the KWin-branch bake test can compile the exact stage the
    /// compositor uses. `data/pointer/shared/pointer.vert` is NOT this: that
    /// is the preview's stage and reads `qt_Matrix` from the UBO, which the
    /// compositor branch does not declare.
    [[nodiscard]] static QString compositorVertexSource();

    /// Include search paths for a pack at @p packDir: its own neighbourhood
    /// `{<packRoot>/shared, <packRoot>}` where packRoot is the parent of the
    /// pack dir, followed by the installed `plasmazones/pointer/shared` under
    /// each XDG data dir.
    ///
    /// Both halves are needed. A bundled pack finds the helpers next door; a
    /// user pack under `~/.local/share/plasmazones/pointer` has no sibling
    /// `shared/` at all, and the entry prologue always emits
    /// `#include <pointer_lib.glsl>`, so without the installed roots every
    /// such pack fails include expansion in the preview and the validator
    /// while rendering perfectly in the compositor, which builds its own list
    /// from the registry's search roots. The pack's own directories come
    /// first, so a pack shipping its own `shared/` is still served from it.
    [[nodiscard]] static QStringList includePathsFor(const QString& packDir);

Q_SIGNALS:
    void effectsChanged();

private:
    /// The registered effect for @p id without copying it out of its pack,
    /// or null when unregistered. The pack stays alive for as long as the
    /// registry holds it, which is the whole of a GUI-thread call.
    [[nodiscard]] const PointerShaderEffect* effectPtr(const QString& id) const;

    // Declared before m_loader so it is destroyed after it: the loader holds
    // a borrowed Registry pointer for its whole lifetime.
    PhosphorRegistry::Registry<PointerPack> m_registry;
    std::unique_ptr<PhosphorRegistry::MetadataPackLoader<PointerPack>> m_loader;
};

} // namespace PhosphorPointerShaders
