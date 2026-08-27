// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IPlacementState.h>

#include <functional>
#include <optional>
#include <type_traits>

#include <QHash>
#include <QString>
#include <QtGlobal>

namespace PhosphorEngine {

/// Rewrite the desktop dimension of a PlacementStateKey-keyed hash per
/// `oldToNew` (absent = unchanged). Companion to PerScreenStates::
/// renumberDesktops for the engines' auxiliary per-context maps (stash,
/// overrides, burst flags). Take-then-reinsert so shifted keys never collide.
template<typename ValueT>
void renumberDesktopKeyedHash(QHash<PlacementStateKey, ValueT>& hash, const QHash<int, int>& oldToNew)
{
    if (oldToNew.isEmpty()) {
        return;
    }
    QList<std::pair<PlacementStateKey, ValueT>> moved;
    for (auto it = hash.begin(); it != hash.end();) {
        const auto mapped = oldToNew.constFind(it.key().desktop);
        if (mapped != oldToNew.constEnd()) {
            PlacementStateKey newKey = it.key();
            newKey.desktop = mapped.value();
            moved.append({newKey, std::move(it.value())});
            it = hash.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& [key, value] : moved) {
        hash.insert(key, std::move(value));
    }
}

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

    // ── Reverse (window -> key) map ──────────────────────────────────────────
    bool hasWindow(const QString& windowId) const
    {
        return m_windowToKey.contains(windowId);
    }

    /// The owning key for `windowId`, or a default-constructed key when untracked
    /// (mirrors QHash::value — an empty screenId marks "not tracked").
    PlacementStateKey keyForWindow(const QString& windowId) const
    {
        return m_windowToKey.value(windowId);
    }

    /// The owning key for `windowId`, or nullopt when untracked.
    std::optional<PlacementStateKey> windowKey(const QString& windowId) const
    {
        auto it = m_windowToKey.constFind(windowId);
        if (it == m_windowToKey.constEnd()) {
            return std::nullopt;
        }
        return it.value();
    }

    void setKeyForWindow(const QString& windowId, const PlacementStateKey& key)
    {
        m_windowToKey.insert(windowId, key);
    }

    /// Drop the reverse-map entry for `windowId` (does not touch state objects).
    void removeWindow(const QString& windowId)
    {
        m_windowToKey.remove(windowId);
    }

    /// Remove and return the reverse-map entry for `windowId` (default key when
    /// absent), mirroring QHash::take.
    PlacementStateKey takeWindow(const QString& windowId)
    {
        return m_windowToKey.take(windowId);
    }

    /// Read-only view of the reverse map for iteration.
    const QHash<QString, PlacementStateKey>& windowKeys() const
    {
        return m_windowToKey;
    }

    /// Resolve the state that owns `windowId` (no create). When `outKey` is
    /// non-null it receives the window's owning key iff the window is tracked.
    StateT* forWindow(const QString& windowId, PlacementStateKey* outKey = nullptr) const
    {
        auto it = m_windowToKey.constFind(windowId);
        if (it == m_windowToKey.constEnd()) {
            return nullptr;
        }
        if (outKey) {
            *outKey = it.value();
        }
        return m_states.value(it.value());
    }

    /// Move a window's reverse-map entry from `oldKey` to `newKey`. Only the
    /// reverse map moves; the engine wraps its own remove-from-old / add-to-new
    /// state lifecycle hooks around this call. `oldKey` is the caller's asserted
    /// current key: the reverse map is authoritative, so `oldKey` only guards against
    /// a stale-caller bug (in debug builds) rather than driving the move.
    void migrate(const QString& windowId, const PlacementStateKey& oldKey, const PlacementStateKey& newKey)
    {
        Q_ASSERT(!m_windowToKey.contains(windowId) || m_windowToKey.value(windowId) == oldKey);
        Q_UNUSED(oldKey)
        m_windowToKey.insert(windowId, newKey);
    }

    /// Rewrite every reverse-map entry pointing at `oldKey` to `newKey`. Used
    /// when a whole state is re-keyed (sticky-pin desktop migration).
    void rekeyWindows(const PlacementStateKey& oldKey, const PlacementStateKey& newKey)
    {
        for (auto it = m_windowToKey.begin(); it != m_windowToKey.end(); ++it) {
            if (it.value() == oldKey) {
                it.value() = newKey;
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

    /// Reap all state for a destroyed virtual desktop: forward map (with the
    /// engine's teardown callback) and reverse map together. The dynamic-
    /// workspaces pass calls this for each removed desktop BEFORE renumbering
    /// the survivors.
    void reapDesktop(int desktop, const std::function<void(const PlacementStateKey&, StateT*)>& onRemove)
    {
        removeStatesIf(
            [desktop](const PlacementStateKey& key, StateT*) {
                return key.desktop == desktop;
            },
            onRemove);
        removeWindowsIf([desktop](const QString&, const PlacementStateKey& key) {
            return key.desktop == desktop;
        });
    }

    /// Rewrite the desktop dimension of every key per `oldToNew` (1-based ints;
    /// keys whose desktop is absent from the mapping are untouched). Both maps
    /// move atomically: forward entries are taken out first so a shifted key
    /// can never collide with a not-yet-shifted one. `skip` exempts sentinel
    /// keys (the snap engine's empty-screenId globals) from the rewrite.
    void renumberDesktops(const QHash<int, int>& oldToNew,
                          const std::function<bool(const PlacementStateKey&)>& skip = nullptr)
    {
        if (oldToNew.isEmpty()) {
            return;
        }
        QList<std::pair<PlacementStateKey, StateT*>> moved;
        for (auto it = m_states.begin(); it != m_states.end();) {
            const auto mapped = oldToNew.constFind(it.key().desktop);
            if (mapped != oldToNew.constEnd() && !(skip && skip(it.key()))) {
                PlacementStateKey newKey = it.key();
                newKey.desktop = mapped.value();
                moved.append({newKey, it.value()});
                it = m_states.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& [key, state] : moved) {
            m_states.insert(key, state);
        }
        for (auto it = m_windowToKey.begin(); it != m_windowToKey.end(); ++it) {
            const auto mapped = oldToNew.constFind(it.value().desktop);
            if (mapped != oldToNew.constEnd() && !(skip && skip(it.value()))) {
                it.value().desktop = mapped.value();
            }
        }
    }

    /// Drop reverse-map entries matching `pred` (e.g. a vanished desktop/activity).
    void removeWindowsIf(const std::function<bool(const QString&, const PlacementStateKey&)>& pred)
    {
        for (auto it = m_windowToKey.begin(); it != m_windowToKey.end();) {
            if (pred(it.key(), it.value())) {
                it = m_windowToKey.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    QHash<PlacementStateKey, StateT*> m_states; ///< key -> owning state (Qt-parent-owned by engine)
    QHash<QString, PlacementStateKey> m_windowToKey; ///< windowId -> owning state key
};

} // namespace PhosphorEngine
