// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../EditorGapsModel.h"
#include "../services/ZoneManager.h"
#include "../undo/UndoController.h"
#include "../undo/commands/UpdateGapOverrideCommand.h"
#include "../undo/commands/UpdateFullScreenGeometryCommand.h"
#include "../undo/commands/UpdateFixedGeometryUnitCommand.h"
#include "../undo/commands/ToggleGeometryModeCommand.h"
#include "../undo/commands/UpdateFixedGeometryCommand.h"
#include "../helpers/SettingsDbusQueries.h"
#include "../../config/configdefaults.h"
#include "core/types/constants.h"
#include "core/platform/logging.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include "core/utils/utils.h"
#include <PhosphorLayoutApi/AspectRatioClass.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include "phosphor_i18n.h"
#include <QGuiApplication>
#include <QJsonObject>
#include <QPointer>
#include <QScreen>
#include <memory>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

int EditorController::overlayDisplayMode() const
{
    return m_overlayDisplayMode;
}

bool EditorController::hasOverlayDisplayModeOverride() const
{
    return m_overlayDisplayMode >= 0;
}

int EditorController::globalOverlayDisplayMode() const
{
    return m_cachedGlobalOverlayDisplayMode;
}

void EditorController::setOverlayDisplayMode(int mode)
{
    if (mode < -1) {
        mode = -1;
    }
    if (m_overlayDisplayMode != mode) {
        auto* cmd = new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OverlayDisplayMode,
                                                 m_overlayDisplayMode, mode);
        m_undoController->push(cmd);
    }
}

void EditorController::setOverlayDisplayModeDirect(int mode)
{
    if (mode < -1) {
        mode = -1;
    }
    if (m_overlayDisplayMode != mode) {
        m_overlayDisplayMode = mode;
        markUnsaved();
        Q_EMIT overlayDisplayModeChanged();
    }
}

void EditorController::clearOverlayDisplayModeOverride()
{
    setOverlayDisplayMode(-1);
}

bool EditorController::useFullScreenGeometry() const
{
    return m_useFullScreenGeometry;
}

void EditorController::setUseFullScreenGeometry(bool enabled)
{
    if (m_useFullScreenGeometry != enabled) {
        auto* cmd = new UpdateFullScreenGeometryCommand(this, m_useFullScreenGeometry, enabled);
        m_undoController->push(cmd);
    }
}

void EditorController::setUseFullScreenGeometryDirect(bool enabled)
{
    if (m_useFullScreenGeometry == enabled) {
        return;
    }
    const QSize previous = targetScreenSize();
    m_useFullScreenGeometry = enabled;
    // The area zones occupy grows or shrinks by the panels, and the
    // reference fixed zones are drawn against is that area, so it moves with
    // it.
    markUnsaved();
    Q_EMIT useFullScreenGeometryChanged();
    republishTargetScreenSize(previous);
}

bool EditorController::fixedGeometryInDevicePixels() const
{
    return m_fixedGeometryInDevicePixels;
}

void EditorController::setFixedGeometryInDevicePixels(bool enabled)
{
    if (m_fixedGeometryInDevicePixels != enabled) {
        auto* cmd = new UpdateFixedGeometryUnitCommand(this, m_fixedGeometryInDevicePixels, enabled);
        m_undoController->push(cmd);
    }
}

void EditorController::setFixedGeometryInDevicePixelsDirect(bool enabled)
{
    if (m_fixedGeometryInDevicePixels == enabled) {
        return;
    }
    const QSize previous = targetScreenSize();
    m_fixedGeometryInDevicePixels = enabled;
    if (enabled) {
        ensureTargetScreenScale();
    }
    // The fixed zones keep their numbers and change unit, so the reference
    // they are drawn against moves by the device-pixel scaling, and the
    // zones with it.
    markUnsaved();
    Q_EMIT fixedGeometryInDevicePixelsChanged();
    republishTargetScreenSize(previous);
}

int EditorController::aspectRatioClass() const
{
    return m_aspectRatioClass;
}

void EditorController::setAspectRatioClass(int cls)
{
    if (cls < 0 || cls > static_cast<int>(PhosphorLayout::AspectRatioClass::Portrait)) {
        return;
    }
    if (m_aspectRatioClass != cls) {
        auto* cmd = new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::AspectRatioClass,
                                                 m_aspectRatioClass, cls);
        m_undoController->push(cmd);
    }
}

void EditorController::setAspectRatioClassDirect(int cls)
{
    if (cls < 0 || cls > static_cast<int>(PhosphorLayout::AspectRatioClass::Portrait)) {
        return;
    }
    if (m_aspectRatioClass != cls) {
        m_aspectRatioClass = cls;
        markUnsaved();
        Q_EMIT aspectRatioClassChanged();
    }
}

// Shared helper: resolve a screen id to a QScreen*, falling back to primary.
// A virtual screen resolves to its physical monitor.
static QScreen* findTargetScreen(const QString& targetScreen)
{
    if (QScreen* screen = PhosphorScreens::ScreenIdentity::findByIdOrName(targetScreen)) {
        return screen;
    }
    return QGuiApplication::primaryScreen();
}

QSize EditorController::targetScreenSize() const
{
    const QSizeF logical = targetLogicalScreenSizeF();
    if (!m_fixedGeometryInDevicePixels || m_targetScreenScale <= 0.0) {
        return logical.toSize();
    }
    // Device pixels per logical pixel on each axis. The daemon's pixelSize
    // for the target's full rect fixes the ratio so the full area maps onto
    // exactly that many device pixels. It is used only when it agrees with
    // the scale to within a couple of device pixels over the full rect, which
    // rejects a size measured over a different rect (a virtual screen whose
    // own geometry the daemon could not give, so the full size below is the
    // whole monitor). Without it, or from a daemon older than the key, the
    // ratio is the scale itself.
    qreal ratioX = m_targetScreenScale;
    qreal ratioY = m_targetScreenScale;
    const QSize full = targetFullLogicalScreenSize();
    if (m_targetScreenPixelSize.isValid() && !full.isEmpty()) {
        const qreal tolerance = m_targetScreenScale + 1.0;
        if (qAbs(m_targetScreenPixelSize.width() - full.width() * m_targetScreenScale) <= tolerance
            && qAbs(m_targetScreenPixelSize.height() - full.height() * m_targetScreenScale) <= tolerance) {
            ratioX = static_cast<qreal>(m_targetScreenPixelSize.width()) / full.width();
            ratioY = static_cast<qreal>(m_targetScreenPixelSize.height()) / full.height();
        }
    }
    // The device pixels the logical size covers on the target output,
    // rounded once from the unrounded logical size.
    return QSize(qRound(logical.width() * ratioX), qRound(logical.height() * ratioY));
}

QSize EditorController::targetFullLogicalScreenSize() const
{
    // The full area of the target: a virtual screen's from the daemon (cached
    // by cacheVirtualScreenGeometry), else the physical monitor's. A virtual
    // screen whose geometry the daemon could not give falls back to its
    // physical monitor, the closest size the editor knows.
    if (m_virtualScreenSize.isValid()) {
        return m_virtualScreenSize;
    }
    QScreen* screen = findTargetScreen(m_targetScreen);
    return screen ? screen->geometry().size() : QSize(1920, 1080);
}

QSize EditorController::targetLogicalScreenSize() const
{
    return targetLogicalScreenSizeF().toSize();
}

QSizeF EditorController::targetLogicalScreenSizeF() const
{
    // Always the live area, whatever size the layout was authored for, so a
    // canvas pixel is a screen pixel in fullscreen editing and the windowed
    // canvas is that same picture scaled. Fixed zones that overflow the area
    // draw past the canvas edge, where the daemon clips them at the screen
    // edge, rather than the whole layout being shrunk to fit a canvas that
    // then no longer matches the screen (issue #1167).
    const QSize full = targetFullLogicalScreenSize();
    // Zones occupy the usable area unless the layout covers the full screen,
    // which is the area the canvas shows and the daemon lays fixed zones out
    // in, so it is the area their numbers are measured against.
    if (m_useFullScreenGeometry) {
        return QSizeF(full);
    }
    const int width = full.width() - m_insetLeft - m_insetRight;
    const int height = full.height() - m_insetTop - m_insetBottom;
    return (width > 0 && height > 0) ? QSizeF(width, height) : QSizeF(full);
}

int EditorController::insetLeft() const
{
    return m_insetLeft;
}

int EditorController::insetTop() const
{
    return m_insetTop;
}

int EditorController::insetRight() const
{
    return m_insetRight;
}

int EditorController::insetBottom() const
{
    return m_insetBottom;
}

void EditorController::setInsets(int left, int top, int right, int bottom)
{
    if (m_insetLeft == left && m_insetTop == top && m_insetRight == right && m_insetBottom == bottom)
        return;
    const QSize previous = targetScreenSize();
    m_insetLeft = left;
    m_insetTop = top;
    m_insetRight = right;
    m_insetBottom = bottom;
    Q_EMIT usableAreaInsetsChanged();
    // The insets bound the area zones occupy, which is the reference fixed
    // zones are drawn against unless the layout covers the full screen. They
    // land asynchronously from refreshUsableAreaInsets(), after a screen
    // switch, a change to the target screen's geometry or a panel moving on
    // it, so this is where that reference catches up.
    if (!m_useFullScreenGeometry) {
        republishTargetScreenSize(previous);
    }
}

void EditorController::refreshUsableAreaInsets()
{
    // For virtual screens, use the daemon's geometry (QScreen doesn't know about VS).
    // The daemon's getScreenGeometry and getAvailableGeometry handle VS IDs.
    // The request is stamped, and its replies carry the stamp and the screen
    // they were asked about: a switch while they are in flight makes them
    // answers for a screen that is no longer the target, and they are dropped.
    const QString requestedScreen = m_targetScreen;
    const quint64 generation = ++m_insetsRequestGeneration;
    QString screenId = m_targetScreen;
    bool isVirtual = PhosphorIdentity::VirtualScreenId::isVirtual(screenId);

    // For physical screens, pre-populate fullGeom from Qt as a fallback in case the
    // daemon D-Bus call fails. For virtual screens, fullGeom stays empty — the daemon
    // is the only source of VS geometry and the getScreenGeometry callback will set it.
    QRect fullGeom;
    if (!isVirtual) {
        QScreen* screen = findTargetScreen(m_targetScreen);
        if (!screen) {
            setInsets(0, 0, 0, 0);
            return;
        }
        fullGeom = screen->geometry();
        screenId = PhosphorScreens::ScreenIdentity::identifierFor(screen);
    }

    // Query the daemon for both full and available geometry via D-Bus.
    // The daemon's PhosphorScreens::ScreenManager handles VS IDs natively.
    QDBusMessage geoMsg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Screen), QStringLiteral("getScreenGeometry"));
    geoMsg << screenId;
    QDBusMessage availMsg = QDBusMessage::createMethodCall(
        QString(PhosphorProtocol::Service::Name), QString(PhosphorProtocol::Service::ObjectPath),
        QString(PhosphorProtocol::Service::Interface::Screen), QStringLiteral("getAvailableGeometry"));
    availMsg << screenId;

    auto* geoWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(geoMsg), this);
    auto* availWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(availMsg), this);

    // Use shared state to wait for both replies. The watchers are children of
    // this controller and their connections use it as context, so a reply
    // that outlives the controller is never delivered and no guard is needed.
    auto fullGeomResult = std::make_shared<QRect>(fullGeom);
    auto availGeomResult = std::make_shared<QRect>();
    auto pending = std::make_shared<int>(2);

    auto applyWhenReady = [this, fullGeomResult, availGeomResult, pending, generation, requestedScreen]() {
        if (--(*pending) > 0) {
            return;
        }
        if (generation != m_insetsRequestGeneration) {
            return;
        }
        QRect fg = *fullGeomResult;
        QRect ag = availGeomResult->isValid() ? *availGeomResult : fg;
        if (fg.isValid()) {
            applyUsableAreaInsets(fg, ag, requestedScreen);
        } else {
            setInsets(0, 0, 0, 0);
        }
    };

    connect(geoWatcher, &QDBusPendingCallWatcher::finished, this,
            [fullGeomResult, applyWhenReady](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<QRect> reply = *w;
                if (reply.isValid() && !reply.value().isEmpty()) {
                    *fullGeomResult = reply.value();
                }
                applyWhenReady();
            });
    connect(availWatcher, &QDBusPendingCallWatcher::finished, this,
            [availGeomResult, applyWhenReady](QDBusPendingCallWatcher* w) {
                w->deleteLater();
                QDBusPendingReply<QRect> reply = *w;
                if (reply.isValid() && !reply.value().isEmpty()) {
                    *availGeomResult = reply.value();
                }
                applyWhenReady();
            });
}

void EditorController::applyUsableAreaInsets(const QRect& fullGeom, const QRect& daemonAvailGeom,
                                             const QString& requestedScreen)
{
    QRect availGeom = daemonAvailGeom;

    // Fallback: try Qt's available geometry (works on some Wayland
    // compositors). Not for a virtual screen: Qt knows only its physical
    // monitor, whose available rect measured against the virtual screen's
    // full rect yields the offset between the two rather than any panel.
    if (availGeom == fullGeom && !PhosphorIdentity::VirtualScreenId::isVirtual(requestedScreen)) {
        QScreen* screen = findTargetScreen(requestedScreen);
        if (screen) {
            QRect qtAvail = screen->availableGeometry();
            if (qtAvail != fullGeom && qtAvail.isValid()) {
                availGeom = qtAvail;
            }
        }
    }

    // Compute insets: how much the available area is inset from each edge of the full screen
    int left = qMax(0, availGeom.left() - fullGeom.left());
    int top = qMax(0, availGeom.top() - fullGeom.top());
    int right = qMax(0, fullGeom.right() - availGeom.right());
    int bottom = qMax(0, fullGeom.bottom() - availGeom.bottom());

    setInsets(left, top, right, bottom);
}

void EditorController::toggleZoneGeometryMode(const QString& zoneId)
{
    if (!servicesReady("toggleZoneGeometryMode")) {
        return;
    }

    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        qCWarning(lcEditor) << "Zone not found for geometry mode toggle:" << zoneId;
        return;
    }

    int oldMode = zone.value(::PhosphorZones::ZoneJsonKeys::GeometryMode, 0).toInt();
    int newMode = (oldMode == 0) ? 1 : 0; // Toggle between Relative(0) and Fixed(1)

    QSize screenSize = targetScreenSize();
    qreal sw = screenSize.width();
    qreal sh = screenSize.height();

    QRectF oldRelGeo = m_zoneManager->extractZoneGeometry(zone);

    // Current fixed geometry (may not exist)
    QRectF oldFixedGeo;
    if (zone.contains(::PhosphorZones::ZoneJsonKeys::FixedX)) {
        oldFixedGeo = m_zoneManager->extractFixedGeometry(zone);
    }

    QRectF newRelGeo = oldRelGeo;
    QRectF newFixedGeo = oldFixedGeo;

    if (newMode == 1) {
        // Switching to Fixed: convert relative -> pixel
        newFixedGeo = QRectF(oldRelGeo.x() * sw, oldRelGeo.y() * sh, oldRelGeo.width() * sw, oldRelGeo.height() * sh);
    } else {
        // Switching to Relative: re-derive the relative rect from the pixels
        // against the current reference, so the zone stays where it is drawn
        // (the stored relative copy is kept only when there are no pixels)
        if (oldFixedGeo.isValid() && sw > 0 && sh > 0) {
            newRelGeo =
                QRectF(oldFixedGeo.x() / sw, oldFixedGeo.y() / sh, oldFixedGeo.width() / sw, oldFixedGeo.height() / sh);
        }
    }

    auto* cmd =
        new ToggleGeometryModeCommand(this, zoneId, oldMode, newMode, oldRelGeo, newRelGeo, oldFixedGeo, newFixedGeo);
    m_undoController->push(cmd);
    markUnsaved();
}

void EditorController::updateZoneFixedGeometry(const QString& zoneId, qreal x, qreal y, qreal w, qreal h)
{
    if (!servicesReady("updateZoneFixedGeometry")) {
        return;
    }

    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        return;
    }

    // Capture old state for undo
    QRectF oldFixed = m_zoneManager->extractFixedGeometry(zone);
    QRectF oldRelative = m_zoneManager->extractZoneGeometry(zone);

    // Validate fixed geometry, clamping only the fields this edit changes.
    // The property panel commits one field at a time and passes the zone's
    // own values for the rest, which may legitimately sit outside what an
    // edit can produce (a negative offset the file format allows, or a size
    // below the editor's minimum) and must come through untouched.
    if (x != oldFixed.x()) {
        x = qMax(0.0, x);
    }
    if (y != oldFixed.y()) {
        y = qMax(0.0, y);
    }
    if (w != oldFixed.width()) {
        w = qMax(static_cast<qreal>(EditorConstants::MinFixedZoneSize), w);
    }
    if (h != oldFixed.height()) {
        h = qMax(static_cast<qreal>(EditorConstants::MinFixedZoneSize), h);
    }
    QRectF newFixed(x, y, w, h);

    // Compute new relative fallback
    QSizeF ss = m_zoneManager->effectiveScreenSizeF();
    QRectF newRelative(x / ss.width(), y / ss.height(), w / ss.width(), h / ss.height());

    // Skip if nothing changed
    const qreal tolerance = 0.5; // Sub-pixel tolerance for fixed coords
    if (qAbs(oldFixed.x() - newFixed.x()) < tolerance && qAbs(oldFixed.y() - newFixed.y()) < tolerance
        && qAbs(oldFixed.width() - newFixed.width()) < tolerance
        && qAbs(oldFixed.height() - newFixed.height()) < tolerance) {
        return;
    }

    auto* cmd = new UpdateFixedGeometryCommand(QPointer<ZoneManager>(m_zoneManager), zoneId, oldFixed, newFixed,
                                               oldRelative, newRelative);
    m_undoController->push(cmd);
    markUnsaved();
}

void EditorController::applyZoneGeometryMode(const QString& zoneId, int mode, const QRectF& relativeGeo,
                                             const QRectF& fixedGeo)
{
    if (!m_zoneManager) {
        return;
    }

    int index = m_zoneManager->findZoneIndex(zoneId);
    if (index < 0) {
        return;
    }

    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        return;
    }

    // Update geometry mode
    zone[::PhosphorZones::ZoneJsonKeys::GeometryMode] = mode;

    // Update relative geometry
    zone[::PhosphorZones::ZoneJsonKeys::X] = relativeGeo.x();
    zone[::PhosphorZones::ZoneJsonKeys::Y] = relativeGeo.y();
    zone[::PhosphorZones::ZoneJsonKeys::Width] = relativeGeo.width();
    zone[::PhosphorZones::ZoneJsonKeys::Height] = relativeGeo.height();

    if (mode == static_cast<int>(PhosphorZones::ZoneGeometryMode::Fixed)) {
        // Switching to Fixed: compute and set fixed pixel coords
        if (fixedGeo.isValid()) {
            zone[::PhosphorZones::ZoneJsonKeys::FixedX] = fixedGeo.x();
            zone[::PhosphorZones::ZoneJsonKeys::FixedY] = fixedGeo.y();
            zone[::PhosphorZones::ZoneJsonKeys::FixedWidth] = fixedGeo.width();
            zone[::PhosphorZones::ZoneJsonKeys::FixedHeight] = fixedGeo.height();
        } else {
            // Compute from relative + screen size
            QSizeF ss = m_zoneManager->effectiveScreenSizeF();
            zone[::PhosphorZones::ZoneJsonKeys::FixedX] = relativeGeo.x() * ss.width();
            zone[::PhosphorZones::ZoneJsonKeys::FixedY] = relativeGeo.y() * ss.height();
            zone[::PhosphorZones::ZoneJsonKeys::FixedWidth] = relativeGeo.width() * ss.width();
            zone[::PhosphorZones::ZoneJsonKeys::FixedHeight] = relativeGeo.height() * ss.height();
        }
    } else {
        // Switching to Relative: remove stale fixed keys
        zone.remove(QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::FixedX));
        zone.remove(QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::FixedY));
        zone.remove(QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::FixedWidth));
        zone.remove(QString::fromLatin1(::PhosphorZones::ZoneJsonKeys::FixedHeight));
    }

    // setZoneData emits ZoneManager::zonesChanged, which is connected to
    // EditorController::zonesChanged — no need to emit again here.
    m_zoneManager->setZoneData(zoneId, zone);
}

void EditorController::refreshGlobalGapOverlaySettings()
{
    // Fetches every gap and overlay key the editor cares about (innerGap, the
    // outerGap cluster and overlayDisplayMode) in a single daemon round-trip
    // instead of one blocking getSetting() call per key, each of which built
    // a fresh QDBusInterface. SettingsAdaptor::getSettings reads straight from
    // the in-memory registry, so the daemon-side cost is unchanged and the
    // editor startup hot path saves the extra IPC round-trips.
    static const QStringList kGapOverlayKeys = {
        QStringLiteral("innerGap"),      QStringLiteral("outerGap"),           QStringLiteral("usePerSideOuterGap"),
        QStringLiteral("outerGapTop"),   QStringLiteral("outerGapBottom"),     QStringLiteral("outerGapLeft"),
        QStringLiteral("outerGapRight"), QStringLiteral("overlayDisplayMode"),
    };
    const QVariantMap values = SettingsDbusQueries::querySettingsBatch(kGapOverlayKeys);

    // The gap globals (innerGap + outerGap cluster) live in the gap sub-model.
    m_gaps->applyGlobalSettings(values);

    // overlayDisplayMode global stays on the controller. Uses toInt(&ok) so a
    // malformed daemon reply falls back to the default rather than coercing to
    // 0; a negative or missing value also falls through to the default.
    auto it = values.constFind(QStringLiteral("overlayDisplayMode"));
    int newOverlay = ConfigDefaults::overlayDisplayMode();
    if (it != values.constEnd()) {
        bool ok = false;
        const int v = it.value().toInt(&ok);
        if (ok && v >= 0) {
            newOverlay = v;
        }
    }
    if (m_cachedGlobalOverlayDisplayMode != newOverlay) {
        m_cachedGlobalOverlayDisplayMode = newOverlay;
        Q_EMIT globalOverlayDisplayModeChanged();
    }
}

} // namespace PlasmaZones
