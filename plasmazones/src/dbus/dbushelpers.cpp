// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "dbushelpers.h"
#include "compositorbridgeadaptor.h"
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
#include <QSet>
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
    // A virtual screen the manager does not resolve (an id left over from a
    // reconfigure) has no geometry of its own. Widened to its physical
    // monitor, zone math would run against the whole monitor for a screen
    // that no longer exists.
    if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
        return {};
    }
    QScreen* screen = PhosphorScreens::ScreenIdentity::findByIdOrName(screenId);
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

namespace {

/// Whether @p message is a method call addressed to @p adaptor's own D-Bus
/// interface, or one naming none, which Qt dispatches by member name.
bool isCallToOwnInterface(const QObject* adaptor, const QDBusMessage& message)
{
    if (message.type() != QDBusMessage::MethodCallMessage) {
        return false;
    }
    const QString called = message.interface();
    if (called.isEmpty()) {
        return true;
    }
    const QMetaObject* const meta = adaptor->metaObject();
    const int info = meta->indexOfClassInfo("D-Bus Interface");
    return info >= 0 && called == QLatin1String(meta->classInfo(info).value());
}

} // namespace

bool sendBusError(const QObject* adaptor, QDBusError::ErrorType type, const QString& message)
{
    const QDBusContext* const context = busContext(adaptor);
    if (!context || !isCallToOwnInterface(adaptor, context->message())) {
        return false;
    }
    context->sendErrorReply(type, message);
    return true;
}

std::optional<QString> busCallerOf(const QObject* adaptor, QLatin1StringView member)
{
    const QDBusContext* const context = busContext(adaptor);
    if (!context) {
        return std::nullopt;
    }
    const QDBusMessage& message = context->message();
    if (message.member() != member || !isCallToOwnInterface(adaptor, message)) {
        return std::nullopt;
    }
    return message.service();
}

bool isBridgeCaller(const QObject* adaptor, const QString& busName)
{
    // The host's direct children are its adaptors, so the lookup is a few
    // pointer comparisons, and reading the bridge where it lives keeps one
    // record of the registered name.
    const QObject* const host = adaptor ? adaptor->parent() : nullptr;
    const CompositorBridgeAdaptor* const bridge =
        host ? host->findChild<CompositorBridgeAdaptor*>(QString(), Qt::FindDirectChildrenOnly) : nullptr;
    return bridge && bridge->isBridgeService(busName);
}

bool admitCompositorReport(const QObject* adaptor, QLatin1StringView member)
{
    const std::optional<QString> caller = busCallerOf(adaptor, member);
    if (!caller || isBridgeCaller(adaptor, *caller)) {
        return true;
    }
    // Once per caller: a peer that keeps calling would otherwise fill the log.
    // Bounded, because a peer that reconnects comes back under a new unique
    // name each time and the names are never reused: past the bound a refusal
    // logs at debug and the set stops growing.
    constexpr qsizetype MaxWarnedCallers = 32;
    static QSet<QString> warned;
    if (warned.contains(*caller)) {
        qCDebug(lcDbus) << member << "refused for" << *caller;
    } else if (warned.size() < MaxWarnedCallers) {
        warned.insert(*caller);
        qCWarning(lcDbus) << member << "refused for" << *caller << "- reserved for the registered compositor bridge";
    } else {
        qCDebug(lcDbus) << member << "refused for" << *caller << "- reserved for the registered compositor bridge";
    }
    sendBusError(adaptor, QDBusError::AccessDenied,
                 QStringLiteral("%1 is reserved for the registered compositor bridge").arg(member));
    return false;
}

} // namespace DbusHelpers
} // namespace PlasmaZones
