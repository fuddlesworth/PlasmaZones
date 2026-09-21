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
#include <QSet>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace PhosphorEngine {

/// Exported because this is a header-only template used from every engine
/// library, so the category has to resolve outside phosphor-engine.
Q_DECLARE_EXPORTED_LOGGING_CATEGORY(lcPerScreenStates, PHOSPHORENGINE_EXPORT)

/// Whether a desktop renumber mapping may be applied at all. KWin desktops are
/// 1-based, so a mapped value below 1 is a poisoned mapping — and rejecting
/// only the offending ENTRY is worse than rejecting nothing: the key it names
/// stays on its old desktop while its siblings move onto that same number,
/// manufacturing exactly the collision the callers' injectivity precondition
/// exists to rule out. So the verdict is all-or-nothing, for every consumer of
/// the mapping, and a refusal leaves every map untouched.
inline bool desktopRenumberMappingIsValid(const QHash<int, int>& oldToNew)
{
    for (auto it = oldToNew.constBegin(); it != oldToNew.constEnd(); ++it) {
        if (it.value() < 1) {
            qCWarning(lcPerScreenStates) << "PhosphorEngine: refusing a desktop renumber mapping with a target below 1"
                                         << it.key() << "->" << it.value();
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
            qCWarning(lcPerScreenStates)
                << "PhosphorEngine::renumberDesktopKeyedHash: target key (desktop" << key.desktop
                << ") is already occupied — mapping is not injective, or an unmapped key holds the target; "
                   "the entry already there is being replaced";
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
/// from windowId to the keys of every state holding it (its memberships, with
/// a context resolver picking the primary). The snap, autotile and scroll
/// engines manage exactly this pair; this template holds it once so the
/// lockstep bookkeeping (lazy create, membership maintenance, migration,
/// prune) is written once.
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

    /// Resolve the state holding `windowId`'s PRIMARY membership (no create).
    /// When `outKey` is non-null it receives that key iff the window is
    /// tracked, including a dangling entry whose forward state is gone, where
    /// the return value is nullptr but `outKey` is still written. Check the
    /// returned state, not `outKey`, to decide whether the window resolved. A
    /// multi-membership window has other states too; see membershipsForWindow.
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
        // A move onto itself is a no-op, not a merge: the merge arm below
        // would find newKey already held and remove the entry at `at`, which
        // for a single-membership window is its only one.
        if (oldKey == newKey) {
            return;
        }
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
                qCWarning(lcPerScreenStates)
                    << "PhosphorEngine::PerScreenStates::renumberDesktops: target key (desktop" << key.desktop
                    << ") is already occupied — mapping is not injective, or an unmapped key holds the target; "
                       "the state already there is being replaced";
            }
            m_states.insert(key, state);
        }
        // Every membership shifts, not just the primary: a window present on
        // several desktops holds a key per desktop and each one names a state
        // the forward pass above just moved. Rebuilt rather than rewritten in
        // place so a shifted key landing on one the window already holds (the
        // non-injective case warned about above) merges instead of duplicating.
        for (auto it = m_windowMemberships.begin(); it != m_windowMemberships.end(); ++it) {
            QList<PlacementStateKey> shifted;
            shifted.reserve(it.value().size());
            for (PlacementStateKey key : std::as_const(it.value())) {
                int newDesktop = 0;
                if (shifts(key, newDesktop)) {
                    key.desktop = newDesktop;
                }
                if (!shifted.contains(key)) {
                    shifted.append(key);
                }
            }
            it.value() = shifted;
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
