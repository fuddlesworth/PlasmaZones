// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutManager.h>
#include <PhosphorZones/Zone.h>
#include "../../core/constants.h"
#include "../../core/geometryutils.h"
#include "../../core/utils.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "../rendering/zonelabeltexturebuilder.h"
#include <QCursor>
#include <QHashFunctions>
#include <QQuickWindow>
#include <QScreen>
#include <QMutexLocker>
#include <QGuiApplication>
#include <QPalette>
#include <PhosphorScreens/ScreenIdentity.h>

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

// Hash only the inputs that ZoneLabelTextureBuilder::build actually reads.
// Highlight state, colors, opacities, etc. do NOT affect the labels texture
// — they're consumed downstream by the shader uniforms — so they're
// deliberately excluded from the cache key. Including them would churn the
// cache on every highlight change and defeat the whole optimization.
quint64 hashLabelsTextureInputs(const QVariantList& patched, const QSize& size, bool showNumbers,
                                const LabelFontSettings& lfs)
{
    // NOTE: inside `namespace PlasmaZones {}` the unqualified name `qHash`
    // resolves to user-defined overloads (TilingStateKey, PhosphorZones::LayoutAssignmentKey)
    // and never falls through to Qt's global `::qHash`. Always fully qualify.
    //
    // Mixer is the standard boost::hash_combine / Fibonacci-constant form.
    // Earlier iterations used (h << 12) + (h >> 4), which has asymmetric and
    // poor avalanche on the low bits — the standard (h << 6) + (h >> 2)
    // distribution is well studied and substantially reduces false collision
    // risk. A collision here means updateLabelsTextureForWindow believes its
    // inputs are unchanged when they aren't, which displays stale zone-number
    // labels to the user: silent wrong output, so it's worth the tighter
    // mixer.
    size_t h = 0;
    const auto mix = [&h](size_t v) {
        h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    };
    mix(::qHash(static_cast<int>(size.width())));
    mix(::qHash(static_cast<int>(size.height())));
    mix(::qHash(static_cast<uint>(showNumbers)));
    mix(::qHash(static_cast<uint>(lfs.fontColor.rgba())));
    mix(::qHash(static_cast<uint>(lfs.backgroundColor.rgba())));
    mix(::qHash(lfs.fontFamily));
    // fontSizeScale is a qreal — bit-cast through quint64 so sub-integer
    // differences still distinguish hash entries.
    quint64 scaleBits = 0;
    const double scale = lfs.fontSizeScale;
    std::memcpy(&scaleBits, &scale, sizeof(scaleBits));
    mix(::qHash(scaleBits));
    mix(::qHash(static_cast<int>(lfs.fontWeight)));
    mix(::qHash(static_cast<uint>(lfs.fontItalic)));
    mix(::qHash(static_cast<uint>(lfs.fontUnderline)));
    mix(::qHash(static_cast<uint>(lfs.fontStrikeout)));
    for (const QVariant& zoneVar : patched) {
        const QVariantMap z = zoneVar.toMap();
        mix(::qHash(z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::ZoneNumber)).toInt()));
        // PhosphorZones::Zone rects in the overlay use qreal; hash the full bit pattern so
        // sub-pixel geometry changes still produce a distinct key.
        const double fields[4] = {
            z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::X)).toDouble(),
            z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::Y)).toDouble(),
            z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::Width)).toDouble(),
            z.value(QLatin1String(::PhosphorZones::ZoneJsonKeys::Height)).toDouble(),
        };
        for (double f : fields) {
            quint64 bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            mix(::qHash(bits));
        }
    }
    // Never return 0 — it's the sentinel for "cache invalid". Collapse the
    // zero-hash corner case to a fixed non-zero value.
    return h ? static_cast<quint64>(h) : 1ULL;
}
} // namespace

void OverlayService::updateLabelsTextureForWindow(QQuickWindow* window, const QVariantList& patched, QScreen* screen,
                                                  PhosphorZones::Layout* screenLayout)
{
    Q_UNUSED(screen)
    if (!window) {
        return;
    }
    const bool showNumbers =
        (m_settings ? m_settings->showZoneNumbers() : true) && (!screenLayout || screenLayout->showZoneNumbers());
    const LabelFontSettings lfs = extractLabelFontSettings(m_settings);
    const QSize size(qMax(1, static_cast<int>(window->width())), qMax(1, static_cast<int>(window->height())));

    // Hash-cache short-circuit.
    //
    // Without this, every call rebuilds a fresh QImage at the overlay's full
    // size (3200×1800 = 23 MB on a 4K monitor) and ships it through
    // QObject::setProperty, which Qt's QML meta-object layer change-detects
    // via QVariant::equals → QImage::operator== — a pixel-by-pixel memcmp of
    // that same 23 MB. The inputs change rarely (only when a zone layout or
    // font setting changes), so caching by an input hash lets us skip BOTH
    // the rebuild and the O(pixels) property-write equality check.
    //
    // This is what keeps refreshFromIdle() cheap — re-pushing zones after a
    // mid-drag trigger release hits the cache and costs one hash compute.
    //
    // Find the owning PerScreenOverlayState entry. updateZonesForAllWindows
    // is the only caller and iterates m_screenStates itself, so this lookup
    // must always succeed — warn + fail loudly otherwise, because a
    // nullptr state would silently defeat the cache (hash never stored →
    // next call rebuilds again → wasted 23 MB × 2 per tick).
    PerScreenOverlayState* state = nullptr;
    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        if (it.value().overlayWindow == window) {
            state = &it.value();
            break;
        }
    }
    if (!state) {
        qCWarning(lcOverlay) << "updateLabelsTextureForWindow: window not tracked in m_screenStates — "
                                "labels-texture cache bypassed";
        // Continue without caching so rendering stays correct; the user will
        // see label updates, just without the hot-path optimization.
    }

    const quint64 newHash = hashLabelsTextureInputs(patched, size, showNumbers, lfs);
    if (state && state->labelsTextureHash == newHash) {
        return; // inputs unchanged — skip the 23 MB build and the setProperty equality compare
    }

    QImage labelsImage = ZoneLabelTextureBuilder::build(patched, size, lfs.fontColor, showNumbers, lfs.backgroundColor,
                                                        lfs.fontFamily, lfs.fontSizeScale, lfs.fontWeight,
                                                        lfs.fontItalic, lfs.fontUnderline, lfs.fontStrikeout);
    if (labelsImage.isNull()) {
        labelsImage = QImage(1, 1, QImage::Format_ARGB32);
        labelsImage.fill(Qt::transparent);
    }
    window->setProperty("labelsTexture", QVariant::fromValue(labelsImage));
    if (state) {
        state->labelsTextureHash = newHash;
    }
}

QVariantList OverlayService::buildZonesList(QScreen* screen) const
{
    // WARNING: One physical QScreen can back multiple virtual screens.
    // When virtual screens are configured, this delegates to the first VS in config
    // order (virtualScreenIdsFor returns IDs in config order, not hash order).
    // Callers with an explicit virtual screen ID should use the QString overload directly.
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    auto* mgr = m_screenManager;
    if (mgr && mgr->hasVirtualScreens(physId)) {
        const QStringList vsIds = mgr->virtualScreenIdsFor(physId);
        if (!vsIds.isEmpty()) {
            return buildZonesList(vsIds.first(), screen);
        }
        return {};
    }

    const QPoint screenCenter = screen->geometry().center();
    QString screenId = Utils::effectiveScreenIdAt(m_screenManager, screenCenter, screen);
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
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);

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

QVariantMap OverlayService::zoneToVariantMap(PhosphorZones::Zone* zone, QScreen* screen,
                                             PhosphorZones::Layout* layout) const
{
    // Physical screen overload: delegates to screenId overload.
    // Defensive check: if virtual screens are configured for this physical screen,
    // screen center disambiguation always resolves to the same VS. Callers must
    // use the QString overload instead.
    const QString physId = Phosphor::Screens::ScreenIdentity::identifierFor(screen);
    auto* mgr = m_screenManager;
    if (mgr && mgr->hasVirtualScreens(physId)) {
        qCWarning(lcOverlay) << "zoneToVariantMap(Zone*, QScreen*, Layout*): physical screen" << physId
                             << "has virtual screens configured — caller should use QString overload.";
    }

    const QPoint screenCenter = screen->geometry().center();
    QString screenId = Utils::effectiveScreenIdAt(m_screenManager, screenCenter, screen);
    QRect overlayGeom = (m_screenStates.contains(screenId) && m_screenStates[screenId].overlayGeometry.isValid()
                             ? m_screenStates[screenId].overlayGeometry
                             : screen->geometry());
    return zoneToVariantMap(zone, screenId, screen, overlayGeom, layout);
}

QVariantMap OverlayService::zoneToVariantMap(PhosphorZones::Zone* zone, const QString& screenId, QScreen* physScreen,
                                             const QRect& overlayGeometry, PhosphorZones::Layout* layout) const
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
    QRectF geom =
        GeometryUtils::getZoneGeometryForScreenF(m_screenManager, zone, physScreen, screenId, layout, m_settings);

    // Convert to overlay-local coordinates: virtual screens use the overlay rect origin,
    // physical screens use the QScreen origin
    const bool isVirtual = PhosphorIdentity::VirtualScreenId::isVirtual(screenId);
    QRectF overlayGeom = isVirtual ? GeometryUtils::availableAreaToOverlayCoordinates(geom, overlayGeometry)
                                   : GeometryUtils::availableAreaToOverlayCoordinates(geom, physScreen);

    map[::PhosphorZones::ZoneJsonKeys::Id] = zone->id().toString(); // Include zone ID for stable selection
    map[::PhosphorZones::ZoneJsonKeys::X] = overlayGeom.x();
    map[::PhosphorZones::ZoneJsonKeys::Y] = overlayGeom.y();
    map[::PhosphorZones::ZoneJsonKeys::Width] = overlayGeom.width();
    map[::PhosphorZones::ZoneJsonKeys::Height] = overlayGeom.height();
    map[::PhosphorZones::ZoneJsonKeys::ZoneNumber] = zone->zoneNumber();
    map[::PhosphorZones::ZoneJsonKeys::Name] = zone->name();
    map[::PhosphorZones::ZoneJsonKeys::IsHighlighted] = zone->isHighlighted();

    // Always include useCustomColors flag so QML can check it
    map[::PhosphorZones::ZoneJsonKeys::UseCustomColors] = zone->useCustomColors();

    // Always include zone colors as hex strings (ARGB format) so QML can use them
    // when useCustomColors is true. QML expects color strings, not QColor objects.
    // This allows QML to always have access to zone colors and decide whether to use them.
    map[::PhosphorZones::ZoneJsonKeys::HighlightColor] = zone->highlightColor().name(QColor::HexArgb);
    map[::PhosphorZones::ZoneJsonKeys::InactiveColor] = zone->inactiveColor().name(QColor::HexArgb);
    map[::PhosphorZones::ZoneJsonKeys::BorderColor] = zone->borderColor().name(QColor::HexArgb);

    // Always include appearance properties so QML can use them when useCustomColors is true
    map[::PhosphorZones::ZoneJsonKeys::ActiveOpacity] = zone->activeOpacity();
    map[::PhosphorZones::ZoneJsonKeys::InactiveOpacity] = zone->inactiveOpacity();
    map[::PhosphorZones::ZoneJsonKeys::BorderWidth] = zone->borderWidth();
    map[::PhosphorZones::ZoneJsonKeys::BorderRadius] = zone->borderRadius();

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
    map[::PhosphorZones::ZoneJsonKeys::OverlayDisplayMode] = resolvedDisplayMode;

    // Relative geometry for LayoutPreview rendering (miniature zone thumbnail)
    const QRectF relGeo = zone->relativeGeometry();
    QVariantMap relGeoMap;
    relGeoMap[::PhosphorZones::ZoneJsonKeys::X] = relGeo.x();
    relGeoMap[::PhosphorZones::ZoneJsonKeys::Y] = relGeo.y();
    relGeoMap[::PhosphorZones::ZoneJsonKeys::Width] = relGeo.width();
    relGeoMap[::PhosphorZones::ZoneJsonKeys::Height] = relGeo.height();
    map[::PhosphorZones::ZoneJsonKeys::RelativeGeometry] = relGeoMap;

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
            PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);
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
