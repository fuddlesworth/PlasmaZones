// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorIpc/IpcEngine.h>

#include <PhosphorIpc/IpcRouter.h>

#include <QQmlEngine>
#include <QVariant>

namespace PhosphorIpc::IpcEngine {

namespace {
constexpr auto RouterPropertyName = "phosphorIpcRouter";
} // namespace

void install(QQmlEngine* engine, IpcRouter* router)
{
    if (!engine) {
        qWarning("PhosphorIpc::IpcEngine::install: null engine ignored");
        return;
    }
    if (!router) {
        qWarning("PhosphorIpc::IpcEngine::install: null router; call uninstall() to drop the binding explicitly");
        return;
    }
    const QVariant current = engine->property(RouterPropertyName);
    if (current.isValid()) {
        IpcRouter* existing = qobject_cast<IpcRouter*>(current.value<QObject*>());
        if (existing == router) {
            return; // idempotent, same router on same engine is fine
        }
        qWarning("PhosphorIpc::IpcEngine::install: replacing existing router on engine");
    }
    engine->setProperty(RouterPropertyName, QVariant::fromValue<QObject*>(router));
}

void uninstall(QQmlEngine* engine)
{
    if (!engine) {
        return;
    }
    engine->setProperty(RouterPropertyName, QVariant());
}

IpcRouter* routerFor(QQmlEngine* engine)
{
    if (!engine) {
        return nullptr;
    }
    return qobject_cast<IpcRouter*>(engine->property(RouterPropertyName).value<QObject*>());
}

} // namespace PhosphorIpc::IpcEngine
