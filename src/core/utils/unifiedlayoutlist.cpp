// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "unifiedlayoutlist.h"

#include "core/types/constants.h"
#include "core/interfaces/interfaces.h"
#include "phosphor_i18n.h"
#include "utils.h"

#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorLayoutApi/ILayoutSource.h>
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <PhosphorTiles/AutotileLayoutSource.h>
#include <PhosphorTiles/AutotilePreviewRender.h>
#include <PhosphorTiles/ITileAlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorZones/IZoneLayoutRegistry.h>
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include <PhosphorZones/ZoneJsonKeys.h>
#include <PhosphorZones/ScrollingTemplateSource.h>
#include <PhosphorZones/ScrollingTemplateStore.h>
#include <PhosphorZones/ZonesLayoutSource.h>

#include <algorithm>
#include <climits>

#include <QHash>
#include <QRectF>
#include <QUuid>

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
        preview.sectionLabel = PhosphorI18n::tr("All Monitors");
        preview.sectionOrder = 0;
        break;
    case AspectRatioClass::Standard:
        preview.sectionLabel = PhosphorI18n::tr("Standard (16:9)");
        preview.sectionOrder = 1;
        break;
    case AspectRatioClass::Ultrawide:
        preview.sectionLabel = PhosphorI18n::tr("Ultrawide (21:9)");
        preview.sectionOrder = 2;
        break;
    case AspectRatioClass::SuperUltrawide:
        preview.sectionLabel = PhosphorI18n::tr("Super-Ultrawide (32:9)");
        preview.sectionOrder = 3;
        break;
    case AspectRatioClass::Portrait:
        preview.sectionLabel = PhosphorI18n::tr("Portrait (9:16)");
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

/// The synthetic "no template" row, carrying the reserved
/// PhosphorZones::NoScrollingTemplate id. Opt-in per call site: it belongs in
/// the lists that PICK a context's template, and not in the management and
/// D-Bus lists, where a row standing for the absence of a template would read
/// as a template you could rename or delete.
///
/// Deliberately zone-less. A template's preview draws its starting columns as
/// bands, and "no template" has none to draw — a single full-width band would
/// read as one maximized column, which is a shape the engine does not adopt.
/// zoneCount 0 is the UnlimitedZoneCount sentinel, so the row passes
/// LayoutPreview::isValid with an empty zone list.
LayoutPreview noScrollingTemplatePreview()
{
    LayoutPreview preview;
    preview.id = QString(PhosphorZones::NoScrollingTemplate);
    preview.displayName = PhosphorI18n::tr("None");
    preview.description =
        PhosphorI18n::tr("Use no template on this screen, so columns keep the built-in widths and heights");
    preview.isScrollingTemplate = true;
    preview.sectionKey = QStringLiteral("scrolling-templates");
    preview.sectionLabel = PhosphorI18n::tr("Scrolling Templates");
    preview.sectionOrder = 10;
    return preview;
}

/// Append every store template as a preview row (its own section, after the
/// aspect sections and the autotile family), optionally led by the None row
/// above. Templates have no hidden/allow-list/aspect axes today, so no context
/// filtering applies — which also satisfies the active-selection exemption
/// trivially (the context's assigned template is always in the list).
///
/// @p verticalAxis is the strip axis of the screen these rows are being built
/// for, and it only reaches the per-screen overload. The flat management list
/// is screen-agnostic by design, so it keeps the horizontal depiction.
void appendScrollingTemplatePreviews(QVector<LayoutPreview>& list, PhosphorZones::ScrollingTemplateStore* store,
                                     bool includeNoTemplateRow, bool verticalAxis)
{
    // The None row does NOT depend on the store: "use no template" is offered
    // even where there are no templates to choose between, because a screen
    // inheriting a default template still has to be able to opt out of it.
    if (includeNoTemplateRow) {
        list.append(noScrollingTemplatePreview());
    }
    if (!store) {
        return;
    }
    const QList<PhosphorZones::ScrollingTemplate> templates = store->templates();
    list.reserve(list.size() + templates.size());
    for (const PhosphorZones::ScrollingTemplate& templ : templates) {
        LayoutPreview preview = PhosphorZones::previewFromScrollingTemplate(templ, verticalAxis);
        preview.sectionKey = QStringLiteral("scrolling-templates");
        preview.sectionLabel = PhosphorI18n::tr("Scrolling Templates");
        preview.sectionOrder = 10;
        list.append(preview);
    }
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
    // the ctor and it self-wires to contentsChanged - every call here
    // is a plain cache hit unless a registry mutation happened since
    // the last build.
    //
    // Fallback: construct a transient AutotileLayoutSource over the
    // registry. The transient source's preview cache is discarded
    // between calls - every algorithm preview is recomputed. This
    // covers code paths that don't yet hold a bundle reference (tests,
    // early-init call sites). The previous process-global
    // AlgorithmRegistry::instance() singleton-backed static source is
    // gone with the registry singleton - per-process ownership leaves
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

// Manual before autotile, recommended first, then by name. There is
// deliberately no template arm: grouping templates away from the other two
// families is a QML concern driven by sectionKey/sectionLabel/sectionOrder,
// which only the QML side reads, so adding a sort arm here would duplicate
// that decision in a second place.
bool defaultPreviewLessThan(const LayoutPreview& a, const LayoutPreview& b)
{
    // The no-template row is an ESCAPE from the list rather than a member of
    // it, so it sorts to the very end instead of taking its alphabetical
    // place among the templates. Tested before every other term, including
    // `recommended`, because none of them should be able to pull it back up:
    // a row that means "none of these" reads as an afterthought to the
    // choices, and landing between two real templates reads as one.
    const bool aNone = a.id == PhosphorZones::NoScrollingTemplate;
    const bool bNone = b.id == PhosphorZones::NoScrollingTemplate;
    if (aNone != bNone) {
        return bNone;
    }
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

QStringList buildCustomOrder(const IOrderingSettings* settings, bool includeManual, bool includeAutotile,
                             bool includeScrollingTemplates)
{
    QStringList order;
    if (!settings) {
        return order;
    }
    if (includeManual) {
        // Snapping layouts are keyed by the UNPREFIXED braced-UUID string in
        // both the order and the preview ids, so they match sortPreviews
        // directly. ("Unprefixed", not "bare": in this repo's QUuid vocabulary
        // bare/braced is the WithoutBraces axis, and these ids keep braces.)
        order.append(settings->snappingLayoutOrder());
    }
    if (includeAutotile) {
        // The order stores BARE algorithm ids ("bsp"), but autotile previews are
        // keyed "autotile:<id>" (LayoutPreview::id). Prefix to the preview
        // namespace, or sortPreviews' id match misses every autotile entry and
        // the priority order silently no-ops for tiling.
        const QStringList algoOrder = settings->tilingAlgorithmOrder();
        order.reserve(order.size() + algoOrder.size());
        for (const QString& algoId : algoOrder) {
            order.append(PhosphorLayout::LayoutId::makeAutotileId(algoId));
        }
    }
    if (includeScrollingTemplates) {
        // Template previews are keyed by the template's braced-UUID string
        // (previewFromScrollingTemplate), which is also what the order stores,
        // so no namespace prefix is needed. The None row deliberately has no
        // order entry — it must resolve to the INT_MAX bucket so
        // defaultPreviewLessThan pins it last — and that invariant must hold
        // for EVERY writer of the key, including a hand-edited config.json
        // that the UI and D-Bus validators never saw, so the reserved word is
        // skipped here rather than trusted absent.
        const QStringList templateOrder = settings->scrollingTemplateOrder();
        order.reserve(order.size() + templateOrder.size());
        for (const QString& id : templateOrder) {
            if (id != PhosphorZones::NoScrollingTemplate) {
                order.append(id);
            }
        }
    }
    return order;
}

QVector<LayoutPreview> buildUnifiedLayoutList(PhosphorZones::IZoneLayoutRegistry* layoutManager,
                                              PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
                                              bool includeAutotile, const QStringList& customOrder,
                                              PhosphorLayout::ILayoutSource* autotileSource,
                                              QSize autotilePreviewCanvas,
                                              PhosphorZones::ScrollingTemplateStore* templateStore)
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

    // No None row on this overload: it is the flat management list (the
    // settings app's catalogue over D-Bus), where a row standing for the
    // absence of a template has nothing to manage. It is screen-agnostic for
    // the same reason, so its template cards keep the horizontal depiction —
    // there is no screen here whose axis they could mirror.
    appendScrollingTemplatePreviews(list, templateStore, false, /*verticalAxis=*/false);

    sortPreviews(list, customOrder);

    return list;
}

QVector<LayoutPreview> buildUnifiedLayoutList(
    PhosphorZones::IZoneLayoutRegistry* layoutManager, PhosphorTiles::ITileAlgorithmRegistry* algorithmRegistry,
    const QString& screenId, int virtualDesktop, const QString& activity, bool includeManual, bool includeAutotile,
    qreal screenAspectRatio, bool filterByAspectRatio, const QStringList& customOrder,
    PhosphorLayout::ILayoutSource* autotileSource, QSize autotilePreviewCanvas, bool includeScrollingTemplates,
    PhosphorZones::ScrollingTemplateStore* templateStore, bool includeNoTemplateRow, bool stripVerticalAxis)
{
    QVector<LayoutPreview> list;

    if (!layoutManager) {
        return list;
    }

    // Translate connector name to screen ID for allowedScreens matching.
    QString resolvedScreenId;
    if (!screenId.isEmpty()) {
        resolvedScreenId = PhosphorScreens::ScreenIdentity::isConnectorName(screenId)
            ? PhosphorScreens::ScreenIdentity::idForName(screenId)
            : screenId;
    }

    // Track the active layout so we can guarantee it appears in the list
    // (prevents empty selector / broken cycling when the active layout is
    // hidden from the filter). Widened HERE rather than at a call site so
    // buildLayoutsList and visibleLayoutCount can never disagree on the row
    // count (a past divergence pinned the popup open). The pre-pivot
    // scrolling-template exemption is gone with the mined model: native
    // templates are not manual layouts, so no Layout* in this walk can be
    // the context's template. Template entries are appended unconditionally
    // further down (appendScrollingTemplatePreviews), which satisfies their
    // own active-selection exemption trivially: nothing filters them, so the
    // context's assigned template is always present.
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

        // Drop autotile previews the user has hidden from the curated picker.
        // Manual layouts are filtered inline above (hiddenFromSelector on the
        // Layout); autotile entries have no Layout, so their hidden state lives
        // in the unified layout-settings.json sidecar keyed by "autotile:<id>".
        // The algorithm active for this context is kept visible even when hidden
        // — mirrors the manual active-layout exemption above so the picker/OSD
        // can't lose the currently-tiling algorithm (broken cycling / empty
        // selector).
        const QString activeAlgoId =
            layoutManager->tilingAlgorithmForScreen(resolvedScreenId, virtualDesktop, activity);
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [layoutManager, &activeAlgoId](const LayoutPreview& preview) {
                                      if (!preview.isAutotile()) {
                                          return false;
                                      }
                                      const QString algoId = PhosphorLayout::LayoutId::extractAlgorithmId(preview.id);
                                      if (!activeAlgoId.isEmpty() && algoId == activeAlgoId) {
                                          return false;
                                      }
                                      const QJsonObject overrides = layoutManager->loadAutotileOverrides(algoId);
                                      return overrides.value(ZoneJsonKeys::HiddenFromSelector).toBool(false);
                                  }),
                   list.end());
    }

    if (includeScrollingTemplates) {
        appendScrollingTemplatePreviews(list, templateStore, includeNoTemplateRow, stripVerticalAxis);
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
