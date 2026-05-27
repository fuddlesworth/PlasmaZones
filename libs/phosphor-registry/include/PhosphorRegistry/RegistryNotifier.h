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
// Consumers get factoryRegistered / factoryUnregistered signals via
// registry.notifier(), with the factory id as the payload (the
// concrete factory instance is then looked up via factory(id) if the
// consumer needs it; passing the typed pointer through a QObject
// signal would require Q_DECLARE_METATYPE for every factory subclass).
class PHOSPHORREGISTRY_EXPORT RegistryNotifier : public QObject
{
    Q_OBJECT
public:
    explicit RegistryNotifier(QObject* parent = nullptr);
    ~RegistryNotifier() override;
    Q_DISABLE_COPY_MOVE(RegistryNotifier)

Q_SIGNALS:
    // Fired after a factory has been added to the registry.
    void factoryRegistered(const QString& id);

    // Fired after a factory has been removed from the registry. The
    // factory is already gone by the time this signal arrives; do
    // not attempt to look it up via factory(id).
    void factoryUnregistered(const QString& id);

    // Friend declaration scope. Registry<T> needs to invoke Q_EMIT on
    // the signals above. Templates and friend declarations don't
    // compose cleanly across translation units, so the signals are
    // public and called as plain member functions (Q_EMIT is just a
    // marker macro, no access enforcement).
};

} // namespace PhosphorRegistry
