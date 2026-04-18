// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/AlgorithmMetadata.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/AutotileLayoutSource.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>

#include "tileslogging.h"

#include <QRect>
#include <QRectF>

namespace PhosphorTiles {

namespace {

// Match the canvas size used by AlgorithmRegistry::generatePreviewZones so
// that previews coming through this adapter are pixel-identical to those
// rendered by the existing daemon-side D-Bus path.
constexpr int PreviewCanvasSize = PhosphorTiles::AlgorithmRegistry::PreviewCanvasSize;

PhosphorLayout::AlgorithmMetadata buildMetadata(PhosphorTiles::TilingAlgorithm* algorithm)
{
    PhosphorLayout::AlgorithmMetadata meta;
    if (!algorithm) {
        return meta;
    }
    meta.supportsMasterCount = algorithm->supportsMasterCount();
    meta.supportsSplitRatio = algorithm->supportsSplitRatio();
    meta.producesOverlappingZones = algorithm->producesOverlappingZones();
    meta.supportsCustomParams = algorithm->supportsCustomParams();
    meta.supportsMemory = algorithm->supportsMemory();
    meta.isScripted = algorithm->isScripted();
    meta.isUserScript = algorithm->isUserScript();
    meta.zoneNumberDisplay = algorithm->zoneNumberDisplay();
    return meta;
}

QString makePreviewId(const QString& algorithmId)
{
    return PhosphorLayout::LayoutId::makeAutotileId(algorithmId);
}

QString cacheKey(const QString& algorithmId, int windowCount)
{
    return algorithmId + QLatin1Char('|') + QString::number(windowCount);
}

} // namespace

PhosphorLayout::LayoutPreview previewFromAlgorithm(const QString& algorithmId,
                                                   PhosphorTiles::TilingAlgorithm* algorithm, int windowCount)
{
    PhosphorLayout::LayoutPreview preview;
    if (!algorithm || algorithmId.isEmpty()) {
        return preview;
    }

    const int effectiveCount = windowCount > 0 ? windowCount : algorithm->defaultMaxWindows();
    const QRect canvas(0, 0, PreviewCanvasSize, PreviewCanvasSize);

    PhosphorTiles::TilingState previewState(QStringLiteral("preview"));
    PhosphorTiles::TilingParams params = PhosphorTiles::TilingParams::forPreview(effectiveCount, canvas, &previewState);

    const QVector<QRect> rects = algorithm->calculateZones(params);

    preview.id = makePreviewId(algorithmId);
    preview.displayName = algorithm->name();
    preview.description = algorithm->description();
    preview.zoneCount = rects.size();
    // Setting preview.algorithm = ... makes isAutotile() return true.
    preview.algorithm = buildMetadata(algorithm);
    preview.isSystem = preview.algorithm->isSystemEntry();

    preview.zones.reserve(rects.size());
    preview.zoneNumbers.reserve(rects.size());
    for (int i = 0; i < rects.size(); ++i) {
        const QRect& r = rects[i];
        // Normalise to 0..1 against the preview canvas so renderers can
        // scale into any pixel rectangle (matches LayoutPreview's contract).
        QRectF rel(static_cast<qreal>(r.x()) / PreviewCanvasSize, static_cast<qreal>(r.y()) / PreviewCanvasSize,
                   static_cast<qreal>(r.width()) / PreviewCanvasSize,
                   static_cast<qreal>(r.height()) / PreviewCanvasSize);
        preview.zones.append(rel);
        preview.zoneNumbers.append(i + 1);
    }

    return preview;
}

PhosphorLayout::LayoutPreview previewFromAlgorithm(PhosphorTiles::TilingAlgorithm* algorithm, int windowCount)
{
    if (!algorithm) {
        return {};
    }
    auto* registry = PhosphorTiles::AlgorithmRegistry::instance();
    if (!registry) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "previewFromAlgorithm: no AlgorithmRegistry available — preview will be empty";
        return {};
    }
    // Recover this algorithm's id by reverse lookup — the algorithm itself
    // doesn't expose it, the registry owns the mapping. O(N); prefer the id
    // overload on hot paths where the caller already has the id.
    const auto ids = registry->availableAlgorithms();
    for (const QString& candidate : ids) {
        if (registry->algorithm(candidate) == algorithm) {
            return previewFromAlgorithm(candidate, algorithm, windowCount);
        }
    }
    qCWarning(PhosphorTiles::lcTilesLib)
        << "previewFromAlgorithm: algorithm not in registry — preview will have empty id";
    return {};
}

// ─── AutotileLayoutSource ───────────────────────────────────────────────────

AutotileLayoutSource::AutotileLayoutSource(PhosphorTiles::AlgorithmRegistry* registry, QObject* parent)
    : PhosphorLayout::ILayoutSource(parent)
    , m_registry(registry ? registry : PhosphorTiles::AlgorithmRegistry::instance())
{
    if (m_registry) {
        const auto onRegistered = [this](const QString&) {
            invalidateCache();
            Q_EMIT contentsChanged();
        };
        const auto onUnregistered = [this](const QString&, bool /*replacing*/) {
            invalidateCache();
            Q_EMIT contentsChanged();
        };
        connect(m_registry, &PhosphorTiles::AlgorithmRegistry::algorithmRegistered, this, onRegistered);
        connect(m_registry, &PhosphorTiles::AlgorithmRegistry::algorithmUnregistered, this, onUnregistered);
    }
}

AutotileLayoutSource::~AutotileLayoutSource() = default;

void AutotileLayoutSource::invalidateCache()
{
    m_cache.clear();
}

QVector<PhosphorLayout::LayoutPreview> AutotileLayoutSource::availableLayouts() const
{
    QVector<PhosphorLayout::LayoutPreview> result;
    if (!m_registry) {
        return result;
    }

    // Iterate by id so we pass the id directly to previewFromAlgorithm and
    // avoid the reverse-lookup on each entry (would otherwise make this
    // O(N²) in the number of registered algorithms).
    const auto ids = m_registry->availableAlgorithms();
    result.reserve(ids.size());
    for (const QString& id : ids) {
        PhosphorTiles::TilingAlgorithm* algorithm = m_registry->algorithm(id);
        if (!algorithm) {
            continue;
        }
        const QString key = cacheKey(id, PhosphorLayout::DefaultPreviewWindowCount);
        auto it = m_cache.constFind(key);
        if (it == m_cache.constEnd()) {
            it = m_cache.insert(key, previewFromAlgorithm(id, algorithm, PhosphorLayout::DefaultPreviewWindowCount));
        }
        result.append(it.value());
    }
    return result;
}

PhosphorLayout::LayoutPreview AutotileLayoutSource::previewAt(const QString& id, int windowCount,
                                                              const QSize& /*canvas*/) const
{
    if (!m_registry || id.isEmpty()) {
        return {};
    }

    if (!PhosphorLayout::LayoutId::isAutotile(id)) {
        return {};
    }
    const QString algorithmId = PhosphorLayout::LayoutId::extractAlgorithmId(id);
    const QString key = cacheKey(algorithmId, windowCount);
    auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd()) {
        return it.value();
    }
    PhosphorTiles::TilingAlgorithm* algorithm = m_registry->algorithm(algorithmId);
    if (!algorithm) {
        return {};
    }
    PhosphorLayout::LayoutPreview preview = previewFromAlgorithm(algorithmId, algorithm, windowCount);
    m_cache.insert(key, preview);
    return preview;
}

} // namespace PhosphorTiles
