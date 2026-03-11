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
#include "../rendering/zonelabeltexturebuilder.h"
#include <QQuickWindow>
#include <QScreen>
#include <QMutexLocker>
#include <KColorScheme>

namespace PlasmaZones {

void OverlayService::updateLabelsTextureForWindow(QQuickWindow* window, const QVariantList& patched, QScreen* screen,
                                                  Layout* screenLayout)
{
    Q_UNUSED(screen)
    if (!window) {
        return;
    }
    const bool showNumbers =
        screenLayout ? screenLayout->showZoneNumbers() : (m_settings ? m_settings->showZoneNumbers() : true);
    const QColor labelFontColor = m_settings ? m_settings->labelFontColor() : QColor(Qt::white);
    QColor backgroundColor = Qt::black;
    if (m_settings) {
        KColorScheme scheme(QPalette::Active, KColorScheme::View);
        backgroundColor = scheme.background(KColorScheme::NormalBackground).color();
    }
    const QString fontFamily = m_settings ? m_settings->labelFontFamily() : QString();
    const qreal fontSizeScale = m_settings ? m_settings->labelFontSizeScale() : 1.0;
    const int fontWeight = m_settings ? m_settings->labelFontWeight() : QFont::Bold;
    const bool fontItalic = m_settings ? m_settings->labelFontItalic() : false;
    const bool fontUnderline = m_settings ? m_settings->labelFontUnderline() : false;
    const bool fontStrikeout = m_settings ? m_settings->labelFontStrikeout() : false;
    const QSize size(qMax(1, static_cast<int>(window->width())), qMax(1, static_cast<int>(window->height())));
    QImage labelsImage =
        ZoneLabelTextureBuilder::build(patched, size, labelFontColor, showNumbers, backgroundColor, fontFamily,
                                       fontSizeScale, fontWeight, fontItalic, fontUnderline, fontStrikeout);
    if (labelsImage.isNull()) {
        labelsImage = QImage(1, 1, QImage::Format_ARGB32);
        labelsImage.fill(Qt::transparent);
    }
    window->setProperty("labelsTexture", QVariant::fromValue(labelsImage));
}

QVariantList OverlayService::buildZonesList(QScreen* screen) const
{
    QVariantList zonesList;

    if (!screen) {
        return zonesList;
    }

    // Get the layout for this specific screen, fall back to global active layout
    // Per-screen assignments take priority so each monitor shows its own layout
    Layout* screenLayout = resolveScreenLayout(screen);

    if (!screenLayout) {
        return zonesList;
    }

    qCDebug(lcOverlay) << "buildZonesList: screen=" << screen->name() << "screenGeom=" << screen->geometry()
                       << "availGeom=" << ScreenManager::actualAvailableGeometry(screen)
                       << "layout=" << screenLayout->name() << "zones=" << screenLayout->zones().size();

    for (auto* zone : screenLayout->zones()) {
        if (zone) {
            zonesList.append(zoneToVariantMap(zone, screen, screenLayout));
        }
    }

    return zonesList;
}

QVariantMap OverlayService::zoneToVariantMap(Zone* zone, QScreen* screen, Layout* layout) const
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
    // Layout's zonePadding/outerGap takes precedence over global settings
    QString screenId = Utils::screenIdentifier(screen);
    int zonePadding = GeometryUtils::getEffectiveZonePadding(layout, m_settings, screenId);
    EdgeGaps outerGaps = GeometryUtils::getEffectiveOuterGaps(layout, m_settings, screenId);
    bool useAvail = !(layout && layout->useFullScreenGeometry());
    QRectF geom = GeometryUtils::getZoneGeometryWithGaps(zone, screen, zonePadding, outerGaps, useAvail);

    // Convert to overlay window local coordinates
    // The overlay covers the full screen, but zones are positioned within available area
    QRectF overlayGeom = GeometryUtils::availableAreaToOverlayCoordinates(geom, screen);

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
    map[QLatin1String("relativeGeometry")] = relGeoMap;

    // ═══════════════════════════════════════════════════════════════════════════════
    // Shader-specific data (ZoneDataProvider texture)
    // ═══════════════════════════════════════════════════════════════════════════════

    // Normalized coordinates 0-1 over the overlay (full screen). relativeGeometry is 0-1
    // over the available area only; the overlay covers the full screen, so we must use
    // overlay-based normalized so shader (rect * iResolution) matches overlay pixels.
    const QRectF screenGeom = screen->geometry();
    const qreal ow = screenGeom.width() > 0 ? screenGeom.width() : 1.0;
    const qreal oh = screenGeom.height() > 0 ? screenGeom.height() : 1.0;
    map[QLatin1String("normalizedX")] = overlayGeom.x() / ow;
    map[QLatin1String("normalizedY")] = overlayGeom.y() / oh;
    map[QLatin1String("normalizedWidth")] = overlayGeom.width() / ow;
    map[QLatin1String("normalizedHeight")] = overlayGeom.height() / oh;

    // Fill color (RGBA premultiplied alpha) for shader
    QColor fillColor = zone->useCustomColors() ? zone->highlightColor()
                                               : (m_settings ? m_settings->highlightColor() : QColor(Qt::blue));
    qreal alpha = zone->useCustomColors() ? zone->activeOpacity() : (m_settings ? m_settings->activeOpacity() : 0.5);
    map[QLatin1String("fillR")] = fillColor.redF() * alpha;
    map[QLatin1String("fillG")] = fillColor.greenF() * alpha;
    map[QLatin1String("fillB")] = fillColor.blueF() * alpha;
    map[QLatin1String("fillA")] = alpha;

    // Border color (RGBA) for shader
    QColor borderClr =
        zone->useCustomColors() ? zone->borderColor() : (m_settings ? m_settings->borderColor() : QColor(Qt::white));
    map[QLatin1String("borderR")] = borderClr.redF();
    map[QLatin1String("borderG")] = borderClr.greenF();
    map[QLatin1String("borderB")] = borderClr.blueF();
    map[QLatin1String("borderA")] = borderClr.alphaF();

    // Shader params: borderRadius, borderWidth (from zone or settings)
    map[QLatin1String("shaderBorderRadius")] =
        zone->useCustomColors() ? zone->borderRadius() : (m_settings ? m_settings->borderRadius() : 8);
    map[QLatin1String("shaderBorderWidth")] =
        zone->useCustomColors() ? zone->borderWidth() : (m_settings ? m_settings->borderWidth() : 2);

    return map;
}

void OverlayService::updateZonesForAllWindows()
{
    m_zoneDataDirty = false;

    for (auto it = m_overlayWindows.begin(); it != m_overlayWindows.end(); ++it) {
        QScreen* screen = it.key();
        QQuickWindow* window = it.value();

        if (!window) {
            continue;
        }

        QVariantList zones = buildZonesList(screen);
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

        if (useShaderForScreen(screen)) {
            Layout* screenLayout = resolveScreenLayout(screen);
            updateLabelsTextureForWindow(window, patched, screen, screenLayout);
        }
    }

    ++m_zoneDataVersion;
    for (auto* w : std::as_const(m_overlayWindows)) {
        if (w) {
            writeQmlProperty(w, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
        }
    }
}

} // namespace PlasmaZones
