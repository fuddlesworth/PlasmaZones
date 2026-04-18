// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "internal.h"
#include "../overlayservice.h"
#include "../../core/logging.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutUtils.h>
#include "../../core/screenmanager.h"
#include "../../core/utils.h"
#include <QQuickWindow>
#include <QScreen>
#include <QSet>
#include <QQmlEngine>
#include <QGuiApplication>

#include <PhosphorLayer/ILayerShellTransport.h>
#include <PhosphorLayer/Surface.h>
#include "pz_roles.h"

namespace PlasmaZones {

namespace {

// Result of OSD window preparation
struct OsdWindowSetup
{
    QQuickWindow* window = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 16.0 / 9.0;

    explicit operator bool() const
    {
        return window != nullptr;
    }
};

// Center an OSD/layer window within a screen geometry using layer surface margins.
// physScreenGeom is the full physical screen; targetGeom is the area to center within
// (same as physScreenGeom for physical screens, or a sub-region for virtual screens).
// Writes through the PhosphorLayer transport handle — keeps the daemon off the
// PhosphorShell API so OSDs can migrate to XdgToplevelTransport (or any future
// transport) without edits here.
void centerLayerWindowOnScreen(PhosphorLayer::ITransportHandle* handle, const QRect& physScreenGeom,
                               const QRect& targetGeom, int osdWidth, int osdHeight)
{
    if (!handle) {
        qCWarning(lcOverlay) << "centerLayerWindowOnScreen: no transport handle — surface was not warmed";
        return;
    }
    // Position OSD within the VS sub-region AND clamp it to never bleed past
    // the VS's edges. Naive centering computes `(vsOffset + (vsW - osd) / 2)`
    // but, for an OSD wider than the VS (e.g. a long navigation label on a
    // narrow half-screen VS), that value can push the right edge of the OSD
    // into the neighbouring VS. The qMin clamps cap the centre position at
    // "rightmost column where the OSD still fits entirely inside the VS";
    // the qMax floors cover the pathological case where the OSD is wider
    // than the VS itself (fall back to VS-aligned left edge).
    const int vsOffsetX = targetGeom.x() - physScreenGeom.x();
    const int vsOffsetY = targetGeom.y() - physScreenGeom.y();
    const int idealCenterX = vsOffsetX + qMax(0, (targetGeom.width() - osdWidth) / 2);
    const int idealCenterY = vsOffsetY + qMax(0, (targetGeom.height() - osdHeight) / 2);
    const int maxCenterX = qMax(vsOffsetX, vsOffsetX + targetGeom.width() - osdWidth);
    const int maxCenterY = qMax(vsOffsetY, vsOffsetY + targetGeom.height() - osdHeight);
    const int targetCenterX = qMin(idealCenterX, maxCenterX);
    const int targetCenterY = qMin(idealCenterY, maxCenterY);
    const int rightMargin = qMax(0, physScreenGeom.width() - targetCenterX - osdWidth);
    const int bottomMargin = qMax(0, physScreenGeom.height() - targetCenterY - osdHeight);

    handle->setAnchors(PhosphorLayer::AnchorAll);
    handle->setMargins(QMargins(targetCenterX, targetCenterY, rightMargin, bottomMargin));
}

// Calculate OSD size and center window. `surface` resolves the transport handle;
// callers pass the state's layoutOsdSurface / navigationOsdSurface.
void sizeAndCenterOsd(QQuickWindow* window, PhosphorLayer::Surface* surface, QScreen* physScreen,
                      const QRect& targetGeom, qreal previewAspectRatio)
{
    constexpr int osdWidth = 280;
    // Clamp AR to sane range to prevent absurd OSD sizes
    const qreal safeAR = qBound(0.5, previewAspectRatio, 4.0);
    const int osdHeight = static_cast<int>(200 / safeAR) + 80;
    if (window) {
        window->setWidth(osdWidth);
        window->setHeight(osdHeight);
    }
    const QRect physGeom = physScreen ? physScreen->geometry() : targetGeom;
    centerLayerWindowOnScreen(surface ? surface->transport() : nullptr, physGeom, targetGeom, osdWidth, osdHeight);
}

} // namespace

bool OverlayService::prepareLayoutOsdWindow(QQuickWindow*& window, PhosphorLayer::Surface*& outSurface,
                                            QScreen*& outPhysScreen, QRect& screenGeom, qreal& aspectRatio,
                                            const QString& screenId)
{
    // Resolve target screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* physScreen = resolveTargetScreen(screenId);
    if (!physScreen) {
        qCWarning(lcOverlay) << "No screen available for layout OSD";
        return false;
    }

    outPhysScreen = physScreen;

    // Use virtual screen geometry if applicable, otherwise physical
    screenGeom = resolveScreenGeometry(screenId);
    if (!screenGeom.isValid()) {
        screenGeom = physScreen->geometry();
    }

    QString effectiveId = screenId.isEmpty() ? Utils::screenIdentifier(physScreen) : screenId;

    if (!(m_screenStates.contains(effectiveId) && m_screenStates[effectiveId].layoutOsdWindow)) {
        createLayoutOsdWindow(effectiveId, physScreen);
    }

    const auto& state = m_screenStates.value(effectiveId);
    window = state.layoutOsdWindow;
    outSurface = state.layoutOsdSurface;
    if (!window) {
        qCWarning(lcOverlay) << "Failed to get layout OSD window";
        return false;
    }

    assertWindowOnScreen(window, physScreen, screenGeom);

    aspectRatio =
        (screenGeom.height() > 0) ? static_cast<qreal>(screenGeom.width()) / screenGeom.height() : (16.0 / 9.0);
    aspectRatio = qBound(0.5, aspectRatio, 4.0);

    return true;
}

void OverlayService::showLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId)
{
    showLayoutOsdImpl(layout, screenId, false);
}

void OverlayService::showLockedLayoutOsd(PhosphorZones::Layout* layout, const QString& screenId)
{
    showLayoutOsdImpl(layout, screenId, true);
}

void OverlayService::showLayoutOsdImpl(PhosphorZones::Layout* layout, const QString& screenId, bool locked)
{
    if (!layout) {
        qCDebug(lcOverlay) << "No layout provided for OSD";
        return;
    }

    if (!locked && layout->zones().isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << layout->name();
        return;
    }
    QQuickWindow* window = nullptr;
    PhosphorLayer::Surface* surface = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, surface, physScreen, screenGeom, aspectRatio, screenId)) {
        return;
    }

    resetOsdOverlayState(window);
    writeQmlProperty(window, QStringLiteral("locked"), locked);
    writeQmlProperty(window, QStringLiteral("layoutId"), layout->id().toString());
    writeQmlProperty(window, QStringLiteral("layoutName"), layout->name());
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("aspectRatioClass"),
                     PhosphorLayout::ScreenClassification::toString(layout->aspectRatioClass()));
    writeQmlProperty(window, QStringLiteral("category"), static_cast<int>(PhosphorZones::LayoutCategory::Manual));
    writeQmlProperty(window, QStringLiteral("autoAssign"), layout->autoAssign());
    writeAutotileMetadata(window, false, false);
    writeQmlProperty(window, QStringLiteral("zones"),
                     layout->zones().isEmpty()
                         ? QVariantList()
                         : PhosphorZones::LayoutUtils::zonesToVariantList(layout, PhosphorZones::ZoneField::Full));
    writeFontProperties(window, m_settings);

    qreal layoutAR = PhosphorLayout::ScreenClassification::aspectRatioForClass(layout->aspectRatioClass(), aspectRatio);
    sizeAndCenterOsd(window, surface, physScreen, screenGeom, layoutAR);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << (locked ? "Locked" : "Layout") << "OSD: layout=" << layout->name() << "screen=" << screenId;
}

void OverlayService::showLayoutOsd(const QString& id, const QString& name, const QVariantList& zones, int category,
                                   bool autoAssign, const QString& screenId, bool showMasterDot,
                                   bool producesOverlappingZones, const QString& zoneNumberDisplay, int masterCount)
{
    if (zones.isEmpty()) {
        qCDebug(lcOverlay) << "Skipping OSD for empty layout=" << name;
        return;
    }

    QQuickWindow* window = nullptr;
    PhosphorLayer::Surface* surface = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, surface, physScreen, screenGeom, aspectRatio, screenId)) {
        return;
    }

    // Reset locked/disabled state — window is reused across show calls, so a prior
    // showLockedLayoutOsd() or showDisabledOsd() would leave the overlay stuck on.
    resetOsdOverlayState(window);
    writeQmlProperty(window, QStringLiteral("layoutId"), id);
    writeQmlProperty(window, QStringLiteral("layoutName"), name);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    // Resolve aspectRatioClass from PhosphorZones::Layout* if available
    qreal layoutAR = aspectRatio;
    {
        QString arClass = QStringLiteral("any");
        auto uuidOpt = Utils::parseUuid(id);
        if (uuidOpt && m_layoutManager) {
            PhosphorZones::Layout* layout = m_layoutManager->layoutById(*uuidOpt);
            if (layout) {
                arClass = PhosphorLayout::ScreenClassification::toString(layout->aspectRatioClass());
                layoutAR =
                    PhosphorLayout::ScreenClassification::aspectRatioForClass(layout->aspectRatioClass(), aspectRatio);
            }
        }
        writeQmlProperty(window, QStringLiteral("aspectRatioClass"), arClass);
    }
    writeQmlProperty(window, QStringLiteral("category"), category);
    writeQmlProperty(window, QStringLiteral("autoAssign"), autoAssign);
    writeAutotileMetadata(window, showMasterDot, producesOverlappingZones, zoneNumberDisplay, masterCount);
    writeQmlProperty(window, QStringLiteral("zones"), zones);
    writeFontProperties(window, m_settings);

    sizeAndCenterOsd(window, surface, physScreen, screenGeom, layoutAR);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << "Layout OSD: name=" << name << "category=" << category << "screen=" << screenId;
}

void OverlayService::showDisabledOsd(const QString& reason, const QString& screenId)
{
    QQuickWindow* window = nullptr;
    PhosphorLayer::Surface* surface = nullptr;
    QScreen* physScreen = nullptr;
    QRect screenGeom;
    qreal aspectRatio = 0;
    if (!prepareLayoutOsdWindow(window, surface, physScreen, screenGeom, aspectRatio, screenId)) {
        return;
    }

    // Reset overlay state then set disabled — locked is intentionally false
    // (mutually exclusive with disabled, also enforced in QML).
    // Clear all layout-specific properties so stale data from a prior showLayoutOsd()
    // doesn't leak through the semi-transparent disabled overlay.
    resetOsdOverlayState(window);
    writeQmlProperty(window, QStringLiteral("disabled"), true);
    writeQmlProperty(window, QStringLiteral("disabledReason"), reason);
    writeQmlProperty(window, QStringLiteral("layoutId"), QString());
    writeQmlProperty(window, QStringLiteral("layoutName"), reason);
    writeQmlProperty(window, QStringLiteral("screenAspectRatio"), aspectRatio);
    writeQmlProperty(window, QStringLiteral("aspectRatioClass"), QStringLiteral("any"));
    writeQmlProperty(window, QStringLiteral("category"), 0);
    writeQmlProperty(window, QStringLiteral("autoAssign"), false);
    writeAutotileMetadata(window, false, false);
    writeQmlProperty(window, QStringLiteral("zones"), QVariantList());
    writeFontProperties(window, m_settings);

    sizeAndCenterOsd(window, surface, physScreen, screenGeom, aspectRatio);
    QMetaObject::invokeMethod(window, "show");
    qCInfo(lcOverlay) << "Disabled OSD: reason=" << reason << "screen=" << screenId;
}

// hideLayoutOsd / hideNavigationOsd (formerly Q_SLOTS connected to the QML
// dismissed() signal) are intentionally gone. The OSD dismiss path is now
// entirely QML-driven: the hide animation's ScriptAction flips _osdDismissed
// which re-evaluates the Window flags binding to include
// Qt.WindowTransparentForInput, and that's the whole mechanism. No C++ work
// runs on dismiss, so destroying the QQuickWindow (and paying the blocking
// ~QQuickWindow Vulkan teardown) never happens in the hot path. Windows are
// pre-warmed by warmUpLayoutOsd() / warmUpNavigationOsd() and stay alive
// for the daemon's lifetime; destroyLayoutOsdWindow() / destroy-
// NavigationOsdWindow() are only invoked from screen-removal / shutdown
// cleanup paths.

// Shared hot-plug hook for both OSDs. Called from warmUpLayoutOsd AND
// warmUpNavigationOsd so a caller that only warms one kind still gets
// per-screen instances on hot-plug for whichever OSDs it actually warmed.
// The per-OSD m_*OsdWarmed booleans inside the lambda gate which windows
// actually get created, so warming only one kind doesn't force the other.
void OverlayService::ensureOsdScreenAddedConnected()
{
    if (m_screenAddedConnected) {
        return;
    }
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen* screen) {
        auto* mgr2 = ScreenManager::instance();
        const QString physId = Utils::screenIdentifier(screen);
        const QStringList ids = mgr2 ? mgr2->effectiveIdsForPhysical(physId) : QStringList{physId};
        if (m_layoutOsdWarmed) {
            for (const QString& sid : ids) {
                if (!(m_screenStates.contains(sid) && m_screenStates[sid].layoutOsdWindow)) {
                    createLayoutOsdWindow(sid, screen);
                }
            }
        }
        if (m_navigationOsdWarmed) {
            for (const QString& sid : ids) {
                if (!(m_screenStates.contains(sid) && m_screenStates[sid].navigationOsdWindow)) {
                    createNavigationOsdWindow(sid, screen);
                }
            }
        }
    });
    m_screenAddedConnected = true;
}

void OverlayService::warmUpLayoutOsd()
{
    const QStringList effectiveIds = ScreenManager::effectiveScreenIdsWithFallback();

    for (const QString& sid : effectiveIds) {
        if (!(m_screenStates.contains(sid) && m_screenStates[sid].layoutOsdWindow)) {
            QScreen* physScreen = ScreenManager::resolvePhysicalScreen(sid);
            if (physScreen) {
                createLayoutOsdWindow(sid, physScreen);
            }
        }
    }
    m_layoutOsdWarmed = true;
    qCInfo(lcOverlay) << "Pre-warmed Layout OSD windows for" << effectiveIds.size() << "effective screens";

    ensureOsdScreenAddedConnected();
}

void OverlayService::warmUpNavigationOsd()
{
    const QStringList effectiveIds = ScreenManager::effectiveScreenIdsWithFallback();

    for (const QString& sid : effectiveIds) {
        if (!(m_screenStates.contains(sid) && m_screenStates[sid].navigationOsdWindow)) {
            QScreen* physScreen = ScreenManager::resolvePhysicalScreen(sid);
            if (physScreen) {
                createNavigationOsdWindow(sid, physScreen);
            }
        }
    }
    m_navigationOsdWarmed = true;
    qCInfo(lcOverlay) << "Pre-warmed Navigation OSD windows for" << effectiveIds.size() << "effective screens";

    // Install the hot-plug hook even if the layout OSD warmer never ran —
    // without this, a consumer that only warms the navigation OSD would
    // silently miss navigation OSDs on later-added screens.
    ensureOsdScreenAddedConnected();
}

void OverlayService::createLayoutOsdWindow(const QString& screenId, QScreen* physScreen)
{
    if (m_screenStates.contains(screenId) && m_screenStates[screenId].layoutOsdSurface) {
        return;
    }

    const auto role = PzRoles::LayoutOsd.withScopePrefix(
        QStringLiteral("plasmazones-layout-osd-%1-%2").arg(screenId).arg(++m_scopeGeneration));

    auto* surface = createLayerSurface(QUrl(QStringLiteral("qrc:/ui/LayoutOsd.qml")), physScreen, role, "layout OSD");
    if (!surface) {
        return;
    }

    // L3 v2 handles dismiss entirely in QML (see LayoutOsd.qml hideAnimation →
    // ScriptAction → _osdDismissed = true). The surface stays warmed for the
    // daemon's lifetime and is reused on every layout switch.
    auto& state = m_screenStates[screenId];
    state.layoutOsdSurface = surface;
    state.layoutOsdWindow = surface->window();
    state.layoutOsdPhysScreen = physScreen;
}

void OverlayService::destroyLayoutOsdWindow(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it == m_screenStates.end()) {
        return;
    }
    if (it->layoutOsdSurface) {
        it->layoutOsdSurface->deleteLater();
        it->layoutOsdSurface = nullptr;
        it->layoutOsdWindow = nullptr;
    }
    it->layoutOsdPhysScreen = nullptr;
}

void OverlayService::showNavigationOsd(bool success, const QString& action, const QString& reason,
                                       const QString& sourceZoneId, const QString& targetZoneId,
                                       const QString& screenId)
{
    qCDebug(lcOverlay) << "showNavigationOsd called: action=" << action << "reason=" << reason << "screen=" << screenId
                       << "success=" << success;

    // Only show OSD for successful actions - failures (no windows, no zones, etc.) don't need feedback
    if (!success) {
        qCDebug(lcOverlay) << "Skipping navigation OSD for failure:" << action << reason;
        return;
    }

    // Deduplicate: Skip if same action+reason+screen within 200ms (prevents duplicate from Qt signal + D-Bus signal)
    const QString actionKey = action + QLatin1Char(':') + reason;
    if (actionKey == m_lastNavigationActionKey && screenId == m_lastNavigationScreenId && m_lastNavigationTime.isValid()
        && m_lastNavigationTime.elapsed() < 200) {
        qCDebug(lcOverlay) << "Skipping duplicate navigation OSD:" << action << reason;
        return;
    }
    m_lastNavigationActionKey = actionKey;
    m_lastNavigationScreenId = screenId;
    m_lastNavigationTime.restart();

    // Resolve target screen using shared helper (handles virtual IDs, fallback chain)
    QScreen* physScreen = resolveTargetScreen(screenId);
    if (!physScreen) {
        qCWarning(lcOverlay) << "No screen available for navigation OSD";
        return;
    }

    // Use virtual screen geometry if applicable, otherwise physical
    QRect navScreenGeom = resolveScreenGeometry(screenId);
    if (!navScreenGeom.isValid()) {
        navScreenGeom = physScreen->geometry();
    }

    QString effectiveId = screenId.isEmpty() ? Utils::screenIdentifier(physScreen) : screenId;

    // Resolve per-screen layout (not the global m_layout which may belong to another screen)
    // Float, algorithm, rotate, and autotile-only actions don't need layout/zones
    static const QSet<QString> noLayoutActions{QStringLiteral("float"),        QStringLiteral("algorithm"),
                                               QStringLiteral("rotate"),       QStringLiteral("focus_master"),
                                               QStringLiteral("swap_master"),  QStringLiteral("master_ratio"),
                                               QStringLiteral("master_count"), QStringLiteral("retile"),
                                               QStringLiteral("swap_vs"),      QStringLiteral("rotate_vs")};
    const bool needsLayout = !noLayoutActions.contains(action);
    PhosphorZones::Layout* screenLayout = resolveScreenLayout(effectiveId);
    if ((needsLayout && !screenLayout) || (screenLayout && screenLayout->zones().isEmpty() && needsLayout)) {
        qCDebug(lcOverlay) << "No layout or zones for navigation OSD: screen=" << effectiveId
                           << "layout=" << (screenLayout ? screenLayout->name() : QStringLiteral("null"))
                           << "zones=" << (screenLayout ? screenLayout->zones().size() : 0) << "action=" << action;
        return;
    }

    // Reuse existing window for this screen (create only if not in map).
    // The window stays alive and visible across rapid navigation calls —
    // QML show() resets the animation and restarts the dismiss timer each time.
    // Cleanup happens when the dismiss timer expires: dismissed() signal →
    // hideNavigationOsd() slot → destroyNavigationOsdWindow(). This matches
    // the layout OSD pattern and avoids Vulkan surface create/destroy churn
    // that causes resource exhaustion and daemon freezes during rapid input.
    if (!(m_screenStates.contains(effectiveId) && m_screenStates[effectiveId].navigationOsdWindow)) {
        // Only try to create if we haven't failed before (prevents log spam)
        if (!m_navigationOsdCreationFailed.value(effectiveId, false)) {
            createNavigationOsdWindow(effectiveId, physScreen);
        }
    }

    const auto& navState = m_screenStates.value(effectiveId);
    auto* window = navState.navigationOsdWindow;
    auto* navSurface = navState.navigationOsdSurface;
    if (!window) {
        // Only warn once per screen to prevent log spam
        if (!m_navigationOsdCreationFailed.value(effectiveId, false)) {
            qCWarning(lcOverlay) << "Failed to get navigation OSD window for screen=" << effectiveId;
            m_navigationOsdCreationFailed.insert(effectiveId, true);
        }
        qCDebug(lcOverlay) << "No navigation OSD window for screen=" << effectiveId;
        return;
    }

    // Process reason field - for rotation/resnap, extract window count
    // Format: "clockwise:N" or "counterclockwise:N" or "resnap:N" where N is window count
    int windowCount = 1;
    QString displayReason = reason;
    if (reason.contains(QLatin1Char(':'))) {
        QStringList parts = reason.split(QLatin1Char(':'));
        if (parts.size() >= 2) {
            bool ok = false;
            int count = parts.at(1).toInt(&ok);
            if (ok && count > 0) {
                windowCount = count;
            }
            if (action == QStringLiteral("rotate")) {
                displayReason = parts.at(0); // "clockwise" or "counterclockwise"
            }
            // resnap keeps full reason for displayReason (optional)
        }
    }

    // Set OSD data
    writeQmlProperty(window, QStringLiteral("success"), success);
    writeQmlProperty(window, QStringLiteral("action"), action);
    writeQmlProperty(window, QStringLiteral("reason"), displayReason);
    writeQmlProperty(window, QStringLiteral("windowCount"), windowCount);

    // Pass source zone ID for swap operations
    writeQmlProperty(window, QStringLiteral("sourceZoneId"), sourceZoneId);

    // Build highlighted zone IDs list (target zones)
    QStringList highlightedZoneIds;
    if (!targetZoneId.isEmpty()) {
        highlightedZoneIds.append(targetZoneId);
    }
    writeQmlProperty(window, QStringLiteral("highlightedZoneIds"), highlightedZoneIds);

    // Use shared PhosphorZones::LayoutUtils with minimal fields for zone number lookup
    // (only need zoneId and zoneNumber, not name/appearance)
    QVariantList zonesList =
        PhosphorZones::LayoutUtils::zonesToVariantList(screenLayout, PhosphorZones::ZoneField::Minimal);
    writeQmlProperty(window, QStringLiteral("zones"), zonesList);

    // Ensure the window is on the correct Wayland output (must come before sizing —
    // assertWindowOnScreen calls setGeometry(screen) which would override setWidth/setHeight)
    assertWindowOnScreen(window, physScreen, navScreenGeom);

    // Read the QML-computed desired size. The NavigationOsd.qml root Window
    // exposes contentDesiredWidth/Height which cascade from messageLabel's
    // implicitWidth (QQuickText measures text synchronously when its text
    // property changes, so by the time writeQmlProperty("reason", ...) above
    // returns the binding has settled). Falls back to 240x70 if the property
    // can't be read — same as the previous hardcoded value.
    constexpr int kFallbackWidth = 240;
    constexpr int kFallbackHeight = 70;
    const QVariant widthVar = window->property("contentDesiredWidth");
    const QVariant heightVar = window->property("contentDesiredHeight");
    const int desiredWidth = widthVar.isValid() && widthVar.toInt() > 0 ? widthVar.toInt() : kFallbackWidth;
    const int desiredHeight = heightVar.isValid() && heightVar.toInt() > 0 ? heightVar.toInt() : kFallbackHeight;

    // Size and center: setWidth/setHeight AFTER assertWindowOnScreen so the final
    // QWindow geometry matches the OSD size (same pattern as sizeAndCenterOsd for LayoutOsd)
    const QRect screenGeom = navScreenGeom;
    window->setWidth(desiredWidth);
    window->setHeight(desiredHeight);
    const QRect physGeom = physScreen ? physScreen->geometry() : screenGeom;
    centerLayerWindowOnScreen(navSurface ? navSurface->transport() : nullptr, physGeom, screenGeom, desiredWidth,
                              desiredHeight);

    // Show with animation
    QMetaObject::invokeMethod(window, "show");

    qCInfo(lcOverlay) << "Showing navigation OSD: success=" << success << "action=" << action << "reason=" << reason
                      << "highlightedZones=" << highlightedZoneIds;
}

// hideNavigationOsd removed together with hideLayoutOsd — see the comment
// block above warmUpLayoutOsd() for the rationale. The m_lastNavigation*
// dedup state is cleared implicitly by the 200 ms timeout check in
// showNavigationOsd() itself (the OSD's ~1000 ms dismiss timer is far
// longer than the dedup window, so any dismiss is always past the
// relevant timeout by the time it fires — no manual clear needed).

void OverlayService::createNavigationOsdWindow(const QString& screenId, QScreen* physScreen)
{
    if (m_screenStates.contains(screenId) && m_screenStates[screenId].navigationOsdSurface) {
        return;
    }

    const auto role = PzRoles::NavigationOsd.withScopePrefix(
        QStringLiteral("plasmazones-navigation-osd-%1-%2").arg(screenId).arg(++m_scopeGeneration));

    auto* surface =
        createLayerSurface(QUrl(QStringLiteral("qrc:/ui/NavigationOsd.qml")), physScreen, role, "navigation OSD");
    if (!surface) {
        m_navigationOsdCreationFailed.insert(screenId, true);
        return;
    }

    auto& state = m_screenStates[screenId];
    state.navigationOsdSurface = surface;
    state.navigationOsdWindow = surface->window();
    state.navigationOsdPhysScreen = physScreen;
    m_navigationOsdCreationFailed.remove(screenId);
}

void OverlayService::destroyNavigationOsdWindow(const QString& screenId)
{
    auto it = m_screenStates.find(screenId);
    if (it != m_screenStates.end()) {
        if (it->navigationOsdSurface) {
            it->navigationOsdSurface->deleteLater();
            it->navigationOsdSurface = nullptr;
            it->navigationOsdWindow = nullptr;
        }
        it->navigationOsdPhysScreen = nullptr;
    }
    m_navigationOsdCreationFailed.remove(screenId);
}

} // namespace PlasmaZones
