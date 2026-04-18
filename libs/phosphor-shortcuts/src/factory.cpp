// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorShortcuts/Factory.h"

#include "PhosphorShortcuts/IBackend.h"
#include "backends.h"
#include "shortcutslogging.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>

namespace Phosphor::Shortcuts {

namespace {

const QString kKGlobalAccelService = QStringLiteral("org.kde.kglobalaccel");
const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");

std::unique_ptr<IBackend> makeDBusTrigger(QObject* parent)
{
    qCInfo(lcPhosphorShortcuts) << "Selected backend: DBusTrigger";
    return std::make_unique<DBusTriggerBackend>(parent);
}

std::unique_ptr<IBackend> autoSelect(QObject* parent)
{
    auto* bus = QDBusConnection::sessionBus().interface();
    if (!bus) {
        qCInfo(lcPhosphorShortcuts) << "No D-Bus session bus — falling back to DBusTrigger";
        return makeDBusTrigger(parent);
    }

#ifdef PHOSPHORSHORTCUTS_HAVE_KGLOBALACCEL
    if (bus->isServiceRegistered(kKGlobalAccelService)) {
        qCInfo(lcPhosphorShortcuts) << "Selected backend: KGlobalAccel";
        return std::make_unique<KGlobalAccelBackend>(parent);
    }
#endif

    // Trust isServiceRegistered — constructing a QDBusInterface here would
    // issue a synchronous Introspect call, stalling daemon startup by a
    // full D-Bus round-trip when the portal is slow. If the service is
    // registered but the GlobalShortcuts interface is missing, PortalBackend
    // will detect that via its CreateSession reply and latch m_sessionFailed,
    // keeping ready() firing so consumers don't hang.
    if (bus->isServiceRegistered(kPortalService)) {
        qCInfo(lcPhosphorShortcuts) << "Selected backend: Portal";
        return std::make_unique<PortalBackend>(parent);
    }

    return makeDBusTrigger(parent);
}

} // namespace

std::unique_ptr<IBackend> createBackend(BackendHint hint, QObject* parent)
{
    switch (hint) {
    case BackendHint::Auto:
        return autoSelect(parent);
    case BackendHint::KGlobalAccel:
#ifdef PHOSPHORSHORTCUTS_HAVE_KGLOBALACCEL
        return std::make_unique<KGlobalAccelBackend>(parent);
#else
        qCWarning(lcPhosphorShortcuts)
            << "KGlobalAccel backend requested but library built without KF6::GlobalAccel — using DBusTrigger";
        return makeDBusTrigger(parent);
#endif
    case BackendHint::Portal:
        return std::make_unique<PortalBackend>(parent);
    case BackendHint::DBusTrigger:
        return makeDBusTrigger(parent);
    case BackendHint::Native:
        qCWarning(lcPhosphorShortcuts)
            << "Native backend is reserved for a future INativeGrabber implementation and is not available yet —"
            << "createBackend(Native) returns nullptr. Callers that want automatic fallback should pass Auto.";
        return nullptr;
    }
    Q_UNREACHABLE();
}

} // namespace Phosphor::Shortcuts
