// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "daemon/overlayservice.h"
#include "core/platform/logging.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/Zone.h>
#include "core/types/constants.h"
#include "core/utils/geometryutils.h"
#include "core/utils/utils.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "daemon/rendering/zonelabeltexturebuilder.h"
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
// - they're consumed downstream by the shader uniforms - so they're
// deliberately excluded from the cache key. Including them would churn the
// cache on every highlight change and defeat the whole optimization.
quint64 hashLabelsTextureInputs(const QVariantList& patched, const QSize& size, qreal devicePixelRatio,
                                bool showNumbers, const LabelFontSettings& lfs)
{
    // NOTE: inside `namespace PlasmaZones {}` the unqualified name `qHash`
    // resolves to a user-defined overload and never falls through to Qt's global `::qHash`: a
    // using-declaration in core/types/types.h pulls one in, which stops ordinary lookup here,
    // and an overload on an argument's own namespace type is found by ADL regardless. Always
    // fully qualify.
    //
    // Mixer is the standard boost::hash_combine / Fibonacci-constant form.
    // Earlier iterations used (h << 12) + (h >> 4), which has asymmetric and
    // poor avalanche on the low bits - the standard (h << 6) + (h >> 2)
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
    // The ratio the payload is rasterised at. Without it, moving the overlay
    // to a differently-scaled output at the same logical size reuses the old
    // texture at the old resolution — the labels stay soft (or over-sharp)
    // until something else in the key happens to change.
    mix(::qHash(devicePixelRatio));
    mix(::qHash(static_cast<uint>(showNumbers)));
    mix(::qHash(static_cast<uint>(lfs.fontColor.rgba())));
    mix(::qHash(static_cast<uint>(lfs.backgroundColor.rgba())));
    mix(::qHash(lfs.fontFamily));
    // fontSizeScale is a qreal - bit-cast through quint64 so sub-integer
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
        mix(::qHash(z.value(::PhosphorZones::ZoneJsonKeys::ZoneNumber).toInt()));
        // PhosphorZones::Zone rects in the overlay use qreal; hash the full bit pattern so
        // sub-pixel geometry changes still produce a distinct key.
        const double fields[4] = {
            z.value(::PhosphorZones::ZoneJsonKeys::X).toDouble(),
            z.value(::PhosphorZones::ZoneJsonKeys::Y).toDouble(),
            z.value(::PhosphorZones::ZoneJsonKeys::Width).toDouble(),
            z.value(::PhosphorZones::ZoneJsonKeys::Height).toDouble(),
        };
        for (double f : fields) {
            quint64 bits = 0;
            std::memcpy(&bits, &f, sizeof(bits));
            mix(::qHash(bits));
        }
    }
    // Never return 0 - it's the sentinel for "cache invalid". Collapse the
    // zero-hash corner case to a fixed non-zero value.
    return h ? static_cast<quint64>(h) : 1ULL;
}
} // namespace

void OverlayService::updateLabelsTextureForWindow(QQuickItem* slot, const QVariantList& patched, QScreen* screen,
                                                  PhosphorZones::Layout* screenLayout, const QString& screenId)
{
    Q_UNUSED(screen)
    if (!slot) {
        return;
    }
    const LabelFontSettings lfs = extractLabelFontSettings(m_settings);
    // Slot is anchors.fill: parent on the shell window, so its size
    // matches the shell - which has been sized to the per-screen rect.
    const QSize size(qMax(1, static_cast<int>(slot->width())), qMax(1, static_cast<int>(slot->height())));
    // The ratio the zone shader samples this texture at: ShaderEffect binds
    // iResolution as logical x effectiveDevicePixelRatio for the overlay path
    // (it reports requiresPhysicalResolution), so the labels must be rasterised
    // at the same ratio or they arrive upscaled. Window-derived rather than
    // screen-derived on purpose — on Wayland QScreen::devicePixelRatio is the
    // wl_output INTEGER buffer scale, which is 2 on a 1.15 output.
    const qreal dpr = slot->window() ? slot->window()->effectiveDevicePixelRatio() : 1.0;

    // The screen id ARRIVES, rather than being recovered by scanning m_screenStates for the
    // slot pointer. Both callers already hold it, so that scan was an O(n) reverse lookup for
    // something known, and the warning arm below could fire for a slot that IS tracked simply
    // because the pointer comparison missed. Now it means what it says.
    //
    // What this does NOT change: the lookup is still a mutating one, because the hash write at
    // the tail needs a mutable handle, and updateZonesForAllWindows calls this while holding
    // an iterator into the same map. That is safe for one reason worth stating rather than
    // rediscovering — m_screenStates is never copied anywhere in the tree, so its refcount is
    // always 1 and the detach a non-const find() would perform never happens.
    auto stateIt = m_screenStates.find(screenId);
    PerScreenOverlayState* state = stateIt != m_screenStates.end() ? &stateIt.value() : nullptr;
    if (!state) {
        qCWarning(lcOverlay) << "updateLabelsTextureForWindow: screen" << screenId
                             << "not tracked in m_screenStates - labels-texture cache bypassed";
    }

    // A SetOverlayShowZoneNumbers context rule overrides the global setting for this
    // screen, matching the QML `showNumbers` property set in updateOverlayWindow so
    // both the property and the label-texture path agree. The per-layout hide still
    // wins (a layout that hides numbers keeps them hidden).
    //
    // screenId is always the caller's live key now, so the override resolves even on the
    // not-tracked path above, where the old reverse scan left the id EMPTY on a miss and so
    // resolved to no override at all. No user-visible verdict changed, though, and an earlier
    // version of this note calling it a behaviour change was wrong twice over: the not-tracked
    // arm is unreachable from either caller (one iterates m_screenStates, the other
    // early-returns because its constFind misses and leaves the slot null), and on the
    // reachable paths the scan always found the caller's OWN key, since one shell per key means
    // no two entries can answer the same item pointer. What the parameter removed is a reverse
    // scan whose miss branch could only have misfired for a caller that does not exist.
    const PhosphorZones::ContextOverlayOverride overlayOverride = overlayOverrideForScreen(m_layoutManager, screenId);
    const bool showNumbers = overlayOverride.showZoneNumbers.value_or(m_settings ? m_settings->showZoneNumbers() : true)
        && (!screenLayout || screenLayout->showZoneNumbers());

    const quint64 newHash = hashLabelsTextureInputs(patched, size, dpr, showNumbers, lfs);
    if (state && state->labelsTextureHash == newHash) {
        return;
    }

    // Sparse glyph-tile payload (a few hundred KB) instead of a full-overlay
    // image; the render node composites it into the screen-addressed texture.
    // An empty payload (numbers off / no zones) is fine: the node binds a 1×1
    // transparent fallback, so no full-screen texture is allocated.
    const PhosphorRendering::ZoneLabelTexture labels = ZoneLabelTextureBuilder::build(
        patched, size, dpr, lfs.fontColor, showNumbers, lfs.backgroundColor, lfs.fontFamily, lfs.fontSizeScale,
        lfs.fontWeight, lfs.fontItalic, lfs.fontUnderline, lfs.fontStrikeout);
    writeQmlProperty(slot, QString(OverlayQmlPropertyNames::LabelsTexture), QVariant::fromValue(labels));
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
    const QString physId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
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

    // One constFind, not contains() plus two const operator[] reads: each of those
    // returns a PerScreenOverlayState by VALUE, so the old form copied the whole
    // per-screen state twice on a path that runs per zone-data update.
    const auto stateIt = m_screenStates.constFind(screenId);
    const QRect overlayGeom =
        (stateIt != m_screenStates.constEnd() && stateIt->overlayGeometry.isValid() ? stateIt->overlayGeometry
                                                                                    : physScreen->geometry());
    qCDebug(lcOverlay) << "buildZonesList: screenId=" << screenId << "overlayGeom=" << overlayGeom
                       << "layout=" << screenLayout->name() << "zones=" << screenLayout->zones().size();

    // Resolve the per-context overlay override ONCE for the screen (it is invariant
    // across the screen's zones) and thread it into each zoneToVariantMap call.
    const PhosphorZones::ContextOverlayOverride overlayOverride = overlayOverrideForScreen(m_layoutManager, screenId);
    for (auto* zone : screenLayout->zones()) {
        if (zone) {
            zonesList.append(zoneToVariantMap(zone, screenId, physScreen, overlayGeom, screenLayout, overlayOverride));
        }
    }

    return zonesList;
}

QVariantMap OverlayService::zoneToVariantMap(PhosphorZones::Zone* zone, const QString& screenId, QScreen* physScreen,
                                             const QRect& overlayGeometry, PhosphorZones::Layout* layout,
                                             const PhosphorZones::ContextOverlayOverride& overlayOverride) const
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
    QRectF geom = GeometryUtils::getZoneGeometryForScreenF(m_screenManager, zone, physScreen, screenId, layout,
                                                           m_settings, m_layoutManager);

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
    // Bounded like the shader keys further down. Zone::fromJson deliberately
    // does not clamp, so a legacy layout can hold a per-zone border wider than
    // any surface now offers; without this the Rectangle-based overlays and
    // snap assist would draw it while the shader overlay and the settings
    // preview drew the clamped value.
    map[::PhosphorZones::ZoneJsonKeys::BorderWidth] = qBound(0, zone->borderWidth(), ConfigDefaults::borderWidthMax());
    map[::PhosphorZones::ZoneJsonKeys::BorderRadius] =
        qBound(0, zone->borderRadius(), ConfigDefaults::borderRadiusMax());

    // ═══════════════════════════════════════════════════════════════════════════════
    // Overlay display mode cascade: zone → context rule → layout → global
    // ═══════════════════════════════════════════════════════════════════════════════
    // A context overlay-style rule slots between the per-zone override and the
    // layout value, mirroring the precedence useShaderForScreen applies. The override
    // is screen-invariant across zones, so the caller resolves it once and passes it
    // in rather than re-resolving (and re-deriving the cache key) per zone.
    int resolvedDisplayMode = ConfigDefaults::overlayDisplayMode();
    if (zone->overlayDisplayMode() >= 0) {
        resolvedDisplayMode = zone->overlayDisplayMode();
    } else if (overlayOverride.style) {
        resolvedDisplayMode = *overlayOverride.style;
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

    // Fill / border reads cascade zone-custom → context rule → global config.
    // A per-zone custom value wins outright; otherwise the context overlay rule
    // (overlayOverride, resolved above) overrides the global setting. Only the
    // fill RGB is used (premultiplied by the separate opacity), so the colour's
    // own alpha is stripped here — no double-apply.
    QColor fillColor = zone->useCustomColors()
        ? zone->highlightColor()
        : overlayOverride.highlightColor.value_or(m_settings ? m_settings->highlightColor()
                                                             : ConfigDefaults::highlightFallbackColor());
    qreal alpha = zone->useCustomColors()
        ? zone->activeOpacity()
        : overlayOverride.activeOpacity.value_or(m_settings ? m_settings->activeOpacity()
                                                            : ConfigDefaults::activeOpacity());
    map[FillR] = fillColor.redF() * alpha;
    map[FillG] = fillColor.greenF() * alpha;
    map[FillB] = fillColor.blueF() * alpha;
    map[FillA] = alpha;

    // Border color (RGBA) for shader
    QColor borderClr = zone->useCustomColors()
        ? zone->borderColor()
        : overlayOverride.borderColor.value_or(m_settings ? m_settings->borderColor()
                                                          : ConfigDefaults::borderFallbackColor());
    map[BorderR] = borderClr.redF();
    map[BorderG] = borderClr.greenF();
    map[BorderB] = borderClr.blueF();
    map[BorderA] = borderClr.alphaF();

    // Shader params: borderRadius, borderWidth (zone → context rule → global).
    // Bounded on the way out whichever tier won: the per-zone value comes from
    // a layout file that is never rewritten on load, so a hand-edited or
    // legacy-wide value reaches here unclamped and would otherwise be drawn.
    map[ShaderBorderRadius] =
        qBound(0,
               zone->useCustomColors() ? zone->borderRadius()
                                       : overlayOverride.borderRadius.value_or(
                                             m_settings ? m_settings->borderRadius() : ConfigDefaults::borderRadius()),
               ConfigDefaults::borderRadiusMax());
    map[ShaderBorderWidth] =
        qBound(0,
               zone->useCustomColors() ? zone->borderWidth()
                                       : overlayOverride.borderWidth.value_or(
                                             m_settings ? m_settings->borderWidth() : ConfigDefaults::borderWidth()),
               ConfigDefaults::borderWidthMax());

    return map;
}

void OverlayService::updateZonesForAllWindows()
{
    m_zoneDataDirty = false;

    for (auto it = m_screenStates.begin(); it != m_screenStates.end(); ++it) {
        // A COPY of the key, not a reference into the hash node. The body writes QML
        // properties, whose bindings evaluate synchronously, and it hands this id to a
        // function that does its own find() on the same map — so a reference here aliases the
        // container it is used to look up. No route inserts into m_screenStates from a binding
        // (QML's whole reach into this class is one context property with one read-only
        // property and two public slots — ONE hide (hideLayoutPicker) and one shader-error reporter
        // (onShaderError), and it is the
        // reporter QML actually calls, from a signal handler), which makes the reference safe BY
        // ENUMERATION rather than by construction. A copy costs one atomic refcount bump and
        // needs no enumeration. The sibling loop in selector.cpp snapshots for its own, separate
        // reason — a completion lambda that COULD rehash the map under its iterators if a future
        // completion-path edit inserted a screen — so it is a
        // precedent for copying, not the same hazard as this one.
        const QString screenId = it.key();
        auto* slot = it.value().mainOverlaySlot();

        if (!slot) {
            continue;
        }

        // The loop already holds the iterator; value(screenId) would re-hash and copy
        // the whole PerScreenOverlayState to read one pointer out of it.
        QScreen* physScreen = it.value().overlayPhysScreen;
        QVariantList zones = buildZonesList(screenId, physScreen);
        QVariantList patched = patchZonesWithHighlight(zones, slot);

        int highlightedCount = 0;
        for (const QVariant& z : patched) {
            if (z.toMap().value(::PhosphorZones::ZoneJsonKeys::IsHighlighted).toBool()) {
                ++highlightedCount;
            }
        }

        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::Zones), patched);
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::ZoneCount), patched.size());
        writeQmlProperty(slot, QString(OverlayQmlPropertyNames::HighlightedCount), highlightedCount);

        if (useShaderForScreen(screenId)) {
            PhosphorZones::Layout* screenLayout = resolveScreenLayout(screenId);
            updateLabelsTextureForWindow(slot, patched, physScreen, screenLayout, screenId);
        }
    }

    ++m_zoneDataVersion;
    for (auto it_ = m_screenStates.constBegin(); it_ != m_screenStates.constEnd(); ++it_) {
        auto* slot = it_.value().mainOverlaySlot();
        if (slot) {
            writeQmlProperty(slot, QStringLiteral("zoneDataVersion"), m_zoneDataVersion);
        }
    }
}

} // namespace PlasmaZones
