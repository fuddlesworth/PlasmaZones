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
#include "../../config/configdefaults.h"
#include "../../core/constants.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include <PhosphorIdentity/VirtualScreenId.h>
#include <QDBusConnection>
#include <QDBusMessage>
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
    // Virtual screens: use cached geometry from daemon (set in setTargetScreen)
    if (m_virtualScreenSize.isValid()) {
        return m_virtualScreenSize;
    }
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
    // For virtual screens, use the daemon's geometry (QScreen doesn't know about VS).
    // The daemon's getScreenGeometry and getAvailableGeometry handle VS IDs.
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
        screenId = Utils::screenIdentifier(screen);
    }

    // Query the daemon for both full and available geometry via D-Bus.
    // The daemon's ScreenManager handles VS IDs natively.
    QDBusMessage geoMsg = QDBusMessage::createMethodCall(
        QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
        QString::fromLatin1(DBus::Interface::Screen), QStringLiteral("getScreenGeometry"));
    geoMsg << screenId;
    QDBusMessage availMsg = QDBusMessage::createMethodCall(
        QString::fromLatin1(DBus::ServiceName), QString::fromLatin1(DBus::ObjectPath),
        QString::fromLatin1(DBus::Interface::Screen), QStringLiteral("getAvailableGeometry"));
    availMsg << screenId;

    auto* geoWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(geoMsg), this);
    auto* availWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(availMsg), this);

    // Use shared state to wait for both replies.
    // QPointer guards against EditorController being destroyed before callbacks fire
    // (e.g., rapid screen switches that destroy and recreate the editor window).
    auto fullGeomResult = std::make_shared<QRect>(fullGeom);
    auto availGeomResult = std::make_shared<QRect>();
    auto pending = std::make_shared<int>(2);
    QPointer<EditorController> self(this);

    auto applyWhenReady = [self, fullGeomResult, availGeomResult, pending]() {
        if (--(*pending) > 0) {
            return;
        }
        if (!self) {
            return;
        }
        QRect fg = *fullGeomResult;
        QRect ag = availGeomResult->isValid() ? *availGeomResult : fg;
        if (fg.isValid()) {
            self->applyUsableAreaInsets(fg, ag);
        } else {
            self->setInsets(0, 0, 0, 0);
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
    // Fetches every gap and overlay key the editor cares about in a single
    // daemon round-trip. Prior to this the editor ctor path made 8 sequential
    // blocking getSetting() calls (1 zonePadding + 6 outerGap-related +
    // 1 overlayDisplayMode) — each constructing a fresh QDBusInterface.
    // SettingsAdaptor::getSettings reads straight from the in-memory
    // registry so the daemon-side cost is unchanged, and we avoid N-1
    // extra IPC round-trips on the editor startup hot path.
    static const QStringList kGapOverlayKeys = {
        QStringLiteral("zonePadding"),   QStringLiteral("outerGap"),           QStringLiteral("usePerSideOuterGap"),
        QStringLiteral("outerGapTop"),   QStringLiteral("outerGapBottom"),     QStringLiteral("outerGapLeft"),
        QStringLiteral("outerGapRight"), QStringLiteral("overlayDisplayMode"),
    };
    const QVariantMap values = SettingsDbusQueries::querySettingsBatch(kGapOverlayKeys);

    // Helper: read an int from the batch result. Uses toInt(&ok) so a
    // malformed daemon reply (wrong type, not convertible) falls back to
    // the default rather than silently coercing to 0. Negative values are
    // also treated as invalid — these keys are all non-negative pixel
    // counts / enum indices, matching the old single-key helpers'
    // semantics. Keys missing from the batch (unknown to the daemon, or
    // the whole call failed) also fall through to the fallback.
    auto readInt = [&](const QString& key, int fallback) {
        auto it = values.constFind(key);
        if (it == values.constEnd()) {
            return fallback;
        }
        bool ok = false;
        const int v = it.value().toInt(&ok);
        if (!ok || v < 0) {
            return fallback;
        }
        return v;
    };
    auto readBool = [&](const QString& key, bool fallback) {
        auto it = values.constFind(key);
        return it == values.constEnd() ? fallback : it.value().toBool();
    };

    // zonePadding
    {
        const int newValue = readInt(QStringLiteral("zonePadding"), Defaults::ZonePadding);
        if (m_cachedGlobalZonePadding != newValue) {
            m_cachedGlobalZonePadding = newValue;
            Q_EMIT globalZonePaddingChanged();
        }
    }

    // outerGap cluster — matches the old refreshGlobalOuterGap()
    // change-detection semantics (one aggregate signal covers any field
    // changing).
    {
        const int newValue = readInt(QStringLiteral("outerGap"), Defaults::OuterGap);
        const bool newUsePerSide = readBool(QStringLiteral("usePerSideOuterGap"), false);
        const int newTop = readInt(QStringLiteral("outerGapTop"), Defaults::OuterGap);
        const int newBottom = readInt(QStringLiteral("outerGapBottom"), Defaults::OuterGap);
        const int newLeft = readInt(QStringLiteral("outerGapLeft"), Defaults::OuterGap);
        const int newRight = readInt(QStringLiteral("outerGapRight"), Defaults::OuterGap);

        const bool changed = (m_cachedGlobalOuterGap != newValue) || (m_cachedGlobalUsePerSideOuterGap != newUsePerSide)
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

    // overlayDisplayMode
    {
        const int newValue = readInt(QStringLiteral("overlayDisplayMode"), ConfigDefaults::overlayDisplayMode());
        if (m_cachedGlobalOverlayDisplayMode != newValue) {
            m_cachedGlobalOverlayDisplayMode = newValue;
            Q_EMIT globalOverlayDisplayModeChanged();
        }
    }
}

} // namespace PlasmaZones
