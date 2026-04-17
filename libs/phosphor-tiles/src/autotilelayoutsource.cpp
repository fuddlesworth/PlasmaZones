// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorLayoutApi/AlgorithmMetadata.h>
#include <PhosphorLayoutApi/EdgeGaps.h>
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
constexpr int PreviewCanvasSize = 1000;

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
    meta.memory = algorithm->supportsMemory();
    meta.isScripted = algorithm->isScripted();
    meta.isUserScript = algorithm->isUserScript();
    meta.zoneNumberDisplay = algorithm->zoneNumberDisplay();
    return meta;
}

QString makePreviewId(const QString& algorithmId)
{
    // Prefix matches PlasmaZones::LayoutId::AutotilePrefix in core/constants.h
    // (kept in sync as a literal so this library doesn't depend on the
    // PlasmaZones header).
    return QStringLiteral("autotile:") + algorithmId;
}

} // namespace

PhosphorLayout::LayoutPreview previewFromAlgorithm(PhosphorTiles::TilingAlgorithm* algorithm, int windowCount)
{
    PhosphorLayout::LayoutPreview preview;
    if (!algorithm) {
        return preview;
    }

    auto* registry = PhosphorTiles::AlgorithmRegistry::instance();
    const QString algorithmId = registry ? registry->availableAlgorithms().value(0) : QString();
    // Recover this algorithm's id by reverse lookup — the algorithm itself
    // doesn't expose it, the registry owns the mapping.
    QString id;
    if (registry) {
        const auto ids = registry->availableAlgorithms();
        for (const QString& candidate : ids) {
            if (registry->algorithm(candidate) == algorithm) {
                id = candidate;
                break;
            }
        }
    }
    if (id.isEmpty()) {
        qCWarning(PhosphorTiles::lcTilesLib)
            << "previewFromAlgorithm: algorithm not in registry — preview will have empty id";
        return preview;
    }

    const int effectiveCount = windowCount > 0 ? windowCount : algorithm->defaultMaxWindows();
    const QRect canvas(0, 0, PreviewCanvasSize, PreviewCanvasSize);

    PhosphorTiles::TilingState previewState(QStringLiteral("preview"));
    PhosphorTiles::TilingParams params = PhosphorTiles::TilingParams::forPreview(effectiveCount, canvas, &previewState);

    const QVector<QRect> rects = algorithm->calculateZones(params);

    preview.id = makePreviewId(id);
    preview.displayName = algorithm->name();
    preview.description = algorithm->description();
    preview.zoneCount = effectiveCount;
    preview.isAutotile = true;
    preview.algorithm = buildMetadata(algorithm);

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

// ─── AutotileLayoutSource ───────────────────────────────────────────────────

AutotileLayoutSource::AutotileLayoutSource(PhosphorTiles::AlgorithmRegistry* registry)
    : m_registry(registry)
{
}

AutotileLayoutSource::~AutotileLayoutSource() = default;

QVector<PhosphorLayout::LayoutPreview> AutotileLayoutSource::availableLayouts() const
{
    QVector<PhosphorLayout::LayoutPreview> result;
    if (!m_registry) {
        return result;
    }

    const auto algorithms = m_registry->allAlgorithms();
    result.reserve(algorithms.size());
    for (PhosphorTiles::TilingAlgorithm* algorithm : algorithms) {
        if (!algorithm) {
            continue;
        }
        result.append(previewFromAlgorithm(algorithm, DefaultPreviewWindowCount));
    }
    return result;
}

PhosphorLayout::LayoutPreview AutotileLayoutSource::previewAt(const QString& id, int windowCount,
                                                              const QSize& /*canvas*/) const
{
    if (!m_registry || id.isEmpty()) {
        return {};
    }

    static const QLatin1String prefix("autotile:");
    if (!id.startsWith(prefix)) {
        return {};
    }
    const QString algorithmId = id.mid(prefix.size());
    PhosphorTiles::TilingAlgorithm* algorithm = m_registry->algorithm(algorithmId);
    return algorithm ? previewFromAlgorithm(algorithm, windowCount) : PhosphorLayout::LayoutPreview{};
}

} // namespace PhosphorTiles
