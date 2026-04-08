// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../services/ZoneManager.h"
#include "../undo/UndoController.h"
#include "../undo/commands/UpdateGapOverrideCommand.h"
#include "../undo/commands/UpdateFullScreenGeometryCommand.h"
#include "../undo/commands/ToggleGeometryModeCommand.h"
#include "../undo/commands/UpdateFixedGeometryCommand.h"
#include "../helpers/SettingsDbusQueries.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>

#include "pz_i18n.h"
#include <QGuiApplication>
#include <QPointer>
#include <QScreen>

namespace PlasmaZones {

int EditorController::zonePadding() const
{
    return m_zonePadding;
}

int EditorController::outerGap() const
{
    return m_outerGap;
}

bool EditorController::hasZonePaddingOverride() const
{
    return m_zonePadding >= 0;
}

bool EditorController::hasOuterGapOverride() const
{
    return m_outerGap >= 0;
}

int EditorController::globalZonePadding() const
{
    return m_cachedGlobalZonePadding;
}

int EditorController::globalOuterGap() const
{
    return m_cachedGlobalOuterGap;
}

void EditorController::setZonePadding(int padding)
{
    if (padding < -1) {
        padding = -1;
    }
    if (m_zonePadding != padding) {
        auto* cmd =
            new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::ZonePadding, m_zonePadding, padding);
        m_undoController->push(cmd);
    }
}

void EditorController::setZonePaddingDirect(int padding)
{
    if (padding < -1) {
        padding = -1;
    }
    if (m_zonePadding != padding) {
        m_zonePadding = padding;
        markUnsaved();
        Q_EMIT zonePaddingChanged();
    }
}

void EditorController::setOuterGap(int gap)
{
    if (gap < -1) {
        gap = -1;
    }
    if (m_outerGap != gap) {
        auto* cmd = new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGap, m_outerGap, gap);
        m_undoController->push(cmd);
    }
}

void EditorController::setOuterGapDirect(int gap)
{
    if (gap < -1) {
        gap = -1;
    }
    if (m_outerGap != gap) {
        m_outerGap = gap;
        markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

bool EditorController::usePerSideOuterGap() const
{
    return m_usePerSideOuterGap;
}

int EditorController::outerGapTop() const
{
    return m_outerGapTop;
}

int EditorController::outerGapBottom() const
{
    return m_outerGapBottom;
}

int EditorController::outerGapLeft() const
{
    return m_outerGapLeft;
}

int EditorController::outerGapRight() const
{
    return m_outerGapRight;
}

bool EditorController::hasPerSideOuterGapOverride() const
{
    return m_usePerSideOuterGap
        && (m_outerGapTop >= 0 || m_outerGapBottom >= 0 || m_outerGapLeft >= 0 || m_outerGapRight >= 0);
}

bool EditorController::globalUsePerSideOuterGap() const
{
    return m_cachedGlobalUsePerSideOuterGap;
}

int EditorController::globalOuterGapTop() const
{
    return m_cachedGlobalOuterGapTop;
}

int EditorController::globalOuterGapBottom() const
{
    return m_cachedGlobalOuterGapBottom;
}

int EditorController::globalOuterGapLeft() const
{
    return m_cachedGlobalOuterGapLeft;
}

int EditorController::globalOuterGapRight() const
{
    return m_cachedGlobalOuterGapRight;
}

void EditorController::setUsePerSideOuterGap(bool enabled)
{
    if (m_usePerSideOuterGap != enabled) {
        // Use a gap override command for undo (toggling per-side is conceptually a gap change)
        auto* cmd = new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::UsePerSideOuterGap,
                                                 m_usePerSideOuterGap ? 1 : 0, enabled ? 1 : 0);
        m_undoController->push(cmd);
    }
}

void EditorController::setUsePerSideOuterGapDirect(bool enabled)
{
    if (m_usePerSideOuterGap != enabled) {
        m_usePerSideOuterGap = enabled;
        markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

void EditorController::setOuterGapTop(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapTop != gap) {
        auto* cmd =
            new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGapTop, m_outerGapTop, gap);
        m_undoController->push(cmd);
    }
}

void EditorController::setOuterGapTopDirect(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapTop != gap) {
        m_outerGapTop = gap;
        markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

void EditorController::setOuterGapBottom(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapBottom != gap) {
        auto* cmd = new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGapBottom,
                                                 m_outerGapBottom, gap);
        m_undoController->push(cmd);
    }
}

void EditorController::setOuterGapBottomDirect(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapBottom != gap) {
        m_outerGapBottom = gap;
        markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

void EditorController::setOuterGapLeft(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapLeft != gap) {
        auto* cmd =
            new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGapLeft, m_outerGapLeft, gap);
        m_undoController->push(cmd);
    }
}

void EditorController::setOuterGapLeftDirect(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapLeft != gap) {
        m_outerGapLeft = gap;
        markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

void EditorController::setOuterGapRight(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapRight != gap) {
        auto* cmd =
            new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGapRight, m_outerGapRight, gap);
        m_undoController->push(cmd);
    }
}

void EditorController::setOuterGapRightDirect(int gap)
{
    if (gap < -1)
        gap = -1;
    if (m_outerGapRight != gap) {
        m_outerGapRight = gap;
        markUnsaved();
        Q_EMIT outerGapChanged();
    }
}

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

void EditorController::clearZonePaddingOverride()
{
    setZonePadding(-1);
}

void EditorController::clearOverlayDisplayModeOverride()
{
    setOverlayDisplayMode(-1);
}

void EditorController::clearOuterGapOverride()
{
    // Early return if nothing to clear — avoids empty macro on undo stack
    bool hasAnyOverride = m_outerGap != -1 || m_usePerSideOuterGap || m_outerGapTop != -1 || m_outerGapBottom != -1
        || m_outerGapLeft != -1 || m_outerGapRight != -1;
    if (!hasAnyOverride) {
        return;
    }

    // Snapshot current state for undo, then push a macro command that resets all gap overrides.
    // Uses beginMacro/endMacro so the entire clear is one undo step.
    m_undoController->beginMacro(PzI18n::tr("Clear Edge Gap Override", "@action"));
    if (m_outerGap != -1) {
        m_undoController->push(
            new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGap, m_outerGap, -1));
    }
    if (m_usePerSideOuterGap) {
        m_undoController->push(
            new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::UsePerSideOuterGap, 1, 0));
    }
    if (m_outerGapTop != -1) {
        m_undoController->push(
            new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGapTop, m_outerGapTop, -1));
    }
    if (m_outerGapBottom != -1) {
        m_undoController->push(new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGapBottom,
                                                            m_outerGapBottom, -1));
    }
    if (m_outerGapLeft != -1) {
        m_undoController->push(
            new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGapLeft, m_outerGapLeft, -1));
    }
    if (m_outerGapRight != -1) {
        m_undoController->push(
            new UpdateGapOverrideCommand(this, UpdateGapOverrideCommand::GapType::OuterGapRight, m_outerGapRight, -1));
    }
    m_undoController->endMacro();
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
    if (m_useFullScreenGeometry != enabled) {
        m_useFullScreenGeometry = enabled;
        markUnsaved();
        Q_EMIT useFullScreenGeometryChanged();
    }
}

int EditorController::aspectRatioClass() const
{
    return m_aspectRatioClass;
}

void EditorController::setAspectRatioClass(int cls)
{
    if (cls < 0 || cls > 4) {
        return;
    }
    if (m_aspectRatioClass != cls) {
        m_aspectRatioClass = cls;
        markUnsaved();
        Q_EMIT aspectRatioClassChanged();
    }
}

// Shared helper: resolve m_targetScreen to a QScreen*, falling back to primary.
static QScreen* findTargetScreen(const QString& targetScreen)
{
    if (!targetScreen.isEmpty()) {
        for (QScreen* screen : QGuiApplication::screens()) {
            if (Utils::screenIdentifier(screen) == targetScreen || screen->name() == targetScreen) {
                return screen;
            }
        }
    }
    return QGuiApplication::primaryScreen();
}

QSize EditorController::targetScreenSize() const
{
    QScreen* screen = findTargetScreen(m_targetScreen);
    return screen ? screen->geometry().size() : QSize(1920, 1080);
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
    m_insetLeft = left;
    m_insetTop = top;
    m_insetRight = right;
    m_insetBottom = bottom;
    Q_EMIT usableAreaInsetsChanged();
}

void EditorController::refreshUsableAreaInsets()
{
    QScreen* screen = findTargetScreen(m_targetScreen);
    if (!screen) {
        setInsets(0, 0, 0, 0);
        return;
    }

    QRect fullGeom = screen->geometry();

    // Query the daemon for available geometry via D-Bus (async to avoid blocking the GUI thread).
    // The daemon's ScreenManager has layer-shell sensor windows that detect
    // actual panel positions — this data is not available in the editor process.
    QString screenId = Utils::screenIdentifier(screen);
    QDBusInterface screenIface(QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
                               QString::fromLatin1(DBus::Interface::Screen), QDBusConnection::sessionBus());

    if (screenIface.isValid()) {
        QDBusPendingCall pending = screenIface.asyncCall(QStringLiteral("getAvailableGeometry"), screenId);
        auto* watcher = new QDBusPendingCallWatcher(pending, this);
        QObject::connect(watcher, &QDBusPendingCallWatcher::finished, this,
                         [this, fullGeom](QDBusPendingCallWatcher* w) {
                             w->deleteLater();
                             QDBusPendingReply<QRect> reply = *w;
                             QRect availGeom = fullGeom;
                             if (reply.isValid() && !reply.value().isEmpty()) {
                                 availGeom = reply.value();
                             }
                             applyUsableAreaInsets(fullGeom, availGeom);
                         });
        return;
    }

    // No daemon — Qt's availableGeometry is used as fallback in applyUsableAreaInsets,
    // but on Wayland it often returns the full geometry (panels aren't reported).
    qCWarning(lcEditor) << "D-Bus daemon unreachable; usable area insets may be inaccurate";
    applyUsableAreaInsets(fullGeom, fullGeom);
}

void EditorController::applyUsableAreaInsets(const QRect& fullGeom, const QRect& daemonAvailGeom)
{
    QRect availGeom = daemonAvailGeom;

    // Fallback: try Qt's available geometry (works on some Wayland compositors)
    if (availGeom == fullGeom) {
        QScreen* screen = findTargetScreen(m_targetScreen);
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

    int oldMode = zone.value(JsonKeys::GeometryMode, 0).toInt();
    int newMode = (oldMode == 0) ? 1 : 0; // Toggle between Relative(0) and Fixed(1)

    QSize screenSize = targetScreenSize();
    qreal sw = screenSize.width();
    qreal sh = screenSize.height();

    QRectF oldRelGeo = m_zoneManager->extractZoneGeometry(zone);

    // Current fixed geometry (may not exist)
    QRectF oldFixedGeo;
    if (zone.contains(JsonKeys::FixedX)) {
        oldFixedGeo = m_zoneManager->extractFixedGeometry(zone);
    }

    QRectF newRelGeo = oldRelGeo;
    QRectF newFixedGeo = oldFixedGeo;

    if (newMode == 1) {
        // Switching to Fixed: convert relative -> pixel
        newFixedGeo = QRectF(oldRelGeo.x() * sw, oldRelGeo.y() * sh, oldRelGeo.width() * sw, oldRelGeo.height() * sh);
    } else {
        // Switching to Relative: convert pixel -> relative (keep relativeGeometry as-is since it's maintained)
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

    // Validate fixed geometry
    x = qMax(0.0, x);
    y = qMax(0.0, y);
    w = qMax(static_cast<qreal>(EditorConstants::MinFixedZoneSize), w);
    h = qMax(static_cast<qreal>(EditorConstants::MinFixedZoneSize), h);

    QVariantMap zone = m_zoneManager->getZoneById(zoneId);
    if (zone.isEmpty()) {
        return;
    }

    // Capture old state for undo
    QRectF oldFixed = m_zoneManager->extractFixedGeometry(zone);
    QRectF oldRelative = m_zoneManager->extractZoneGeometry(zone);
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
    zone[JsonKeys::GeometryMode] = mode;

    // Update relative geometry
    zone[JsonKeys::X] = relativeGeo.x();
    zone[JsonKeys::Y] = relativeGeo.y();
    zone[JsonKeys::Width] = relativeGeo.width();
    zone[JsonKeys::Height] = relativeGeo.height();

    if (mode == static_cast<int>(ZoneGeometryMode::Fixed)) {
        // Switching to Fixed: compute and set fixed pixel coords
        if (fixedGeo.isValid()) {
            zone[JsonKeys::FixedX] = fixedGeo.x();
            zone[JsonKeys::FixedY] = fixedGeo.y();
            zone[JsonKeys::FixedWidth] = fixedGeo.width();
            zone[JsonKeys::FixedHeight] = fixedGeo.height();
        } else {
            // Compute from relative + screen size
            QSizeF ss = m_zoneManager->effectiveScreenSizeF();
            zone[JsonKeys::FixedX] = relativeGeo.x() * ss.width();
            zone[JsonKeys::FixedY] = relativeGeo.y() * ss.height();
            zone[JsonKeys::FixedWidth] = relativeGeo.width() * ss.width();
            zone[JsonKeys::FixedHeight] = relativeGeo.height() * ss.height();
        }
    } else {
        // Switching to Relative: remove stale fixed keys
        zone.remove(QString::fromLatin1(JsonKeys::FixedX));
        zone.remove(QString::fromLatin1(JsonKeys::FixedY));
        zone.remove(QString::fromLatin1(JsonKeys::FixedWidth));
        zone.remove(QString::fromLatin1(JsonKeys::FixedHeight));
    }

    // setZoneData emits ZoneManager::zonesChanged, which is connected to
    // EditorController::zonesChanged — no need to emit again here.
    m_zoneManager->setZoneData(zoneId, zone);
}

void EditorController::refreshGlobalZonePadding()
{
    int newValue = SettingsDbusQueries::queryGlobalZonePadding();

    if (m_cachedGlobalZonePadding != newValue) {
        m_cachedGlobalZonePadding = newValue;
        Q_EMIT globalZonePaddingChanged();
    }
}

void EditorController::refreshGlobalOuterGap()
{
    int newValue = SettingsDbusQueries::queryGlobalOuterGap();
    bool newUsePerSide = SettingsDbusQueries::queryGlobalUsePerSideOuterGap();
    int newTop = SettingsDbusQueries::queryGlobalOuterGapTop();
    int newBottom = SettingsDbusQueries::queryGlobalOuterGapBottom();
    int newLeft = SettingsDbusQueries::queryGlobalOuterGapLeft();
    int newRight = SettingsDbusQueries::queryGlobalOuterGapRight();

    bool changed = (m_cachedGlobalOuterGap != newValue) || (m_cachedGlobalUsePerSideOuterGap != newUsePerSide)
        || (m_cachedGlobalOuterGapTop != newTop) || (m_cachedGlobalOuterGapBottom != newBottom)
        || (m_cachedGlobalOuterGapLeft != newLeft) || (m_cachedGlobalOuterGapRight != newRight);

    m_cachedGlobalOuterGap = newValue;
    m_cachedGlobalUsePerSideOuterGap = newUsePerSide;
    m_cachedGlobalOuterGapTop = newTop;
    m_cachedGlobalOuterGapBottom = newBottom;
    m_cachedGlobalOuterGapLeft = newLeft;
    m_cachedGlobalOuterGapRight = newRight;

    if (changed) {
        Q_EMIT globalOuterGapChanged();
    }
}

void EditorController::refreshGlobalOverlayDisplayMode()
{
    int newValue = SettingsDbusQueries::queryGlobalOverlayDisplayMode();

    if (m_cachedGlobalOverlayDisplayMode != newValue) {
        m_cachedGlobalOverlayDisplayMode = newValue;
        Q_EMIT globalOverlayDisplayModeChanged();
    }
}

} // namespace PlasmaZones
