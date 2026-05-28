// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorIpc/phosphoripc_export.h>

#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QString>
#include <QVariantList>
#include <QtCore/qtclasshelpermacros.h>
#include <QtQml/qqmlregistration.h>

namespace PhosphorIpc {

// Pointer-only member; full definition only needed in the .cpp.
class IpcRouter;

// QML-side declarative wrapper for IpcRouter target registration.
// Instantiated by user QML; auto-registers with the application's
// IpcRouter in componentComplete(). Functions and signals declared
// on the IpcTarget item itself form the exposed surface, the
// schema generator walks the IpcTarget's own metaobject.
//
// Router discovery: looks for the QQmlEngine property
// "phosphorIpcRouter" stashed by IpcEngine::install(). If the
// application forgot to install the router, the IpcTarget logs a
// qWarning at componentComplete and stays inert (no registration);
// the rest of the QML tree continues to work.
//
// Lifetime: the router's QHash<QString, QPointer<QObject>> entry
// auto-clears when this QObject is destroyed, but IpcTarget also
// explicitly unregisters in its destructor so the registry's
// targetUnregistered signal fires. The destructor passes `this` to
// `unregisterTarget(name, obj)` so the router can reject the call
// if the registration was rejected (duplicate name) and a different
// target now owns the registry slot. Without that ownership check
// a duplicate-rejected IpcTarget would tear down the legitimate
// owner's binding on destruction.
class PHOSPHORIPC_EXPORT IpcTarget : public QObject, public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT
    Q_PROPERTY(QString target READ target WRITE setTarget NOTIFY targetChanged)
public:
    explicit IpcTarget(QObject* parent = nullptr);
    ~IpcTarget() override;
    Q_DISABLE_COPY_MOVE(IpcTarget)

    [[nodiscard]] QString target() const;
    void setTarget(const QString& name);

    // Push an event to every subscriber for (target, signalName).
    // QML plugin authors call this whenever a wire-visible state
    // transition happens, explicit per-transition, in contrast
    // to an auto-introspected Qt-signal hook. The args list is
    // serialised to JSON via the router's variant-to-JSON helper
    // (the same one used for sync call return values), so any
    // QVariantList-shaped payload works.
    Q_INVOKABLE void emitEvent(const QString& signalName, const QVariantList& args = {});

    // QQmlParserStatus, register with the router after QML has
    // finished setting up the item (so the `target` property is
    // populated).
    void classBegin() override;
    void componentComplete() override;

Q_SIGNALS:
    void targetChanged();

private:
    QString m_target;
    QPointer<IpcRouter> m_router;
    bool m_registered = false;
};

} // namespace PhosphorIpc
