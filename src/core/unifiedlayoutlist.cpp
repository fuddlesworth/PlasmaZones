// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "unifiedlayoutlist.h"

#include "constants.h"
#include "interfaces.h"
#include "pz_i18n.h"
#include "utils.h"

#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorLayoutApi/ILayoutSource.h>
#include <PhosphorTiles/AutotileLayoutSource.h>
#include <PhosphorTiles/AutotilePreviewRender.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/ZonesLayoutSource.h>

#include <algorithm>

#include <QHash>
#include <QJsonArray>
#include <QRectF>
#include <QUuid>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PhosphorZones::LayoutUtils {

using ::PhosphorLayout::AspectRatioClass;
using ::PhosphorLayout::LayoutPreview;
namespace ScreenClassification = ::PhosphorLayout::ScreenClassification;
namespace Utils = ::PlasmaZones::Utils;

// ═══════════════════════════════════════════════════════════════════════════
// Unified layout list building
// ═══════════════════════════════════════════════════════════════════════════

namespace {

/// Map aspect-ratio class to section label + order for manual previews.
void setAspectRatioSection(LayoutPreview& preview)
{
    const auto cls = preview.aspectRatioClass;
    preview.sectionKey = ScreenClassification::toString(cls);
    switch (cls) {
    case AspectRatioClass::Any:
        preview.sectionLabel = PzI18n::tr("All Monitors");
        preview.sectionOrder = 0;
        break;
    case AspectRatioClass::Standard:
        preview.sectionLabel = PzI18n::tr("Standard (16:9)");
        preview.sectionOrder = 1;
        break;
    case AspectRatioClass::Ultrawide:
        preview.sectionLabel = PzI18n::tr("Ultrawide (21:9)");
        preview.sectionOrder = 2;
        break;
    case AspectRatioClass::SuperUltrawide:
        preview.sectionLabel = PzI18n::tr("Super-Ultrawide (32:9)");
        preview.sectionOrder = 3;
        break;
    case AspectRatioClass::Portrait:
        preview.sectionLabel = PzI18n::tr("Portrait (9:16)");
        preview.sectionOrder = 4;
        break;
    }
}

LayoutPreview previewFromLayoutWithSection(PhosphorZones::Layout* layout)
{
    LayoutPreview preview = PhosphorZones::previewFromLayout(layout);
    setAspectRatioSection(preview);
    return preview;
}

void appendAutotilePreviewsForCanvas(QVector<LayoutPreview>& list,
                                     PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry, QSize canvas)
{
    // Aspect-aware path: bypass the AutotileLayoutSource cache (keyed on
    // (id, windowCount), so it can't distinguish per-aspect previews) and
    // call previewFromAlgorithm directly with the requested canvas. Used
    // for per-screen pickers (layout picker, OSD, etc.) where the
    // algorithm preview must match the live tiler's split decisions.
    if (!algorithmRegistry) {
        return;
    }
    const auto ids = algorithmRegistry->availableAlgorithms();
    list.reserve(list.size() + ids.size());
    for (const QString& id : ids) {
        auto* algorithm = algorithmRegistry->algorithm(id);
        if (!algorithm) {
            continue;
        }
        list.append(PhosphorTiles::previewFromAlgorithm(id, algorithm, algorithm->defaultMaxWindows(),
                                                        algorithmRegistry, canvas));
    }
}

void appendAutotilePreviews(QVector<LayoutPreview>& list, PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                            PhosphorLayout::ILayoutSource* autotileSource, QSize canvas = {})
{
    // Aspect-aware callers go through the direct path so previews match
    // their target screen's tiling. The cached source's key tuple
    // (id, windowCount) doesn't include canvas, so reusing it across
    // aspect classes would silently serve the wrong preview.
    if (canvas.width() > 0 && canvas.height() > 0) {
        appendAutotilePreviewsForCanvas(list, algorithmRegistry, canvas);
        return;
    }

    // Preferred path: a long-lived ILayoutSource (typically the
    // autotile source owned by the caller's LayoutSourceBundle) whose
    // internal preview cache is reused across calls. The bundle binds
    // that source to the composition root's AlgorithmRegistry once in
    // the ctor and it self-wires to contentsChanged — every call here
    // is a plain cache hit unless a registry mutation happened since
    // the last build.
    //
    // Fallback: construct a transient AutotileLayoutSource over the
    // registry. The transient source's preview cache is discarded
    // between calls — every algorithm preview is recomputed. This
    // covers code paths that don't yet hold a bundle reference (tests,
    // early-init call sites). The previous process-global
    // AlgorithmRegistry::instance() singleton-backed static source is
    // gone with the registry singleton — per-process ownership leaves
    // no well-defined registry for a process-wide source to bind to.
    if (autotileSource) {
        const auto previews = autotileSource->availableLayouts();
        list.reserve(list.size() + previews.size());
        for (const auto& preview : previews) {
            list.append(preview);
        }
        return;
    }
    if (!algorithmRegistry) {
        return;
    }
    PhosphorTiles::AutotileLayoutSource source(algorithmRegistry);
    const auto previews = source.availableLayouts();
    list.reserve(list.size() + previews.size());
    for (const auto& preview : previews) {
        list.append(preview);
    }
}

bool defaultPreviewLessThan(const LayoutPreview& a, const LayoutPreview& b)
{
    if (a.recommended != b.recommended) {
        return a.recommended;
    }
    if (a.isAutotile() != b.isAutotile()) {
        return !a.isAutotile();
    }
    return a.displayName.compare(b.displayName, Qt::CaseInsensitive) < 0;
}

void sortPreviews(QVector<LayoutPreview>& list, const QStringList& customOrder = {})
{
    if (!customOrder.isEmpty()) {
        QHash<QString, int> orderMap;
        for (int i = 0; i < customOrder.size(); ++i) {
            orderMap.insert(customOrder[i], i);
        }

        std::stable_sort(list.begin(), list.end(), [&orderMap](const LayoutPreview& a, const LayoutPreview& b) {
            const int aIdx = orderMap.value(a.id, INT_MAX);
            const int bIdx = orderMap.value(b.id, INT_MAX);
            if (aIdx != bIdx) {
                return aIdx < bIdx;
            }
            return defaultPreviewLessThan(a, b);
        });
    } else {
        std::sort(list.begin(), list.end(), defaultPreviewLessThan);
    }
}

} // namespace

QStringList buildCustomOrder(const IOrderingSettings* settings, bool includeManual, bool includeAutotile)
{
    QStringList order;
    if (!settings) {
        return order;
    }
    if (includeManual) {
        order.append(settings->snappingLayoutOrder());
    }
    if (includeAutotile) {
        order.append(settings->tilingAlgorithmOrder());
    }
    return order;
}

QVector<LayoutPreview> buildUnifiedLayoutList(PhosphorZones::LayoutRegistry* layoutManager,
                                              PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                                              bool includeAutotile, const QStringList& customOrder,
                                              PhosphorLayout::ILayoutSource* autotileSource,
                                              QSize autotilePreviewCanvas)
{
    QVector<LayoutPreview> list;

    if (layoutManager) {
        const auto layouts = layoutManager->layouts();
        for (PhosphorZones::Layout* layout : layouts) {
            if (!layout) {
                continue;
            }
            list.append(previewFromLayoutWithSection(layout));
        }
    }

    if (includeAutotile) {
        appendAutotilePreviews(list, algorithmRegistry, autotileSource, autotilePreviewCanvas);
    }

    sortPreviews(list, customOrder);

    return list;
}

QVector<LayoutPreview> buildUnifiedLayoutList(PhosphorZones::LayoutRegistry* layoutManager,
                                              PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                                              const QString& screenId, int virtualDesktop, const QString& activity,
                                              bool includeManual, bool includeAutotile, qreal screenAspectRatio,
                                              bool filterByAspectRatio, const QStringList& customOrder,
                                              PhosphorLayout::ILayoutSource* autotileSource,
                                              QSize autotilePreviewCanvas)
{
    QVector<LayoutPreview> list;

    if (!layoutManager) {
        return list;
    }

    // Translate connector name to screen ID for allowedScreens matching.
    QString resolvedScreenId;
    if (!screenId.isEmpty()) {
        resolvedScreenId = Phosphor::Screens::ScreenIdentity::isConnectorName(screenId)
            ? Phosphor::Screens::ScreenIdentity::idForName(screenId)
            : screenId;
    }

    // Track the active layout so we can guarantee it appears in the list
    // (prevents empty selector / broken cycling when the active layout is
    // hidden from the filter).
    PhosphorZones::Layout* activeLayout = layoutManager->activeLayout();

    if (includeManual) {
        const auto layouts = layoutManager->layouts();
        for (PhosphorZones::Layout* layout : layouts) {
            if (!layout) {
                continue;
            }

            const bool isActive = (layout == activeLayout);

            if (layout->hiddenFromSelector() && !isActive) {
                continue;
            }

            if (!isActive && !resolvedScreenId.isEmpty() && !layout->allowedScreens().isEmpty()) {
                if (!layout->allowedScreens().contains(resolvedScreenId)) {
                    continue;
                }
            }

            if (!isActive && virtualDesktop > 0 && !layout->allowedDesktops().isEmpty()) {
                if (!layout->allowedDesktops().contains(virtualDesktop)) {
                    continue;
                }
            }

            if (!isActive && !activity.isEmpty() && !layout->allowedActivities().isEmpty()) {
                if (!layout->allowedActivities().contains(activity)) {
                    continue;
                }
            }

            LayoutPreview preview = previewFromLayoutWithSection(layout);

            if (screenAspectRatio > 0.0) {
                preview.recommended = layout->matchesAspectRatio(screenAspectRatio);
            }

            if (filterByAspectRatio && screenAspectRatio > 0.0 && !preview.recommended && !isActive) {
                continue;
            }

            list.append(preview);
        }
    }

    if (includeAutotile) {
        appendAutotilePreviews(list, algorithmRegistry, autotileSource, autotilePreviewCanvas);
    }

    sortPreviews(list, customOrder);

    return list;
}

int findLayoutIndex(const QVector<LayoutPreview>& previews, const QString& layoutId)
{
    for (int i = 0; i < previews.size(); ++i) {
        if (previews[i].id == layoutId) {
            return i;
        }
    }
    return -1;
}

const LayoutPreview* findLayout(const QVector<LayoutPreview>& previews, const QString& layoutId)
{
    const int index = findLayoutIndex(previews, layoutId);
    return index >= 0 ? &previews[index] : nullptr;
}

} // namespace PhosphorZones::LayoutUtils
