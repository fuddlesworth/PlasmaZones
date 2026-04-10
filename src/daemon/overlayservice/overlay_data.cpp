// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include "../../core/layout.h"
#include "../../core/layoutmanager.h"
#include "../../core/zone.h"
#include "../../core/constants.h"
#include "../../core/geometryutils.h"
#include "../../core/utils.h"
#include "../../core/screenmanager.h"
#include "../../core/virtualscreen.h"
#include "../rendering/zonelabeltexturebuilder.h"
#include <QCursor>
#include <QQuickWindow>
#include <QScreen>
#include <QMutexLocker>
#include <QGuiApplication>
#include <QPalette>

namespace PlasmaZones {

namespace {
// Shader-specific JSON keys (overlay-local, not shared with serialization)
constexpr QLatin1String NormalizedX{"normalizedX"};
constexpr QLatin1String NormalizedY{"normalizedY"};
constexpr QLatin1String NormalizedWidth{"normalizedWidth"};
constexpr QLatin1String NormalizedHeight{"normalizedHeight"};
constexpr QLatin1String FillR{"fillR"};
constexpr QLatin1String FillG{"fillG"};
constexpr QLatin1String FillB{"fillB"};
constexpr QLatin1String FillA{"fillA"};
constexpr QLatin1String BorderR{"borderR"};
constexpr QLatin1String BorderG{"borderG"};
constexpr QLatin1String BorderB{"borderB"};
constexpr QLatin1String BorderA{"borderA"};
constexpr QLatin1String ShaderBorderRadius{"shaderBorderRadius"};
constexpr QLatin1String ShaderBorderWidth{"shaderBorderWidth"};
} // namespace

void OverlayService::updateLabelsTextureForWindow(QQuickWindow* window, const QVariantList& patched, QScreen* screen,
                                                  Layout* screenLayout)
{
    Q_UNUSED(screen)
    if (!window) {
        return;
    }
    const bool showNumbers =
        (m_settings ? m_settings->showZoneNumbers() : true) && (!screenLayout || screenLayout->showZoneNumbers());
    const LabelFontSettings lfs = extractLabelFontSettings(m_settings);
    const QSize size(qMax(1, static_cast<int>(window->width())), qMax(1, static_cast<int>(window->height())));
    QImage labelsImage = ZoneLabelTextureBuilder::build(patched, size, lfs.fontColor, showNumbers, lfs.backgroundColor,
                                                        lfs.fontFamily, lfs.fontSizeScale, lfs.fontWeight,
                                                        lfs.fontItalic, lfs.fontUnderline, lfs.fontStrikeout);
    if (labelsImage.isNull()) {
        labelsImage = QImage(1, 1, QImage::Format_ARGB32);
        labelsImage.fill(Qt::transparent);
    }
    window->setProperty("labelsTexture", QVariant::fromValue(labelsImage));
}

QVariantList OverlayService::buildZonesList(QScreen* screen) const
{
    // WARNING: One physical QScreen can back multiple virtual screens.
    // This overload picks the FIRST matching virtual screen ID from m_screenStates,
    // which is non-deterministic for QHash iteration. It should only be called in
    // single-overlay-per-physical-screen contexts (no virtual screens configured).
    // Callers with virtual screen context should use buildZonesList(screenId, physScreen) directly.

    // Virtual screens make the QScreen* overload ambiguous — screen center always
    // resolves to the same VS. Delegate to the first virtual screen's QString overload
    // so callers in single-physical-screen paths still get correct zone data.
    // Callers with an explicit virtual screen ID should use the QString overload directly.
    const QString physId = Utils::screenIdentifier(screen);
    auto* mgr = ScreenManager::instance();
    if (mgr && mgr->hasVirtualScreens(physId)) {
        const QStringList vsIds = mgr->virtualScreenIdsFor(physId);
        if (!vsIds.isEmpty()) {
            return buildZonesList(vsIds.first(), screen);
        }
        return {};
    }

    const QPoint screenCenter = screen->geometry().center();
    QString screenId = Utils::effectiveScreenIdAt(screenCenter, screen);
    return buildZonesList(screenId, screen);
}

QVariantList OverlayService::buildZonesList(const QString& screenId, QScreen* physScreen) const
{
    QVariantList zonesList;

    if (!physScreen) {
        return zonesList;
    }

    // Get the layout for this specific screen, fall back to global active layout
    // Per-screen assignments take priority so each monitor shows its own layout
    Layout* screenLayout = resolveScreenLayout(screenId);

    if (!screenLayout) {
        return zonesList;
    }

    const QRect overlayGeom = (m_screenStates.contains(screenId) && m_screenStates[screenId].overlayGeometry.isValid()
                                   ? m_screenStates[screenId].overlayGeometry
                                   : physScreen->geometry());
    qCDebug(lcOverlay) << "buildZonesList: screenId=" << screenId << "overlayGeom=" << overlayGeom
                       << "layout=" << screenLayout->name() << "zones=" << screenLayout->zones().size();

    for (auto* zone : screenLayout->zones()) {
        if (zone) {
            zonesList.append(zoneToVariantMap(zone, screenId, physScreen, overlayGeom, screenLayout));
        }
    }

    return zonesList;
}

QVariantMap OverlayService::zoneToVariantMap(Zone* zone, QScreen* screen, Layout* layout) const
{
    // Physical screen overload: delegates to screenId overload.
    // Defensive check: if virtual screens are configured for this physical screen,
    // screen center disambiguation always resolves to the same VS. Callers must
    // use the QString overload instead.
    const QString physId = Utils::screenIdentifier(screen);
    auto* mgr = ScreenManager::instance();
    if (mgr && mgr->hasVirtualScreens(physId)) {
        qCWarning(lcOverlay) << "zoneToVariantMap(Zone*, QScreen*, Layout*): physical screen" << physId
                             << "has virtual screens configured — caller should use QString overload.";
    }

    const QPoint screenCenter = screen->geometry().center();
    QString screenId = Utils::effectiveScreenIdAt(screenCenter, screen);
    QRect overlayGeom = (m_screenStates.contains(screenId) && m_screenStates[screenId].overlayGeometry.isValid()
                             ? m_screenStates[screenId].overlayGeometry
                             : screen->geometry());
    return zoneToVariantMap(zone, screenId, screen, overlayGeom, layout);
}

QVariantMap OverlayService::zoneToVariantMap(Zone* zone, const QString& screenId, QScreen* physScreen,
                                             const QRect& overlayGeometry, Layout* layout) const
{
    QVariantMap map;

    // Null check to prevent SIGSEGV
    if (!zone) {
        qCWarning(lcOverlay) << "Zone is null";
        return map;
    }

    // Calculate zone geometry with gaps applied (matches snap geometry).
    // Uses the layout's geometry preference: available area (excluding panels/taskbars)
    // or full screen geometry depending on useFullScreenGeometry setting.
    // Calculate zone geometry with gaps, auto-resolving virtual screen geometry
    QRectF geom = GeometryUtils::getZoneGeometryForScreenF(zone, physScreen, screenId, layout, m_settings);

    // Convert to overlay-local coordinates: virtual screens use the overlay rect origin,
    // physical screens use the QScreen origin
    const bool isVirtual = VirtualScreenId::isVirtual(screenId);
    QRectF overlayGeom = isVirtual ? GeometryUtils::availableAreaToOverlayCoordinates(geom, overlayGeometry)
                                   : GeometryUtils::availableAreaToOverlayCoordinates(geom, physScreen);

    map[JsonKeys::Id] = zone->id().toString(); // Include zone ID for stable selection
    map[JsonKeys::X] = overlayGeom.x();
    map[JsonKeys::Y] = overlayGeom.y();
    map[JsonKeys::Width] = overlayGeom.width();
    map[JsonKeys::Height] = overlayGeom.height();
    map[JsonKeys::ZoneNumber] = zone->zoneNumber();
    map[JsonKeys::Name] = zone->name();
    map[JsonKeys::IsHighlighted] = zone->isHighlighted();

    // Always include useCustomColors flag so QML can check it
    map[JsonKeys::UseCustomColors] = zone->useCustomColors();

    // Always include zone colors as hex strings (ARGB format) so QML can use them
    // when useCustomColors is true. QML expects color strings, not QColor objects.
    // This allows QML to always have access to zone colors and decide whether to use them.
    map[JsonKeys::HighlightColor] = zone->highlightColor().name(QColor::HexArgb);
    map[JsonKeys::InactiveColor] = zone->inactiveColor().name(QColor::HexArgb);
    map[JsonKeys::BorderColor] = zone->borderColor().name(QColor::HexArgb);

    // Always include appearance properties so QML can use them when useCustomColors is true
    map[JsonKeys::ActiveOpacity] = zone->activeOpacity();
    map[JsonKeys::InactiveOpacity] = zone->inactiveOpacity();
    map[JsonKeys::BorderWidth] = zone->borderWidth();
    map[JsonKeys::BorderRadius] = zone->borderRadius();

    // ═══════════════════════════════════════════════════════════════════════════════
    // Overlay display mode cascade: zone → layout → global
    // ═══════════════════════════════════════════════════════════════════════════════
    int resolvedDisplayMode = 0; // default: ZoneRectangles
    if (zone->overlayDisplayMode() >= 0) {
        resolvedDisplayMode = zone->overlayDisplayMode();
    } else if (layout && layout->overlayDisplayMode() >= 0) {
        resolvedDisplayMode = layout->overlayDisplayMode();
    } else if (m_settings) {
        resolvedDisplayMode = static_cast<int>(m_settings->overlayDisplayMode());
    }
    map[JsonKeys::OverlayDisplayMode] = resolvedDisplayMode;

    // Relative geometry for LayoutPreview rendering (miniature zone thumbnail)
    const QRectF relGeo = zone->relativeGeometry();
    QVariantMap relGeoMap;
    relGeoMap[JsonKeys::X] = relGeo.x();
    relGeoMap[JsonKeys::Y] = relGeo.y();
    relGeoMap[JsonKeys::Width] = relGeo.width();
    relGeoMap[JsonKeys::Height] = relGeo.height();
    map[JsonKeys::RelativeGeometry] = relGeoMap;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Shader-specific data (ZoneDataProvider texture)
    // ═══════════════════════════════════════════════════════════════════════════════

    // Normalized coordinates 0-1 over the overlay window. For virtual screens,
    // the overlay covers the virtual screen geometry (not the full physical screen),
    // so normalize against the overlay geometry.
    const QRectF normGeom = QRectF(overlayGeometry);
    const qreal ow = normGeom.width() > 0 ? normGeom.width() : 1.0;
    const qreal oh = normGeom.height() > 0 ? normGeom.height() : 1.0;
    map[NormalizedX] = overlayGeom.x() / ow;
    map[NormalizedY] = overlayGeom.y() / oh;
    map[NormalizedWidth] = overlayGeom.width() / ow;
    map[NormalizedHeight] = overlayGeom.height() / oh;

    // Fill color (RGBA premultiplied alpha) for shader
    QColor fillColor = zone->useCustomColors() ? zone->highlightColor()
                                               : (m_settings ? m_settings->highlightColor() : QColor(Qt::blue));
    qreal alpha = zone->useCustomColors() ? zone->activeOpacity() : (m_settings ? m_settings->activeOpacity() : 0.5);
    map[FillR] = fillColor.redF() * alpha;
    map[FillG] = fillColor.greenF() * alpha;
    map[FillB] = fillColor.blueF() * alpha;
    map[FillA] = alpha;

    // Border color (RGBA) for shader
    QColor borderClr =
        zone->useCustomColors() ? zone->borderColor() : (m_settings ? m_settings->borderColor() : QColor(Qt::white));
    map[BorderR] = borderClr.redF();
    map[BorderG] = borderClr.greenF();
    map[BorderB] = borderClr.blueF();
    map[BorderA] = borderClr.alphaF();

    // Shader params: borderRadius, borderWidth (from zone or settings)
    map[ShaderBorderRadius] =
        zone->useCustomColors() ? zone->borderRadius() : (m_settings ? m_settings->borderRadius() : 8);
    map[ShaderBorderWidth] =
        zone->useCustomColors() ? zone->borderWidth() : (m_settings ? m_settings->borderWidth() : 2);

    return map;
}

void OverlayService::updateZonesForAllWindows()
{
    m_zoneDataDirty = false;

    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        const QString& screenId = it.key();
        QQuickWindow* window = it.value().overlayWindow;

        if (!window) {
            continue;
        }

        QScreen* physScreen = m_screenStates.value(screenId).overlayPhysScreen;
        QVariantList zones = buildZonesList(screenId, physScreen);
        QVariantList patched = patchZonesWithHighlight(zones, window);

        int highlightedCount = 0;
        for (const QVariant& z : patched) {
            if (z.toMap().value(QLatin1String("isHighlighted")).toBool()) {
                ++highlightedCount;
            }
        }

        writeQmlProperty(window, QStringLiteral("zones"), patched);
        writeQmlProperty(window, QStringLiteral("zoneCount"), patched.size());
        writeQmlProperty(window, QStringLiteral("highlightedCount"), highlightedCount);

        if (useShaderForScreen(screenId)) {
            Layout* screenLayout = resolveScreenLayout(screenId);
            updateLabelsTextureForWindow(window, patched, physScreen, screenLayout);
        }
    }

    ++m_zoneDataVersion;
    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* w = it_.value().overlayWindow;
        if (w) {
            writeQmlProperty(w, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
        }
    }
}

} // namespace PlasmaZones
