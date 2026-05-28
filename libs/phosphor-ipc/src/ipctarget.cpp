// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcTarget.h>

#include <PhosphorIpc/IpcEngine.h>
#include <PhosphorIpc/IpcProtocol.h>
#include <PhosphorIpc/IpcRouter.h>

#include <QJsonArray>
#include <QJsonValue>
#include <QQmlEngine>

namespace PhosphorIpc {

IpcTarget::IpcTarget(QObject* parent)
    : QObject(parent)
{
}

IpcTarget::~IpcTarget()
{
    if (m_registered && m_router) {
        // Pass `this` so the router rejects the call when a
        // different QObject now owns the `m_target` slot. This
        // matters when an IpcTarget's registerTarget was REJECTED
        // (duplicate name): m_registered was never set true on
        // that path, so we'd skip this branch entirely; but if
        // some future path ever sets m_registered without verifying
        // ownership, the ownership check below keeps the
        // legitimate owner's binding intact.
        m_router->unregisterTarget(m_target, this);
    }
}

QString IpcTarget::target() const
{
    return m_target;
}

void IpcTarget::setTarget(const QString& name)
{
    if (m_target == name) {
        return;
    }
    // Changing target name after registration is a misuse, the
    // registry key was already published. Refuse silently with a
    // diagnostic so plugin authors notice.
    if (m_registered) {
        qWarning("PhosphorIpc::IpcTarget: target name changed after registration (was '%s', set '%s' ignored)",
                 qPrintable(m_target), qPrintable(name));
        return;
    }
    m_target = name;
    Q_EMIT targetChanged();
}

void IpcTarget::classBegin()
{
    // No-op: registration happens in componentComplete, after QML
    // has finished setting the target property.
}

void IpcTarget::emitEvent(const QString& signalName, const QVariantList& args)
{
    if (!m_router || !m_registered) {
        qWarning(
            "PhosphorIpc::IpcTarget::emitEvent: target '%s' is not registered (router missing or registration "
            "rejected)",
            qPrintable(m_target));
        return;
    }
    if (signalName.isEmpty()) {
        qWarning("PhosphorIpc::IpcTarget::emitEvent: empty signalName ignored");
        return;
    }
    // Walk args through the shared variantToJson so subscription
    // payloads match the JSON shape sync calls emit for return
    // values. (QJsonArray::fromVariantList would drop unknown
    // metatypes to null where the call path drops them to
    // toString, keeping a single converter is the only way to
    // avoid drift.)
    QJsonArray jsonArgs;
    for (const QVariant& v : args) {
        jsonArgs.append(variantToJson(v));
    }
    m_router->broadcastEvent(m_target, signalName, jsonArgs);
}

void IpcTarget::componentComplete()
{
    if (m_target.isEmpty()) {
        qWarning("PhosphorIpc::IpcTarget: missing 'target' property, not registered");
        return;
    }
    QQmlEngine* engine = qmlEngine(this);
    if (!engine) {
        qWarning("PhosphorIpc::IpcTarget '%s': no QQmlEngine (instantiated outside QML?)", qPrintable(m_target));
        return;
    }
    IpcRouter* router = IpcEngine::routerFor(engine);
    if (!router) {
        qWarning("PhosphorIpc::IpcTarget '%s': no router installed on the engine via IpcEngine::install()",
                 qPrintable(m_target));
        return;
    }
    m_router = router;
    // registerTarget returns false when the registry already binds
    // `m_target` to a different object (first-registration-wins).
    // Only flip m_registered on success so the destructor's
    // unregisterTarget call doesn't tear down the legitimate
    // owner's binding when this IpcTarget is destroyed. emitEvent
    // also checks m_registered before broadcasting so duplicate-
    // rejected targets can't fire events under the wire-name that
    // a different object owns.
    if (!router->registerTarget(m_target, this)) {
        m_router.clear();
        return;
    }
    m_registered = true;
}

} // namespace PhosphorIpc
