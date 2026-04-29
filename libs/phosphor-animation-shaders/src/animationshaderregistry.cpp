// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/MetadataPackScanStrategy.h>

#include <QDir>
#include <QJsonObject>
#include <QLoggingCategory>

#include <optional>

namespace PhosphorAnimationShaders {

namespace {
Q_LOGGING_CATEGORY(lcRegistry, "phosphoranimationshaders.registry")

/// Parse one already-validated metadata.json root into an
/// AnimationShaderEffect. The strategy already ran the file-existence,
/// size-cap, and JSON-object-root checks before invoking us; we own only
/// the schema-specific bits — directory-relative path resolution and
/// `isUserEffect` stamping. The strategy itself rejects empty-id
/// payloads with a warning, so we don't double-check here.
///
/// Note — intentional asymmetry with `PhosphorShaders::ShaderRegistry::parseShader`:
/// that parser rejects payloads whose `sourcePath` doesn't exist on disk,
/// whereas this one tolerates missing frag / vert. AnimationShaderEffect
/// payloads are consumed by the kwin effect's transition pipeline which
/// validates path existence at use-time and gracefully falls back; the
/// registry is the catalog, not the gate. Watch-set auto-fingerprinting
/// in the strategy still mixes a "missing" sentinel for absent files,
/// so a frag materialising on disk shifts the signature and fires
/// `effectsChanged`.
std::optional<AnimationShaderEffect> parseEffect(const QString& effectDir, const QJsonObject& root, bool isUserDir)
{
    AnimationShaderEffect e = AnimationShaderEffect::fromJson(root);
    e.sourceDir = effectDir;
    e.isUserEffect = isUserDir;

    // Resolve directory-relative paths via QDir::filePath, which returns
    // absolute inputs unchanged — keeps schema tolerance symmetric with
    // PhosphorShaders::ShaderRegistry's parser. Naive string concat
    // would mangle absolute paths from a metadata.json into invalid
    // double-rooted forms.
    const QDir dir(effectDir);
    if (!e.fragmentShaderPath.isEmpty()) {
        e.fragmentShaderPath = dir.filePath(e.fragmentShaderPath);
    }
    if (!e.vertexShaderPath.isEmpty()) {
        e.vertexShaderPath = dir.filePath(e.vertexShaderPath);
    }
    if (!e.previewPath.isEmpty()) {
        e.previewPath = dir.filePath(e.previewPath);
    }

    return e;
}

/// Per-payload watch list — frag + vert shader file paths. The strategy
/// already adds the metadata.json itself; this callback is for everything
/// else. Preview is informational only (no live-reload need on a static
/// thumbnail) and is excluded.
QStringList effectWatchPaths(const AnimationShaderEffect& e)
{
    QStringList paths;
    if (!e.fragmentShaderPath.isEmpty()) {
        paths.append(e.fragmentShaderPath);
    }
    if (!e.vertexShaderPath.isEmpty()) {
        paths.append(e.vertexShaderPath);
    }
    return paths;
}

// No `setSignatureContrib` is wired below — the strategy auto-fingerprints
// every distinct file `effectWatchPaths` returns (path|size|mtime|), which
// covers the resolved frag and vertex shader paths in both the
// "search-path reorder swaps the winning copy" and "in-place content edit"
// scenarios. A bespoke `SignatureContrib` would be redundant.

} // namespace

std::unique_ptr<PhosphorFsLoader::IScanStrategy>
AnimationShaderRegistry::buildScanStrategy(AnimationShaderRegistry* self)
{
    auto strategy = std::make_unique<ScanStrategy>(parseEffect, [self]() {
        Q_EMIT self->effectsChanged();
    });
    strategy->setPerEntryWatchPaths(effectWatchPaths);
    strategy->setLoggingCategory(&lcRegistry());
    return strategy;
}

AnimationShaderRegistry::AnimationShaderRegistry(QObject* parent)
    : MetadataPackRegistryBase(lcRegistry(), buildScanStrategy(this), parent)
    , m_typedStrategy(static_cast<ScanStrategy*>(strategy()))
{
    // The static_cast above is safe by construction (`buildScanStrategy`
    // is the only path that populates the base's strategy slot, and it
    // always produces a `ScanStrategy`). Pin that invariant in debug
    // builds via dynamic_cast so a future refactor that diverts the
    // strategy slot fails loudly instead of silently UB-ing on lookup.
    Q_ASSERT_X(dynamic_cast<ScanStrategy*>(strategy()) != nullptr, "AnimationShaderRegistry",
               "buildScanStrategy must return a MetadataPackScanStrategy<AnimationShaderEffect>");
}

AnimationShaderRegistry::~AnimationShaderRegistry() = default;

void AnimationShaderRegistry::onUserPathChanged(const QString& path)
{
    m_typedStrategy->setUserPath(path);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

QList<AnimationShaderEffect> AnimationShaderRegistry::availableEffects() const
{
    // Strategy returns a sorted-by-id snapshot — single source of truth
    // for QHash-randomisation-stable output.
    return m_typedStrategy->packs();
}

AnimationShaderEffect AnimationShaderRegistry::effect(const QString& id) const
{
    return m_typedStrategy->pack(id);
}

bool AnimationShaderRegistry::hasEffect(const QString& id) const
{
    return m_typedStrategy->contains(id);
}

QStringList AnimationShaderRegistry::effectIds() const
{
    return m_typedStrategy->packIds();
}

} // namespace PhosphorAnimationShaders
