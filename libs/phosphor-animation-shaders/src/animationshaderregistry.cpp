// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/MetadataPackScanStrategy.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonObject>
#include <QLoggingCategory>

#include <algorithm>
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

/// Hash the schema-specific bits change-detection cares about beyond
/// `metadata.json` itself. The strategy already mixes in `id`, `isUser`,
/// and the metadata.json's size+mtime per entry — that automatically
/// covers every parser-consumed metadata field (`name`, `description`,
/// `parameters[]`, etc.) without enumerating them here. What this
/// contributor adds is the OUT-OF-METADATA state that still affects
/// rendering: the resolved frag/vert path strings (a search-path
/// reorder that swaps the winning copy of an effect needs to be a
/// signature change even if the metadata bytes match), the source-dir
/// (same reasoning), and a frag-file mtime+size so a content edit on
/// the fragment shader fires `effectsChanged` even when no
/// `metadata.json` byte moved.
void contributeSignature(QCryptographicHash& h, const AnimationShaderEffect& e)
{
    h.addData(e.fragmentShaderPath.toUtf8());
    h.addData(QByteArrayView("|"));
    h.addData(e.vertexShaderPath.toUtf8());
    h.addData(QByteArrayView("|"));
    h.addData(e.sourceDir.toUtf8());
    h.addData(QByteArrayView("|"));
    // QFileInfo on a missing file returns size 0 and an invalid mtime,
    // and `lastModified().toMSecsSinceEpoch()` on an invalid datetime
    // is implementation-defined. The signature would still be stable
    // *while the file stays absent*, but a flickering file would muddy
    // change-detection diagnostics. Feed an explicit "missing" sentinel
    // when the file isn't there; on-disk files contribute size+mtime.
    auto contributeFileFp = [&h](const QString& path) {
        if (path.isEmpty()) {
            return;
        }
        const QFileInfo fi(path);
        if (fi.exists()) {
            h.addData(QByteArray::number(fi.size()));
            h.addData(QByteArrayView("|"));
            h.addData(QByteArray::number(fi.lastModified().toMSecsSinceEpoch()));
        } else {
            h.addData(QByteArrayView("missing"));
        }
        h.addData(QByteArrayView("|"));
    };
    contributeFileFp(e.fragmentShaderPath);
    // Vertex shader mtime+size — symmetrical with the frag treatment.
    // `effectWatchPaths` watches the vertex shader so a content edit
    // fires the rescan; without this mix-in the SHA-1 signature
    // wouldn't shift and `effectsChanged` would be silenced.
    contributeFileFp(e.vertexShaderPath);
}

} // namespace

std::unique_ptr<PhosphorFsLoader::IScanStrategy>
AnimationShaderRegistry::buildScanStrategy(AnimationShaderRegistry* self)
{
    auto strategy = std::make_unique<ScanStrategy>(parseEffect, [self]() {
        Q_EMIT self->effectsChanged();
    });
    strategy->setPerEntryWatchPaths(effectWatchPaths);
    strategy->setSignatureContrib(contributeSignature);
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
