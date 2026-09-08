// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Touchpad gestures for the Phosphor shell (A3, per-surface "Gesture"
// rows). Only the compositor sees touchpad gestures: a layer-shell client
// gets pointer events, never a three-finger swipe. So the effect registers
// the shell's gestures with KWin's recognizer and reports each completed
// one to the daemon over CompositorBridge.reportGesture; the daemon
// re-emits it as gestureReported and the shell maps it to a surface.
//
// The set is fixed and small: the identity names four. Three-finger swipe
// up and down for the launcher, four-finger swipe up and four-finger
// pinch out for the dashboard. Progress is not forwarded; a gesture is an
// event, the surfaces run their own enter and release.

#include "plasmazoneseffect.h"

#include "compositor/effectlogging.h"

#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>
#include <effect/globals.h>

#include <QAction>

namespace PlasmaZones {

namespace {

QString swipeDirectionName(KWin::SwipeDirection direction)
{
    switch (direction) {
    case KWin::SwipeDirection::Up:
        return QStringLiteral("up");
    case KWin::SwipeDirection::Down:
        return QStringLiteral("down");
    case KWin::SwipeDirection::Left:
        return QStringLiteral("left");
    case KWin::SwipeDirection::Right:
        return QStringLiteral("right");
    case KWin::SwipeDirection::Invalid:
        break;
    }
    return QString();
}

QString pinchDirectionName(KWin::PinchDirection direction)
{
    switch (direction) {
    case KWin::PinchDirection::Expanding:
        return QStringLiteral("expanding");
    case KWin::PinchDirection::Contracting:
        return QStringLiteral("contracting");
    }
    return QString();
}

} // namespace

void PlasmaZonesEffect::reportShellGesture(const QString& kind, const QString& direction, uint fingerCount)
{
    qCDebug(lcEffect) << "shell gesture:" << kind << direction << fingerCount;
    PhosphorProtocol::ClientHelpers::sendOneWay(PhosphorProtocol::Service::Interface::CompositorBridge,
                                                QStringLiteral("reportGesture"),
                                                {kind, direction, QVariant::fromValue(fingerCount)});
}

void PlasmaZonesEffect::initTouchpadGestures()
{
    if (!KWin::effects) {
        return;
    }

    const auto swipe = [this](KWin::SwipeDirection direction, uint fingers) {
        const QString name = swipeDirectionName(direction);
        auto* action = new QAction(this);
        action->setObjectName(QStringLiteral("phosphor-shell-swipe-%1-%2").arg(name).arg(fingers));
        connect(action, &QAction::triggered, this, [this, name, fingers]() {
            reportShellGesture(QStringLiteral("swipe"), name, fingers);
        });
        KWin::effects->registerTouchpadSwipeShortcut(direction, fingers, action);
    };
    const auto pinch = [this](KWin::PinchDirection direction, uint fingers) {
        const QString name = pinchDirectionName(direction);
        auto* action = new QAction(this);
        action->setObjectName(QStringLiteral("phosphor-shell-pinch-%1-%2").arg(name).arg(fingers));
        connect(action, &QAction::triggered, this, [this, name, fingers]() {
            reportShellGesture(QStringLiteral("pinch"), name, fingers);
        });
        KWin::effects->registerTouchpadPinchShortcut(direction, fingers, action);
    };

    // Launcher (A3 §2): three-finger swipe up opens, down closes.
    swipe(KWin::SwipeDirection::Up, 3);
    swipe(KWin::SwipeDirection::Down, 3);
    // Dashboard (A3 §7): four-finger swipe up opens, pinch out closes.
    swipe(KWin::SwipeDirection::Up, 4);
    pinch(KWin::PinchDirection::Expanding, 4);
}

} // namespace PlasmaZones
