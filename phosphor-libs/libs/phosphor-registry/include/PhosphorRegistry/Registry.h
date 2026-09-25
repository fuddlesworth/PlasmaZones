// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include <PhosphorRegistry/IFactoryBase.h>
#include <PhosphorRegistry/RegistryNotifier.h>

#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>
#include <QtGlobal> // qWarning, qPrintable

#include <memory>
#include <type_traits>
#include <utility> // std::move

namespace PhosphorRegistry {

// Generic, instance-per-composition-root registry of factories keyed by
// their id() — the single id-keyed storage + change-notify primitive every
// registry in the tree composes, so storage, lookup, ownership, threading,
// and change signals are uniform instead of hand-rolled per registry.
//
// Used by the shell's five UI seams (bar widgets, control-center tiles,
// launcher providers, OSDs, desktop widgets) AND by the domain registries
// (shader packs, animation effects, tiling algorithms, layout sources,
// easing curves). Entries are populated explicitly (UI built-ins, the curve
// built-ins), by PluginLoader (.so packs), or by MetadataPackLoader<T>
// (content packs scanned from disk).
//
// ## Thread safety
//
// Thread-safe: an internal QMutex guards the store, so registration,
// unregistration, and lookup may run on any thread (e.g. a worker thread
// loading a curve concurrently with a GUI-thread lookup) without corrupting
// the store, and every read (factory/ids/forEach/size) returns a consistent
// snapshot taken under the lock. The mutex is uncontended on the common
// single-threaded GUI path, so the cost there is negligible. Change signals
// are emitted via the RegistryNotifier AFTER the lock is released, so a slot
// may safely call back into the registry; the notifier QObject keeps the
// thread affinity of whoever created the Registry, and cross-thread emissions
// reach consumers via queued connections.
//
// One caveat: the registration-ORDER guarantee on the change signals (below)
// holds only for single-threaded mutation (the common GUI-thread case). Two
// threads mutating concurrently each emit AFTER releasing the lock, so the
// relative order of their signals is unspecified even though the store's
// m_order — and therefore ids()/forEach() — stays correct. No signal is lost;
// only cross-thread emission ordering is undefined.
//
// ## Duplicate id policy
//
// registerFactory rejects a duplicate id by default (first-registration
// wins — the contract PluginLoader relies on for plugin id collisions).
// Pass DuplicatePolicy::Replace to overwrite an existing entry instead
// (the curve / algorithm registries replace a prior registration); a replace
// fires factoryUnregistered(old) then factoryRegistered(new).
//
// ## Owner tags
//
// A factory may be registered under an owner tag so a subsystem that
// contributes several factories can drop all of them in one call
// (unregisterByOwner) — e.g. a plugin or a user-JSON loader retiring its
// whole contribution. Untagged registrations (the empty tag) are the default
// and are never matched by unregisterByOwner.
//
// A Replace adopts the NEW call's owner tag — it does not preserve the prior
// entry's tag. A re-registration that wants to keep an entry in its owner
// group must pass the same ownerTag again (a tag-less Replace moves the entry
// to the untagged group). MetadataPackLoader and the algorithm registry
// re-register untagged, so this is a no-op for them; CurveLoader, by contrast,
// registers TAGGED (a per-loader owner tag) and deliberately re-passes that tag
// on every Replace so its entries stay bulk-removable via unregisterByOwner in
// ~CurveLoader.
//
// Header-only by design (no .cpp). The template body must be instantiable
// from any consuming translation unit.

// Behaviour when registerFactory hits an id that is already registered.
enum class DuplicatePolicy {
    Reject, ///< Keep the existing entry, log + return false (default).
    Replace, ///< Overwrite; fire factoryUnregistered(old) + factoryRegistered(new).
};

template<typename Factory>
class Registry
{
    static_assert(std::is_base_of_v<IFactoryBase, Factory>,
                  "Registry<T> requires T to derive from PhosphorRegistry::IFactoryBase");

public:
    Registry() = default;
    ~Registry() = default;

    Q_DISABLE_COPY_MOVE(Registry)

    // Add @p factory under factory->id(). Returns true on success. A null
    // factory or empty id is rejected (false + qWarning). A duplicate id is
    // rejected by default (false + qWarning); pass DuplicatePolicy::Replace
    // to overwrite it instead. @p ownerTag groups the entry for
    // unregisterByOwner (empty = untagged, the default). The registry holds
    // one shared_ptr ref; consumers can hold more via factory() / forEach().
    bool registerFactory(std::shared_ptr<Factory> factory, const QString& ownerTag = QString(),
                         DuplicatePolicy policy = DuplicatePolicy::Reject)
    {
        if (!factory) {
            qWarning("PhosphorRegistry::Registry::registerFactory: null factory ignored");
            return false;
        }
        const QString id = factory->id();
        if (id.isEmpty()) {
            qWarning("PhosphorRegistry::Registry::registerFactory: factory %p with empty id ignored",
                     static_cast<const void*>(factory.get()));
            return false;
        }
        bool replaced = false;
        {
            QMutexLocker locker(&m_mutex);
            if (m_entries.contains(id)) {
                if (policy == DuplicatePolicy::Reject) {
                    qWarning("PhosphorRegistry::Registry::registerFactory: duplicate id '%s' ignored", qPrintable(id));
                    return false;
                }
                // Replace keeps the existing order position — a hot-reload that
                // re-registers an id must not shuffle it to the end of the
                // catalogue (the layout-source composite + algorithm UI lists
                // rely on stable order across replacements).
                replaced = true;
            } else {
                m_order.append(id);
            }
            m_entries.insert(id, Entry{std::move(factory), ownerTag});
        }
        // Signals fire outside the lock so a slot may re-enter the registry.
        if (replaced) {
            m_notifier.notifyUnregistered(id);
        }
        m_notifier.notifyRegistered(id);
        return true;
    }

    // Remove the factory at @p id. Returns true if one was removed. No-op
    // (returns false, no signal) if id is unknown. Existing shared_ptr refs
    // held by consumers stay valid; the registry just drops its own ref.
    bool unregisterFactory(const QString& id)
    {
        {
            QMutexLocker locker(&m_mutex);
            if (m_entries.remove(id) == 0) {
                return false;
            }
            m_order.removeOne(id);
        }
        m_notifier.notifyUnregistered(id);
        return true;
    }

    // Remove every factory registered under @p ownerTag. Returns the count
    // removed. An empty tag matches nothing (untagged entries are never bulk-
    // removable — that would wipe the whole registry by accident); returns 0.
    int unregisterByOwner(const QString& ownerTag)
    {
        if (ownerTag.isEmpty()) {
            qWarning("PhosphorRegistry::Registry::unregisterByOwner: empty owner tag matches nothing, ignored");
            return 0;
        }
        QList<QString> removedIds;
        {
            QMutexLocker locker(&m_mutex);
            // Walk m_order (not m_entries) so removedIds — and therefore the
            // factoryUnregistered signals below — are in registration order,
            // not QHash's hash order. Consumers rely on the ordered-signal
            // contract (see signals_fireInRegistrationOrder).
            for (const QString& id : std::as_const(m_order)) {
                if (m_entries.value(id).ownerTag == ownerTag) {
                    removedIds.append(id);
                }
            }
            for (const QString& id : std::as_const(removedIds)) {
                m_entries.remove(id);
                m_order.removeOne(id);
            }
        }
        for (const QString& id : std::as_const(removedIds)) {
            m_notifier.notifyUnregistered(id);
        }
        return static_cast<int>(removedIds.size());
    }

    // Drop every entry at once WITHOUT firing per-entry signals. For bulk
    // teardown — a composition root shutting down, where consumers are tearing
    // down too and per-entry unregister notifications are noise. A caller that
    // needs to finalise object lifetimes (e.g. flush owned QObjects' deferred
    // deletes) should snapshot the entries via forEach() BEFORE calling clear().
    void clear()
    {
        QMutexLocker locker(&m_mutex);
        m_entries.clear();
        m_order.clear();
    }

    // Lookup. Returns the registered factory or a null shared_ptr if @p id is
    // unknown. The returned shared_ptr keeps the factory alive even if it is
    // unregistered concurrently.
    [[nodiscard]] std::shared_ptr<Factory> factory(const QString& id) const
    {
        QMutexLocker locker(&m_mutex);
        const auto it = m_entries.constFind(id);
        return it == m_entries.cend() ? std::shared_ptr<Factory>() : it.value().factory;
    }

    // Snapshot of currently-registered ids in REGISTRATION (insertion) order:
    // the order registerFactory was first called for each id, with a Replace
    // keeping the original position and an unregister removing it. Deterministic
    // and stable — consumers that need a different display order (e.g.
    // alphabetical) sort the result themselves; consumers that need the
    // composition / priority order (the layout-source composite, algorithm UI
    // lists) get it for free.
    [[nodiscard]] QList<QString> ids() const
    {
        QMutexLocker locker(&m_mutex);
        return m_order;
    }

    // Functional iteration over a SNAPSHOT taken under the lock, in
    // registration (insertion) order: the visitor is called as
    // `visitor(const std::shared_ptr<Factory>&)` for each factory, outside the
    // lock. Mutating the registry from the visitor is therefore safe (it
    // affects the next iteration, not this snapshot).
    template<typename Visitor>
    void forEach(Visitor&& visitor) const
    {
        QList<std::shared_ptr<Factory>> snapshot;
        {
            QMutexLocker locker(&m_mutex);
            snapshot.reserve(m_order.size());
            for (const QString& id : m_order) {
                const auto it = m_entries.constFind(id);
                if (it != m_entries.cend()) {
                    snapshot.append(it.value().factory);
                }
            }
        }
        for (const std::shared_ptr<Factory>& factory : std::as_const(snapshot)) {
            visitor(factory);
        }
    }

    // Access the signal carrier. Survives for the registry's lifetime;
    // consumers connect during composition-root setup and rely on Qt's
    // connection auto-disconnect when either end is destroyed.
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
        QMutexLocker locker(&m_mutex);
        return m_entries.isEmpty();
    }

    // Number of currently-registered factories.
    [[nodiscard]] qsizetype size() const
    {
        QMutexLocker locker(&m_mutex);
        return m_entries.size();
    }

private:
    struct Entry
    {
        std::shared_ptr<Factory> factory;
        QString ownerTag;
    };

    mutable QMutex m_mutex;
    QHash<QString, Entry> m_entries;
    // Registration (insertion) order of the ids in m_entries — appended on a
    // fresh register, position-preserved on a Replace, removed on unregister.
    // Backs ids() / forEach() so iteration is deterministic and stable rather
    // than QHash's hash order. Kept in lockstep with m_entries under m_mutex.
    QList<QString> m_order;
    // Owned by value; default-constructed parentless. notifier() hands out a
    // pointer so consumers can connect, but ownership stays inside the
    // Registry. Signals are emitted outside m_mutex (see registerFactory).
    RegistryNotifier m_notifier;
};

} // namespace PhosphorRegistry
