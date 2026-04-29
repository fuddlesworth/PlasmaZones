// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimationShaders/AnimationShaderRegistry.h>

#include <PhosphorFsLoader/MetadataPackScanStrategy.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
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
/// the schema-specific bits — directory-relative path resolution,
/// `isUserEffect` stamping, and minimum-viable validation.
///
/// Return `std::nullopt` to skip the entry silently. Returning an effect
/// with an empty `id` also skips it (the strategy checks).
std::optional<AnimationShaderEffect> parseEffect(const QString& effectDir, const QJsonObject& root, bool isUserDir)
{
    AnimationShaderEffect e = AnimationShaderEffect::fromJson(root);
    if (e.id.isEmpty()) {
        // The strategy logs the empty-id case at warning level — return
        // nullopt so we don't double-emit.
        return std::nullopt;
    }

    e.sourceDir = effectDir;
    e.isUserEffect = isUserDir;

    // Resolve directory-relative paths to absolute. The bare strings in
    // metadata.json are file names inside `effectDir`; consumers expect
    // absolute paths.
    if (!e.fragmentShaderPath.isEmpty()) {
        e.fragmentShaderPath = effectDir + QLatin1Char('/') + e.fragmentShaderPath;
    }
    if (!e.vertexShaderPath.isEmpty()) {
        e.vertexShaderPath = effectDir + QLatin1Char('/') + e.vertexShaderPath;
    }
    if (!e.previewPath.isEmpty()) {
        e.previewPath = effectDir + QLatin1Char('/') + e.previewPath;
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
    if (!e.fragmentShaderPath.isEmpty()) {
        const QFileInfo fragInfo(e.fragmentShaderPath);
        h.addData(QByteArray::number(fragInfo.size()));
        h.addData(QByteArrayView("|"));
        h.addData(QByteArray::number(fragInfo.lastModified().toMSecsSinceEpoch()));
    }
}

} // namespace

AnimationShaderRegistry::AnimationShaderRegistry(QObject* parent)
    : MetadataPackRegistryBase(lcRegistry(), parent)
    , m_strategy(std::make_unique<ScanStrategy>(parseEffect, [this]() {
        Q_EMIT effectsChanged();
    }))
{
    m_strategy->setPerEntryWatchPaths(effectWatchPaths);
    m_strategy->setSignatureContrib(contributeSignature);
    m_strategy->setLoggingCategory(&lcRegistry());
    initWatcher(*m_strategy);
}

AnimationShaderRegistry::~AnimationShaderRegistry() = default;

void AnimationShaderRegistry::onUserPathChanged(const QString& path)
{
    m_strategy->setUserPath(path);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Lookup
// ═══════════════════════════════════════════════════════════════════════════════

QList<AnimationShaderEffect> AnimationShaderRegistry::availableEffects() const
{
    // Strategy returns a sorted-by-id snapshot — single source of truth
    // for QHash-randomisation-stable output.
    return m_strategy->packs();
}

AnimationShaderEffect AnimationShaderRegistry::effect(const QString& id) const
{
    return m_strategy->pack(id);
}

bool AnimationShaderRegistry::hasEffect(const QString& id) const
{
    return m_strategy->contains(id);
}

QStringList AnimationShaderRegistry::effectIds() const
{
    QStringList ids = m_strategy->packsById().keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

} // namespace PhosphorAnimationShaders
