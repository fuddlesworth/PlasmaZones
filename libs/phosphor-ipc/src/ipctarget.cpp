// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcTarget.h>

#include <PhosphorIpc/IpcEngine.h>
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
        m_router->unregisterTarget(m_target);
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
    // Changing target name after registration is a misuse — the
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
    if (!m_router) {
        qWarning("PhosphorIpc::IpcTarget::emitEvent: no router (target '%s' wasn't registered)", qPrintable(m_target));
        return;
    }
    if (signalName.isEmpty()) {
        qWarning("PhosphorIpc::IpcTarget::emitEvent: empty signalName ignored");
        return;
    }
    // Serialise QVariantList → QJsonArray. We use QJsonArray's
    // fromVariantList helper which mirrors the variantToJson rules
    // in IpcRouter for the call-return path (modulo custom QMetaType
    // fallbacks which would degrade to null here — for the call
    // path they degrade to toString fallback. Symmetry is good
    // enough for Phase 1.4; revisit if anyone hits a custom type
    // on a subscription path).
    m_router->broadcastEvent(m_target, signalName, QJsonArray::fromVariantList(args));
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
    router->registerTarget(m_target, this);
    m_registered = true;
}

} // namespace PhosphorIpc
