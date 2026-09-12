// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <phosphorengine_export.h>

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IPlacementState.h>

#include <functional>
#include <optional>
#include <type_traits>

#include <QHash>
#include <QList>
#include <QLoggingCategory>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace PhosphorEngine {

/// Exported because this is a header-only template used from every engine
/// library, so the category has to resolve outside phosphor-engine.
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(lcPerScreenStates, PHOSPHORENGINE_EXPORT)

/// The two cooperating maps a per-monitor placement engine keeps: a forward map
/// from PlacementStateKey to the owning per-screen state object (Qt-parent-owned
/// by the engine, constructed via a caller-supplied factory), and a reverse map
/// from windowId to its owning key. Both the snap engine (SnapState) and the
/// autotile engine (TilingState) manage exactly this pair; this template holds
/// it once so the lockstep bookkeeping (lazy create, reverse-map maintenance,
/// migration, prune) is written once.
///
/// StateT must implement PhosphorEngine::IPlacementState.
///
/// Engine-specific lifecycle (algorithm hooks, retile scheduling, overflow
/// bookkeeping, state teardown) is deliberately OUT of this container: the
/// mutation and prune helpers take the engine's callbacks so the engine wraps
/// its own hooks around the pure map moves.
template<typename StateT>
class PerScreenStates
{
    static_assert(std::is_base_of_v<IPlacementState, StateT>,
                  "PerScreenStates<StateT>: StateT must implement PhosphorEngine::IPlacementState");

public:
    /// Lazily creates the state for `key` if absent. `factory` is invoked only on
    /// a miss; if it returns nullptr (e.g. the engine rejected an unknown screen)
    /// nothing is inserted and nullptr is returned.
    StateT* forKey(const PlacementStateKey& key, const std::function<StateT*()>& factory)
    {
        auto it = m_states.find(key);
        if (it != m_states.end()) {
            return it.value();
        }
        StateT* created = factory ? factory() : nullptr;
        if (created) {
            m_states.insert(key, created);
        }
        return created;
    }

    /// The state for `key`, or nullptr if none exists (never creates).
    StateT* stateForKey(const PlacementStateKey& key) const
    {
        return m_states.value(key);
    }

    bool containsKey(const PlacementStateKey& key) const
    {
        return m_states.contains(key);
    }

    /// Insert/replace the state at `key` (caller retains ownership semantics).
    void insertState(const PlacementStateKey& key, StateT* state)
    {
        m_states.insert(key, state);
    }

    /// Remove and return the state at `key` (nullptr if absent). Does not delete.
    StateT* takeState(const PlacementStateKey& key)
    {
        return m_states.take(key);
    }

    int stateCount() const
    {
        return m_states.size();
    }

    /// Read-only view of the forward map for iteration.
    const QHash<PlacementStateKey, StateT*>& states() const
    {
        return m_states;
    }

    // ── Reverse (window -> memberships) map ──────────────────────────────────
    //
    // A window is a member of every state that holds it. Almost always that is
    // exactly one, and for those the accessors below answer precisely what the
    // single-key map they replaced answered. More than one arises when a window
    // is present on several desktops at once — sticky (on all of them), or a
    // span like {1,2} — and the engines give it a place in each, so it can be
    // tiled or snapped differently per desktop instead of existing in whichever
    // context happened to be current when it opened.

    /// Resolves the key a screen is CURRENTLY showing. The container cannot
    /// know this (the engine owns the context tracker), so the engine installs
    /// its currentKeyForScreen here and multi-membership lookups use it to pick
    /// the membership in view. Optional: without it the first membership wins,
    /// which is the right answer for every single-membership window.
    using ContextKeyResolver = std::function<PlacementStateKey(const QString& screenId)>;

    void setContextKeyResolver(ContextKeyResolver resolver)
    {
        m_contextKeyResolver = std::move(resolver);
    }

    bool hasWindow(const QString& windowId) const
    {
        return m_windowMemberships.contains(windowId);
    }

    /// The PRIMARY key for `windowId`, or a default-constructed key when
    /// untracked (an empty screenId marks "not tracked", as before).
    PlacementStateKey keyForWindow(const QString& windowId) const
    {
        return primaryOf(m_windowMemberships.value(windowId));
    }

    /// The PRIMARY key for `windowId`, or nullopt when untracked.
    std::optional<PlacementStateKey> windowKey(const QString& windowId) const
    {
        auto it = m_windowMemberships.constFind(windowId);
        if (it == m_windowMemberships.constEnd()) {
            return std::nullopt;
        }
        return primaryOf(it.value());
    }

    /// Every state this window belongs to, in the order they were added.
    QList<PlacementStateKey> membershipsForWindow(const QString& windowId) const
    {
        return m_windowMemberships.value(windowId);
    }

    /// Whether `windowId` belongs to the state at `key` specifically. The
    /// question to ask instead of comparing keyForWindow() against a key: once
    /// a window can be in two states at once, "its key is not that one" stops
    /// meaning "it is not in that one".
    bool hasMembership(const QString& windowId, const PlacementStateKey& key) const
    {
        return m_windowMemberships.value(windowId).contains(key);
    }

    /// Make `key` the window's ONLY membership. This is ordinary placement:
    /// the state named by `key` owns the window outright, and any membership
    /// it held elsewhere is gone. Adopting a window into an ADDITIONAL context
    /// is addMembership.
    void setKeyForWindow(const QString& windowId, const PlacementStateKey& key)
    {
        m_windowMemberships.insert(windowId, {key});
    }

    /// Add `key` to the window's memberships, keeping the ones it has. No-op
    /// when it is already a member.
    void addMembership(const QString& windowId, const PlacementStateKey& key)
    {
        QList<PlacementStateKey>& keys = m_windowMemberships[windowId];
        if (!keys.contains(key)) {
            keys.append(key);
        }
    }

    /// Drop one membership. The window stops being tracked entirely when its
    /// last one goes, so `hasWindow` cannot answer true for a window no state
    /// holds.
    void removeMembership(const QString& windowId, const PlacementStateKey& key)
    {
        auto it = m_windowMemberships.find(windowId);
        if (it == m_windowMemberships.end()) {
            return;
        }
        it.value().removeAll(key);
        if (it.value().isEmpty()) {
            m_windowMemberships.erase(it);
        }
    }

    /// Drop EVERY membership for `windowId` (does not touch state objects).
    void removeWindow(const QString& windowId)
    {
        m_windowMemberships.remove(windowId);
    }

    /// Remove every membership and return the primary (default key when
    /// absent), mirroring QHash::take.
    PlacementStateKey takeWindow(const QString& windowId)
    {
        return primaryOf(m_windowMemberships.take(windowId));
    }

    /// Every tracked window id, once each however many states hold it.
    QStringList trackedWindowIds() const
    {
        return m_windowMemberships.keys();
    }

    int trackedWindowCount() const
    {
        return m_windowMemberships.size();
    }

    /// Visit every (windowId, key) membership pair. For sweeps that have to
    /// see a multi-membership window once per state rather than once.
    void forEachMembership(const std::function<void(const QString&, const PlacementStateKey&)>& fn) const
    {
        if (!fn) {
            return;
        }
        for (auto it = m_windowMemberships.cbegin(); it != m_windowMemberships.cend(); ++it) {
            for (const PlacementStateKey& key : it.value()) {
                fn(it.key(), key);
            }
        }
    }

    /// Resolve the state that owns `windowId` (no create). When `outKey` is
    /// non-null it receives the window's owning key iff the window is tracked.
    StateT* forWindow(const QString& windowId, PlacementStateKey* outKey = nullptr) const
    {
        auto it = m_windowMemberships.constFind(windowId);
        if (it == m_windowMemberships.constEnd()) {
            return nullptr;
        }
        const PlacementStateKey key = primaryOf(it.value());
        if (outKey) {
            *outKey = key;
        }
        return m_states.value(key);
    }

    /// Move a window's reverse-map entry from `oldKey` to `newKey`. Only the
    /// reverse map moves; the engine wraps its own remove-from-old / add-to-new
    /// state lifecycle hooks around this call. `oldKey` is the caller's asserted
    /// current key: the reverse map is authoritative, so `oldKey` only reports a
    /// stale-caller bug rather than driving the move.
    void migrate(const QString& windowId, const PlacementStateKey& oldKey, const PlacementStateKey& newKey)
    {
        // Debug assert AND a release-build warning for the same condition: a
        // stale-caller bug silently rewrote the reverse map in release, which
        // is exactly the case that leaves a window resolving to a state that
        // does not hold it. The move still happens either way — the map is
        // authoritative and refusing here would strand the window worse — so
        // this reports rather than guards.
        auto it = m_windowMemberships.find(windowId);
        if (it != m_windowMemberships.end() && !it.value().contains(oldKey)) {
            Q_ASSERT(false);
            qCWarning(lcPerScreenStates) << "PerScreenStates::migrate: caller asserted" << windowId << "was on desktop"
                                         << oldKey.desktop << "of screen" << oldKey.screenId
                                         << "but its memberships are" << it.value().size() << "key(s) not including"
                                         << "that one — migrating the primary instead";
        }
        if (it == m_windowMemberships.end()) {
            m_windowMemberships.insert(windowId, {newKey});
            return;
        }
        // Move the asserted membership, leaving the window's OTHER contexts
        // alone: a multi-desktop window migrating on one desktop keeps its
        // place on the rest. Falls back to the primary when the asserted key
        // is not held, which is the stale-caller case warned about above.
        QList<PlacementStateKey>& keys = it.value();
        const qsizetype at = keys.indexOf(keys.contains(oldKey) ? oldKey : primaryOf(keys));
        if (at < 0) {
            keys.append(newKey);
            return;
        }
        if (keys.contains(newKey)) {
            keys.removeAt(at); // already a member there; the move is a merge
        } else {
            keys[at] = newKey;
        }
    }

    /// Rewrite every membership equal to `oldKey` to `newKey`. Used when a
    /// whole state is re-keyed (sticky-pin desktop migration).
    void rekeyWindows(const PlacementStateKey& oldKey, const PlacementStateKey& newKey)
    {
        for (auto it = m_windowMemberships.begin(); it != m_windowMemberships.end(); ++it) {
            QList<PlacementStateKey>& keys = it.value();
            const qsizetype at = keys.indexOf(oldKey);
            if (at < 0) {
                continue;
            }
            if (keys.contains(newKey)) {
                keys.removeAt(at);
            } else {
                keys[at] = newKey;
            }
        }
    }

    /// Lockstep prune of the forward map: for every state matching `pred`,
    /// invoke `onRemove` (engine-specific teardown) BEFORE dropping the entry.
    /// The reverse map is left to the caller (release paths collect released
    /// windows and clean the reverse map separately; desktop/activity prunes use
    /// removeWindowsIf()).
    void removeStatesIf(const std::function<bool(const PlacementStateKey&, StateT*)>& pred,
                        const std::function<void(const PlacementStateKey&, StateT*)>& onRemove)
    {
        for (auto it = m_states.begin(); it != m_states.end();) {
            if (pred(it.key(), it.value())) {
                if (onRemove) {
                    onRemove(it.key(), it.value());
                }
                it = m_states.erase(it);
            } else {
                ++it;
            }
        }
    }

    /// Drop MEMBERSHIPS matching `pred` (e.g. a vanished desktop/activity).
    /// Per membership, not per window: a window present on several desktops
    /// loses only the ones that went away, and drops out of tracking entirely
    /// when the last of them does.
    void removeWindowsIf(const std::function<bool(const QString&, const PlacementStateKey&)>& pred)
    {
        if (!pred) {
            return;
        }
        for (auto it = m_windowMemberships.begin(); it != m_windowMemberships.end();) {
            QList<PlacementStateKey>& keys = it.value();
            const QString& windowId = it.key();
            keys.removeIf([&pred, &windowId](const PlacementStateKey& key) {
                return pred(windowId, key);
            });
            if (keys.isEmpty()) {
                it = m_windowMemberships.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    /// The membership in the context its screen is currently showing, else the
    /// first. Single-membership windows — everything but a multi-desktop one —
    /// short-circuit on the size check without consulting the resolver.
    PlacementStateKey primaryOf(const QList<PlacementStateKey>& keys) const
    {
        if (keys.isEmpty()) {
            return {};
        }
        if (keys.size() == 1 || !m_contextKeyResolver) {
            return keys.first();
        }
        for (const PlacementStateKey& key : keys) {
            if (m_contextKeyResolver(key.screenId) == key) {
                return key;
            }
        }
        // Every membership is off-context (the window is on desktops the user
        // is not looking at on any of its screens). Any of them beats none:
        // the callers that land here are asking "where is this window tracked",
        // and a null answer reads as untracked, which it is not.
        return keys.first();
    }

    QHash<PlacementStateKey, StateT*> m_states; ///< key -> owning state (Qt-parent-owned by engine)
    QHash<QString, QList<PlacementStateKey>> m_windowMemberships; ///< windowId -> every state holding it
    ContextKeyResolver m_contextKeyResolver;
};

} // namespace PhosphorEngine
