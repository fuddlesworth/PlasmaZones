// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/RegistryNotifier.h>

#include <QHash>
#include <QList>
#include <QString>
#include <QtGlobal> // qWarning, qPrintable

#include <functional>
#include <memory>
#include <type_traits>

namespace PhosphorRegistry {

// Generic, instance-per-composition-root registry of factories keyed
// by their id(). One instance per UI seam (the shell typically owns
// five: bar widgets, control-center tiles, launcher providers, OSDs,
// desktop widgets).
//
// Lifetime / threading
//   - Owned by the composition root. Not a singleton; tests can build
//     their own Registry locally and tear down cleanly.
//   - Single-threaded. The Registry must be constructed and used on
//     one thread (typically the GUI thread). RegistryNotifier inherits
//     that thread affinity; consumers connecting to its signals get
//     Qt::AutoConnection semantics relative to whatever thread they
//     hand the Registry off to.
//
// Signals
//   - factoryRegistered / factoryUnregistered fire on the
//     RegistryNotifier this Registry owns (accessible via notifier()).
//     The id is the signal payload; consumers look up the concrete
//     factory via factory(id) when they need it.
//
// Header-only by design (no .cpp). The template body is small and
// must be instantiable from any consuming translation unit.
template<typename Factory>
class Registry
{
    static_assert(std::is_base_of_v<IFactoryBase, Factory>,
                  "Registry<T> requires T to derive from PhosphorRegistry::IFactoryBase");

public:
    Registry() = default;
    ~Registry() = default;

    Q_DISABLE_COPY_MOVE(Registry)

    // Add factory to the registry under factory->id(). Rejected with
    // a no-op + qWarning if a factory with the same id is already
    // registered (first-registration wins; the rejection prevents
    // accidental override and matches the documented contract). The
    // caller retains ownership semantics via std::shared_ptr: the
    // registry holds one ref, surfaces and other consumers can hold
    // additional refs through factory() / forEach().
    void registerFactory(std::shared_ptr<Factory> factory)
    {
        if (!factory) {
            qWarning("PhosphorRegistry::Registry::registerFactory: null factory ignored");
            return;
        }
        const QString id = factory->id();
        if (id.isEmpty()) {
            qWarning("PhosphorRegistry::Registry::registerFactory: factory with empty id ignored");
            return;
        }
        if (m_factories.contains(id)) {
            qWarning("PhosphorRegistry::Registry::registerFactory: duplicate id '%s' ignored", qPrintable(id));
            return;
        }
        m_factories.insert(id, std::move(factory));
        m_notifier.notifyRegistered(id);
    }

    // Remove the factory at id, if any. No-op (silent) if id is
    // unknown — the unregistered signal is not fired in that case
    // (it would lie about state never having existed). Existing
    // shared_ptr refs held by surfaces stay valid; the registry just
    // drops its own ref.
    void unregisterFactory(const QString& id)
    {
        if (m_factories.remove(id) > 0) {
            m_notifier.notifyUnregistered(id);
        }
    }

    // Lookup. Returns the registered factory or a null shared_ptr if
    // id is unknown. The factory remains alive as long as the
    // registry holds it (call unregisterFactory to drop).
    [[nodiscard]] std::shared_ptr<Factory> factory(const QString& id) const
    {
        return m_factories.value(id);
    }

    // Snapshot of currently-registered ids. Iteration order is
    // QHash's hash order — not registration order. Consumers that
    // need a stable display order must sort the result themselves.
    [[nodiscard]] QList<QString> ids() const
    {
        return m_factories.keys();
    }

    // Functional iteration. Visits each registered factory exactly
    // once. The visitor must not mutate the registry (register /
    // unregister inside the loop is undefined behaviour — same as
    // any QHash iteration contract).
    void forEach(const std::function<void(const std::shared_ptr<Factory>&)>& visitor) const
    {
        for (const auto& entry : m_factories) {
            visitor(entry);
        }
    }

    // Access the signal carrier. Survives for the registry's
    // lifetime; consumers connect signal-slot during composition
    // root setup and rely on Qt's connection auto-disconnect when
    // either end is destroyed.
    [[nodiscard]] RegistryNotifier* notifier()
    {
        return &m_notifier;
    }
    [[nodiscard]] const RegistryNotifier* notifier() const
    {
        return &m_notifier;
    }

    // True if no factories are currently registered.
    [[nodiscard]] bool isEmpty() const
    {
        return m_factories.isEmpty();
    }

    // Number of currently-registered factories.
    [[nodiscard]] qsizetype size() const
    {
        return m_factories.size();
    }

private:
    QHash<QString, std::shared_ptr<Factory>> m_factories;
    // Owned by value; default-constructed parentless. The Registry's
    // public notifier() returns a pointer so consumers can connect,
    // but ownership stays inside the Registry — same lifetime, same
    // thread.
    RegistryNotifier m_notifier;
};

} // namespace PhosphorRegistry
