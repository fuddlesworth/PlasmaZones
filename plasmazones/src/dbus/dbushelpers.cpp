// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbushelpers.h"
#include "core/utils/utils.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include "core/utils/geometryutils.h"
#include <PhosphorZones/Layout.h>
#include <QDBusContext>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QMetaClassInfo>
#include <QMetaObject>
#include <QScreen>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {
namespace DbusHelpers {

QString resolveScreenId(PhosphorScreens::ScreenManager* mgr, const QString& screenId)
{
    if (screenId.isEmpty()) {
        // Fall back to the primary screen's effective screen ID.
        // QCursor::pos() returns stale data for Wayland background daemons,
        // so we avoid it entirely and use the primary screen instead.
        QScreen* primary = QGuiApplication::primaryScreen();
        if (primary && mgr) {
            // If the primary screen has virtual subdivisions, returns the first
            // virtual screen ID. Otherwise returns the physical screen ID.
            const QStringList ids = mgr->virtualScreenIdsFor(PhosphorScreens::ScreenIdentity::identifierFor(primary));
            return ids.isEmpty() ? PhosphorScreens::ScreenIdentity::identifierFor(primary) : ids.first();
        }
        return primary ? PhosphorScreens::ScreenIdentity::identifierFor(primary) : QString();
    }
    return PhosphorIdentity::VirtualScreenId::isVirtual(screenId)
        ? screenId
        : PhosphorScreens::ScreenIdentity::idForName(screenId);
}

QRectF resolveScreenGeometry(PhosphorScreens::ScreenManager* mgr, PhosphorZones::Layout* layout,
                             const QString& screenId)
{
    if (mgr) {
        const QRect geom = mgr->screenGeometry(screenId);
        if (geom.isValid()) {
            return GeometryUtils::effectiveScreenGeometry(mgr, layout, screenId);
        }
    }
    QScreen* screen =
        PhosphorScreens::ScreenIdentity::findByIdOrName(PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
    if (!screen && mgr) {
        screen = mgr->physicalScreenFor(screenId).qscreen;
    }
    if (screen) {
        return GeometryUtils::effectiveScreenGeometry(mgr, layout, screen);
    }
    return {};
}

QScreen* resolvePhysicalQScreen(PhosphorScreens::ScreenManager* mgr, const QString& screenId)
{
    if (mgr) {
        QScreen* screen = mgr->physicalScreenFor(screenId).qscreen;
        if (screen) {
            return screen;
        }
    }
    return PhosphorScreens::ScreenIdentity::findByIdOrName(
        PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
}

const QDBusContext* busContext(const QObject* adaptor)
{
    QObject* const host = adaptor ? adaptor->parent() : nullptr;
    // The lookup Qt itself uses to find the context it sets.
    const auto* context = host ? static_cast<const QDBusContext*>(host->qt_metacast("QDBusContext")) : nullptr;
    return context && context->calledFromDBus() ? context : nullptr;
}

QString busCaller(const QObject* adaptor)
{
    const QDBusContext* const context = busContext(adaptor);
    return context ? context->message().service() : QString();
}

bool sendBusError(const QObject* adaptor, QDBusError::ErrorType type, const QString& message)
{
    const QDBusContext* const context = busContext(adaptor);
    if (!context || context->message().type() != QDBusMessage::MethodCallMessage) {
        return false;
    }
    const QString called = context->message().interface();
    if (!called.isEmpty()) {
        const QMetaObject* const meta = adaptor->metaObject();
        const int info = meta->indexOfClassInfo("D-Bus Interface");
        if (info < 0 || called != QLatin1String(meta->classInfo(info).value())) {
            return false;
        }
    }
    context->sendErrorReply(type, message);
    return true;
}

} // namespace DbusHelpers
} // namespace PlasmaZones
