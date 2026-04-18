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

    const QRect canvas(0, 0, PreviewCanvasSize, PreviewCanvasSize);

    // Seed preview params from the registry's configured values so previews
    // reflect the user's saved master-count / split-ratio / max-windows
    // tuning. Active algorithm gets the global configured values; other
    // algorithms fall back to their per-algorithm saved entry, then to the
    // algorithm's own defaults.
    int masterCount = PhosphorTiles::AutotileDefaults::DefaultMasterCount;
    qreal splitRatio = algorithm->defaultSplitRatio();
    int effectiveCount = windowCount;
    if (auto* registry = PhosphorTiles::AlgorithmRegistry::instance()) {
        const auto& params = PhosphorTiles::AlgorithmRegistry::configuredPreviewParams();
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

PhosphorLayout::LayoutPreview previewFromAlgorithm(PhosphorTiles::TilingAlgorithm* algorithm, int windowCount)
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
    return previewFromAlgorithm(id, algorithm, windowCount);
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
        // User-tuned master-count / split-ratio updates must invalidate the
        // preview cache — otherwise the next availableLayouts() returns
        // geometry rendered against the old state.
        connect(m_registry, &PhosphorTiles::AlgorithmRegistry::previewParamsChanged, this, [this]() {
            invalidateCache();
            Q_EMIT contentsChanged();
        });
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
