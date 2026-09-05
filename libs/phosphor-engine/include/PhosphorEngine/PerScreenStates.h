// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorEngine/EngineTypes.h>
#include <PhosphorEngine/IPlacementState.h>

#include <functional>
#include <optional>
#include <type_traits>

#include <QHash>
#include <QSet>
#include <QString>
#include <QtGlobal>

namespace PhosphorEngine {

/// Whether a desktop renumber mapping may be applied at all. KWin desktops are
/// 1-based, so a mapped value below 1 is a poisoned mapping — and rejecting
/// only the offending ENTRY is worse than rejecting nothing: the key it names
/// stays on its old desktop while its siblings move onto that same number,
/// manufacturing exactly the collision the callers' injectivity precondition
/// exists to rule out. So the verdict is all-or-nothing, for every consumer of
/// the mapping, and a refusal leaves every map untouched.
///
/// The warnings in this header are plain qWarning, not qCWarning: phosphor-engine
/// declares no logging category, and this is a header-only template consumed by
/// three engines that each have their own — a category defined here would either
/// leak into all of them or force a new library-level one for four call sites.
inline bool desktopRenumberMappingIsValid(const QHash<int, int>& oldToNew)
{
    for (auto it = oldToNew.constBegin(); it != oldToNew.constEnd(); ++it) {
        if (it.value() < 1) {
            qWarning("PhosphorEngine: refusing a desktop renumber mapping with a target below 1 (%d -> %d)", it.key(),
                     it.value());
            return false;
        }
    }
    return true;
}

/// Rewrite the desktop dimension of a PlacementStateKey-keyed hash per
/// `oldToNew` (absent = unchanged). A mapping carrying any target below 1 is
/// refused WHOLE (desktopRenumberMappingIsValid), never per entry.
/// Companion to PerScreenStates::renumberDesktops for the engines'
/// auxiliary per-context maps (stash, overrides, burst flags).
/// `oldToNew` must be INJECTIVE over the desktops actually present, and no
/// mapped-to desktop may already be held by an unmapped key — the same
/// precondition PerScreenStates::renumberDesktops carries, surfaced the same
/// way (a warning on the collision, in every build) because the same violation
/// silently drops an entry.
/// Take-then-reinsert so shifted keys never collide. No `skip` predicate,
/// unlike PerScreenStates::renumberDesktops: the aux maps engines pass here
/// never hold sentinel keys (the snap engine's empty-screenId globals live in
/// its state map, not in any keyed hash). A future aux map that did carry
/// sentinels would need the same exemption renumberDesktops has.
template<typename ValueT>
void renumberDesktopKeyedHash(QHash<PlacementStateKey, ValueT>& hash, const QHash<int, int>& oldToNew)
{
    if (oldToNew.isEmpty() || !desktopRenumberMappingIsValid(oldToNew)) {
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
        // Same injectivity precondition, and the same failure, as
        // PerScreenStates::renumberDesktops: a target already held by an
        // unmapped key makes the insert drop the value that was there. WARN
        // ONLY, deliberately — no assert. desktopRenumberMappingIsValid is the
        // gate every caller runs, and it sees only the mapping, so it cannot
        // rule this out: whether an UNMAPPED key already holds a target is a
        // property of the hash, not of oldToNew. Asserting on a condition the
        // gate structurally cannot cover would abort a debug daemon on a
        // mapping the gate accepted, while release merely overwrites. The
        // warning names the collision either way, and the caller contract
        // above is the real defence.
        if (hash.contains(key)) {
            qWarning(
                "PhosphorEngine::renumberDesktopKeyedHash: target key (desktop %d) is already occupied — "
                "mapping is not injective, or an unmapped key holds the target; the entry already there is "
                "being replaced",
                key.desktop);
        }
        hash.insert(key, std::move(value));
    }
}

/// Set flavour of renumberDesktopKeyedHash, for an engine's auxiliary
/// per-context SETS (an armed-context marker with no value of its own). Same
/// gate, same take-then-reinsert, and the same injectivity precondition. There
/// is no collision warning here because a set has no value to lose: two keys
/// mapping onto one simply coalesce into the single entry the caller wanted.
inline void renumberDesktopKeyedSet(QSet<PlacementStateKey>& set, const QHash<int, int>& oldToNew)
{
    if (oldToNew.isEmpty() || !desktopRenumberMappingIsValid(oldToNew)) {
        return;
    }
    QList<PlacementStateKey> moved;
    for (auto it = set.begin(); it != set.end();) {
        const auto mapped = oldToNew.constFind(it->desktop);
        if (mapped != oldToNew.constEnd()) {
            PlacementStateKey newKey = *it;
            newKey.desktop = mapped.value();
            moved.append(newKey);
            it = set.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& key : moved) {
        set.insert(key);
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
    /// Replacing an existing entry does NOT tear the old state down or touch
    /// the reverse map — a caller for whom a state may already exist at `key`
    /// must takeState() first and run its own teardown, or the replaced
    /// state's windows keep resolving to a state no longer in the forward map.
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
    /// non-null it receives the window's owning key whenever a reverse-map
    /// entry exists — including a dangling entry whose forward state is gone,
    /// where the return value is nullptr but `outKey` is still written. Check
    /// the returned state, not `outKey`, to decide whether the window resolved.
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
        const auto tracked = m_windowToKey.constFind(windowId);
        const bool staleCaller = tracked != m_windowToKey.constEnd() && !(tracked.value() == oldKey);
        if (staleCaller) {
            // Diagnostic only — the reverse map is authoritative and the move
            // proceeds either way; the caller's asserted key being stale means
            // ITS bookkeeping is wrong, which the log surfaces in release too.
            qWarning("PhosphorEngine::PerScreenStates::migrate: caller's oldKey is stale for window %ls",
                     qUtf16Printable(windowId));
        }
        Q_ASSERT(!staleCaller);
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
    /// engine's teardown callback) and reverse map together. `skip` exempts
    /// sentinel keys (the snap engine's empty-screenId globals) exactly as it
    /// does in renumberDesktops. No production caller today: each engine's
    /// reapDesktopState composes its existing count-based prune with its own
    /// value-side sweeps instead. Kept as the single, combined identity-based
    /// form of that sweep and exercised by the container's tests.
    void reapDesktop(int desktop, const std::function<void(const PlacementStateKey&, StateT*)>& onRemove,
                     const std::function<bool(const PlacementStateKey&)>& skip = nullptr)
    {
        removeStatesIf(
            [desktop, &skip](const PlacementStateKey& key, StateT*) {
                return key.desktop == desktop && !(skip && skip(key));
            },
            onRemove);
        removeWindowsIf([desktop, &skip](const QString&, const PlacementStateKey& key) {
            return key.desktop == desktop && !(skip && skip(key));
        });
    }

    /// Rewrite the desktop dimension of every key per `oldToNew` (1-based ints;
    /// keys whose desktop is absent from the mapping are untouched). A mapping
    /// carrying ANY target below 1 is refused whole and nothing moves — see
    /// desktopRenumberMappingIsValid for why a per-entry refusal is unsafe.
    /// Both maps move atomically: forward entries are taken out first so a
    /// shifted key can never collide with a not-yet-shifted one. `skip` exempts
    /// sentinel keys (the snap engine's empty-screenId globals) from the
    /// rewrite.
    ///
    /// `oldToNew` must be INJECTIVE over the desktops actually present, and no
    /// mapped-to desktop may already be held by an unmapped (or skipped) key.
    /// Either violation makes two states land on one key and the later insert
    /// silently drops the earlier state, leaking it and stranding its windows'
    /// reverse entries. The reconciler derives the map from a KWin id-list
    /// delta, where both hold; the assert below catches a caller that does not.
    void renumberDesktops(const QHash<int, int>& oldToNew,
                          const std::function<bool(const PlacementStateKey&)>& skip = nullptr)
    {
        if (oldToNew.isEmpty() || !desktopRenumberMappingIsValid(oldToNew)) {
            return;
        }
        const auto shifts = [&oldToNew, &skip](const PlacementStateKey& key, int& newDesktop) {
            const auto mapped = oldToNew.constFind(key.desktop);
            if (mapped == oldToNew.constEnd() || (skip && skip(key))) {
                return false;
            }
            newDesktop = mapped.value();
            return true;
        };
        QList<std::pair<PlacementStateKey, StateT*>> moved;
        for (auto it = m_states.begin(); it != m_states.end();) {
            int newDesktop = 0;
            if (shifts(it.key(), newDesktop)) {
                PlacementStateKey newKey = it.key();
                newKey.desktop = newDesktop;
                moved.append({newKey, it.value()});
                it = m_states.erase(it);
            } else {
                ++it;
            }
        }
        for (const auto& [key, state] : moved) {
            // Warn only, for the reason spelled out in
            // renumberDesktopKeyedHash: the shared validity gate sees the
            // mapping alone and cannot know whether an unmapped key already
            // holds a target, so an assert here would abort a debug daemon on
            // a mapping the gate passed. A silent overwrite would leak the
            // displaced state and strand its windows' reverse entries, so the
            // collision is still surfaced, matching migrate() above.
            if (m_states.contains(key)) {
                qWarning(
                    "PhosphorEngine::PerScreenStates::renumberDesktops: target key (desktop %d) is already "
                    "occupied — mapping is not injective, or an unmapped key holds the target; the state "
                    "already there is being replaced",
                    key.desktop);
            }
            m_states.insert(key, state);
        }
        for (auto it = m_windowToKey.begin(); it != m_windowToKey.end(); ++it) {
            int newDesktop = 0;
            if (shifts(it.value(), newDesktop)) {
                it.value().desktop = newDesktop;
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
