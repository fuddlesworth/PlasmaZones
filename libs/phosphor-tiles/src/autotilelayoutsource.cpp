// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/AlgorithmMetadata.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/AutotileConstants.h>
#include <PhosphorTiles/AutotileLayoutSource.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorTiles/TilingState.h>

#include "tileslogging.h"

#include <QRect>
#include <QRectF>

namespace PhosphorTiles {

namespace {

// Shared canvas edge length — every preview path in-tree scales against
// this value, so consumers can render against any pixel rect without drift.
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
    meta.zoneNumberDisplay = PhosphorLayout::zoneNumberDisplayFromString(algorithm->zoneNumberDisplay());
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
                                                   PhosphorTiles::TilingAlgorithm* algorithm, int windowCount,
                                                   PhosphorTiles::ITileAlgorithmRegistry* registry)
{
    PhosphorLayout::LayoutPreview preview;
    if (!algorithm || algorithmId.isEmpty()) {
        return preview;
    }
    // Null registry is tolerated (matches ZonesLayoutSource's discipline):
    // the preview-params seeding loop below already guards on `if (registry)`,
    // and callers that pass null just get defaults.

    const QRect canvas(0, 0, PreviewCanvasSize, PreviewCanvasSize);

    // Seed preview params from the supplied registry's configured values so
    // previews reflect the user's saved master-count / split-ratio / max-windows
    // tuning. Active algorithm gets the global configured values; other
    // algorithms fall back to their per-algorithm saved entry, then to the
    // algorithm's own defaults.
    int masterCount = PhosphorTiles::AutotileDefaults::DefaultMasterCount;
    qreal splitRatio = algorithm->defaultSplitRatio();
    int effectiveCount = windowCount;
    if (registry) {
        const auto& params = registry->previewParams();
        const bool isActive = !params.algorithmId.isEmpty() && registry->algorithm(params.algorithmId) == algorithm;
        if (isActive) {
            if (params.masterCount > 0) {
                masterCount = params.masterCount;
            }
            if (params.splitRatio > 0.0) {
                splitRatio = params.splitRatio;
            }
            if (effectiveCount <= 0 && params.maxWindows > 0) {
                effectiveCount = params.maxWindows;
            }
        } else {
            auto it = params.savedAlgorithmSettings.constFind(algorithmId);
            if (it != params.savedAlgorithmSettings.constEnd()) {
                const QVariantMap& saved = it.value();
                const int savedMaster = saved.value(PhosphorTiles::AutotileJsonKeys::MasterCount, -1).toInt();
                const qreal savedRatio = saved.value(PhosphorTiles::AutotileJsonKeys::SplitRatio, -1.0).toDouble();
                if (savedMaster > 0) {
                    masterCount = savedMaster;
                }
                if (savedRatio > 0.0) {
                    splitRatio = savedRatio;
                }
            }
        }
    }
    if (effectiveCount <= 0) {
        effectiveCount = algorithm->defaultMaxWindows();
    }

    PhosphorTiles::TilingState previewState(QStringLiteral("preview"));
    previewState.setMasterCount(masterCount);
    previewState.setSplitRatio(splitRatio);
    PhosphorTiles::TilingParams params = PhosphorTiles::TilingParams::forPreview(effectiveCount, canvas, &previewState);

    const QVector<QRect> rects = algorithm->calculateZones(params);

    preview.id = makePreviewId(algorithmId);
    preview.displayName = algorithm->name();
    preview.description = algorithm->description();
    preview.zoneCount = rects.size();
    // Setting preview.algorithm = ... makes isAutotile() return true.
    preview.algorithm = buildMetadata(algorithm);
    // Built-in C++ algorithms and system-installed scripts are system
    // entries; user scripts are not. Consumers should not recompute this
    // — they treat LayoutPreview::isSystem as authoritative.
    preview.isSystem = !preview.algorithm->isScripted || !preview.algorithm->isUserScript;

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

PhosphorLayout::LayoutPreview previewFromAlgorithm(PhosphorTiles::TilingAlgorithm* algorithm, int windowCount,
                                                   PhosphorTiles::ITileAlgorithmRegistry* registry)
{
    if (!algorithm) {
        return {};
    }
    // The algorithm carries its own id now (populated by AlgorithmRegistry
    // at registration). Empty registryId means the algorithm exists but
    // isn't currently registered — preview would have no stable id to
    // reference, so bail out.
    const QString id = algorithm->registryId();
    if (id.isEmpty()) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "previewFromAlgorithm: algorithm not in registry — preview will have empty id";
        return {};
    }
    return previewFromAlgorithm(id, algorithm, windowCount, registry);
}

// ─── AutotileLayoutSource ───────────────────────────────────────────────────

AutotileLayoutSource::AutotileLayoutSource(PhosphorTiles::ITileAlgorithmRegistry* registry, QObject* parent)
    : PhosphorLayout::ILayoutSource(parent)
    , m_registry(registry)
{
    // Null registry is tolerated — mirrors ZonesLayoutSource. In production
    // the factory's registrar returns nullptr when ctx doesn't surface the
    // registry, so the source is never constructed with a null pointer.
    // Public API callers that pass null just get an empty source.
    if (m_registry) {
        // Seed the algorithm-count cache — insertCacheEntry() relies on it
        // for its FIFO cap, and the first invalidation signal may not fire
        // until long after the first availableLayouts() call.
        m_algorithmCountCache = m_registry->availableAlgorithms().size();
        // Subscribe to the unified ILayoutSourceRegistry::contentsChanged
        // signal — AlgorithmRegistry already bridges its three specific
        // mutation signals (algorithmRegistered / algorithmUnregistered /
        // previewParamsChanged) into that single emission, so the cache
        // invalidates exactly once per registry change regardless of
        // which mutation caused it.
        connect(m_registry, &PhosphorTiles::ITileAlgorithmRegistry::contentsChanged, this,
                &AutotileLayoutSource::invalidateCache);
    }
}

AutotileLayoutSource::~AutotileLayoutSource() = default;

void AutotileLayoutSource::invalidateCache()
{
    m_cache.clear();
    m_cacheOrder.clear();
    // Refresh the cached algorithm count at the same time — the registry's
    // algorithm set is exactly what just changed, and this is the only path
    // where the count can drift. availableAlgorithms() allocates a fresh
    // QStringList so we pay the cost here (on change) rather than on every
    // insertCacheEntry().
    m_algorithmCountCache = m_registry ? m_registry->availableAlgorithms().size() : 0;
    // All invalidation paths converge here, so emitting once guarantees no
    // listener misses a rebuild regardless of which signal fired.
    Q_EMIT contentsChanged();
}

void AutotileLayoutSource::insertCacheEntry(const QString& key, const PhosphorLayout::LayoutPreview& preview) const
{
    // FIFO cap: registered-algorithm-count × 10 entries. Enough headroom for
    // the layout-picker UI (one preview per algorithm × a handful of
    // windowCount values) while preventing unbounded growth if a caller
    // probes previewAt() with a wide range of window counts.
    const int cap = qMax(10, m_algorithmCountCache * 10);

    // Re-insert semantics: if the key is already present (e.g. a caller
    // re-queries after eviction or deliberately overrides), drop the old
    // FIFO slot first so m_cacheOrder never accumulates duplicates.
    // Without this, a hot key's repeated insert leaves stale entries in
    // m_cacheOrder that the eviction loop walks past as no-ops on
    // m_cache.remove(), eventually evicting a valid live entry.
    if (m_cache.contains(key)) {
        m_cacheOrder.removeAll(key);
        m_cache.remove(key);
    }

    // If the algorithm count shrank since the last insert, m_cache may
    // already hold more entries than the current cap allows. Prune the
    // excess (oldest first) before inserting so we never leave the cache
    // above the cap.
    while (m_cache.size() >= cap && !m_cacheOrder.isEmpty()) {
        const QString evict = m_cacheOrder.takeFirst();
        m_cache.remove(evict);
    }
    m_cache.insert(key, preview);
    m_cacheOrder.append(key);
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
            PhosphorLayout::LayoutPreview preview =
                previewFromAlgorithm(id, algorithm, PhosphorLayout::DefaultPreviewWindowCount, m_registry);
            insertCacheEntry(key, preview);
            result.append(preview);
        } else {
            result.append(it.value());
        }
    }
    return result;
}

PhosphorLayout::LayoutPreview AutotileLayoutSource::previewAt(const QString& id, int windowCount,
                                                              const QSize& /*canvas*/)
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
    PhosphorLayout::LayoutPreview preview = previewFromAlgorithm(algorithmId, algorithm, windowCount, m_registry);
    insertCacheEntry(key, preview);
    return preview;
}

} // namespace PhosphorTiles
