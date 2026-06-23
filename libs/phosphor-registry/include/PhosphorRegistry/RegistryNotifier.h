// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/phosphorregistry_export.h>

#include <QObject>
#include <QString>
#include <QtCore/qtclasshelpermacros.h> // Q_DISABLE_COPY_MOVE

namespace PhosphorRegistry {

// QObject-derived signal carrier for Registry<T>. The Registry itself
// is a template, and Qt's moc cannot template-process a Q_OBJECT
// class. The standard workaround is to put Q_OBJECT machinery on a
// non-template helper that the template owns and forwards through.
//
// Registry<T> calls notifyRegistered / notifyUnregistered (the public
// emit forwarders below) rather than emitting the signals directly.
// Qt's `Q_SIGNALS:` access level (currently `public:` in Qt 6) is an
// implementation detail Qt does not commit to keep public, so a
// template invoking Q_EMIT m_notifier.factoryRegistered(...) would be
// fragile under future Qt strictness. The forwarder pattern keeps
// the access-level contract under our control.
//
// Consumers connect to factoryRegistered / factoryUnregistered via
// registry.notifier(), with the factory id as the payload. The
// concrete factory instance is looked up via factory(id) if needed
// (passing the typed pointer through a QObject signal would require
// Q_DECLARE_METATYPE for every factory subclass).
class PHOSPHORREGISTRY_EXPORT RegistryNotifier : public QObject
{
    Q_OBJECT
public:
    explicit RegistryNotifier(QObject* parent = nullptr);
    ~RegistryNotifier() override;
    Q_DISABLE_COPY_MOVE(RegistryNotifier)

    // Public emit forwarders. Registry<T> calls these from its template
    // body so the Q_EMIT macro lives next to the signal declaration
    // (whose access level Qt controls), not inside a template
    // (whose Q_EMIT call would be fragile against future Qt access-
    // level tightening of `Q_SIGNALS:`).
    void notifyRegistered(const QString& id)
    {
        Q_EMIT factoryRegistered(id);
    }
    void notifyUnregistered(const QString& id)
    {
        Q_EMIT factoryUnregistered(id);
    }

Q_SIGNALS:
    // Fired after a factory has been added to the registry.
    void factoryRegistered(const QString& id);

    // Fired after a factory has been removed from the registry. On a plain
    // unregister the factory is gone — factory(id) returns null. On a Replace
    // (DuplicatePolicy::Replace overwrites an existing id) this fires for the
    // OLD factory immediately before factoryRegistered(id) for the new one,
    // and the replacement is ALREADY in place — so factory(id) returns the new
    // factory here, not null. Consumers that key teardown on factory(id)==null
    // must account for the Replace case.
    void factoryUnregistered(const QString& id);
};

} // namespace PhosphorRegistry
