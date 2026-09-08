// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <PhosphorAnimation/ProfilePaths.h>

#include <QMetaObject>
#include <QStringList>
#include <QThread>

namespace PhosphorAnimation {

namespace {

template<typename Func>
void emitThreadSafe(QObject* obj, Func&& fn)
{
    if (QThread::currentThread() == obj->thread()) {
        fn();
    } else {
        QMetaObject::invokeMethod(obj, std::forward<Func>(fn), Qt::QueuedConnection);
    }
}

} // namespace

std::atomic<PhosphorProfileRegistry*> PhosphorProfileRegistry::s_defaultRegistry{nullptr};

PhosphorProfileRegistry::PhosphorProfileRegistry(QObject* parent)
    : QObject(parent)
{
}

PhosphorProfileRegistry::~PhosphorProfileRegistry() = default;

void PhosphorProfileRegistry::setDefaultRegistry(PhosphorProfileRegistry* registry)
{
    // Relaxed store: the publishing thread must have fully constructed
    // *registry before calling this method, and the consumer-side
    // load-and-use sequence is a single pointer dereference that needs
    // no happens-before with any other memory. Matches the
    // PhosphorCurve::setDefaultRegistry contract.
    s_defaultRegistry.store(registry, std::memory_order_release);
}

PhosphorProfileRegistry* PhosphorProfileRegistry::defaultRegistry()
{
    return s_defaultRegistry.load(std::memory_order_acquire);
}

std::optional<Profile> PhosphorProfileRegistry::resolve(const QString& path) const
{
    // Exact path, highest precedence first: a non-seed entry if one is
    // registered here, otherwise the seed. Answering "nothing here" for a path
    // that carries only a seed would be a lie — the seed is what that path
    // resolves to, and a registry whose entire content is seeds (the shell
    // tier's) would look empty.
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_profiles.constFind(path);
    if (it != m_profiles.constEnd()) {
        return *it;
    }
    auto seed = m_seedProfiles.constFind(path);
    if (seed != m_seedProfiles.constEnd()) {
        return *seed;
    }
    return std::nullopt;
}

Profile PhosphorProfileRegistry::resolveWithInheritance(const QString& path,
                                                        const QString& /*lowPrecedenceOwnerTag*/) const
{
    // The tag argument is ignored: seed-ness is now decided by which store an
    // entry lives in, chosen at write time against the registry's configured
    // tag. Passing a DIFFERENT tag here used to select a different layer;
    // there is no longer any layer for it to select. Retained so existing
    // callers keep compiling, and it simply forwards.
    return resolveWithInheritance(path);
}

Profile PhosphorProfileRegistry::resolveWithInheritance(const QString& path) const
{
    // Build the chain root-first so the overlay walk runs shallow →
    // deep, with each engaged optional in a deeper entry replacing
    // the shallower one. Mirrors ProfileTree::resolve and
    // ShaderProfileTree::resolve so consumers see consistent
    // inheritance semantics regardless of which container holds the
    // overrides.
    QStringList chain;
    QString cursor = path;
    while (!cursor.isEmpty()) {
        chain.prepend(cursor);
        cursor = ProfilePaths::parentPath(cursor);
    }

    // Same overlay rule as ProfileTree::overlay — every engaged
    // optional in src wins; unset (nullopt) fields in src leave dst
    // alone (the inheritance mechanism). Curve / duration /
    // minDistance / sequenceMode / staggerInterval / presetName are
    // independent — a child that only set `duration` still inherits
    // the parent's `curve`.
    const auto overlay = [](Profile& dst, const Profile& src) {
        if (src.curve) {
            dst.curve = src.curve;
        }
        if (src.duration) {
            dst.duration = src.duration;
        }
        if (src.minDistance) {
            dst.minDistance = src.minDistance;
        }
        if (src.sequenceMode) {
            dst.sequenceMode = src.sequenceMode;
        }
        if (src.staggerInterval) {
            dst.staggerInterval = src.staggerInterval;
        }
        if (src.presetName) {
            dst.presetName = src.presetName;
        }
    };

    Profile effective; // default-constructed: every optional nullopt
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Pass 1: the low-precedence (seed) layer. Store membership IS
        // the owner, so no tag lookup is needed and the pass runs even
        // when no tag is configured — a registry with no seeds simply
        // has an empty store and pass 1 contributes nothing.
        for (const QString& step : chain) {
            const auto it = m_seedProfiles.constFind(step);
            if (it == m_seedProfiles.constEnd()) {
                continue;
            }
            overlay(effective, *it);
        }

        // Pass 2: everything NOT in the seed layer. These are Settings
        // publishes (direct/empty owner) and user JSONs (loader-tagged
        // owner). Always overlays after pass 1, so a user edit at any
        // depth wins over any seed at any depth. No owner filter is
        // needed: seeds are not in `m_profiles` at all.
        for (const QString& step : chain) {
            const auto it = m_profiles.constFind(step);
            if (it == m_profiles.constEnd()) {
                continue;
            }
            overlay(effective, *it);
        }
    }
    return effective.withDefaults();
}

void PhosphorProfileRegistry::setLowPrecedenceOwnerTag(const QString& tag)
{
    // Migrate rather than just assign. Seed-ness is decided at WRITE time by
    // comparing against this tag, so a caller that registers a seed before
    // setting the tag would land it in `m_profiles`, where pass 1 can no
    // longer see it. Moving entries on a tag change makes the ordering of
    // the two calls irrelevant instead of a silent trap. All three
    // composition roots set the tag first today; this keeps that from being
    // load-bearing.
    QStringList moved;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_lowPrecedenceOwnerTag == tag) {
            return;
        }

        // Entries previously treated as seeds go back to the non-seed store
        // under the OLD tag, which is what they would have been had the new
        // tag been set from the start.
        if (!m_lowPrecedenceOwnerTag.isEmpty() && !m_seedProfiles.isEmpty()) {
            const QString oldTag = m_lowPrecedenceOwnerTag;
            for (auto it = m_seedProfiles.constBegin(); it != m_seedProfiles.constEnd(); ++it) {
                if (!m_profiles.contains(it.key())) {
                    m_profiles.insert(it.key(), it.value());
                    m_owners.insert(it.key(), oldTag);
                    moved.append(it.key());
                }
            }
            m_seedProfiles.clear();
        }

        m_lowPrecedenceOwnerTag = tag;

        // Entries already stored under the new tag become seeds.
        if (!tag.isEmpty()) {
            QStringList promote;
            for (auto it = m_owners.constBegin(); it != m_owners.constEnd(); ++it) {
                if (it.value() == tag) {
                    promote.append(it.key());
                }
            }
            for (const QString& path : std::as_const(promote)) {
                m_seedProfiles.insert(path, m_profiles.value(path));
                m_profiles.remove(path);
                m_owners.remove(path);
                moved.append(path);
            }
        }
    }

    // A migration changes what every affected path resolves to, so consumers
    // must re-resolve exactly as they would after any other write.
    for (const QString& path : std::as_const(moved)) {
        emitThreadSafe(this, [this, path] {
            Q_EMIT profileChanged(path);
        });
    }
}

void PhosphorProfileRegistry::registerProfile(const QString& path, const Profile& profile)
{
    registerProfile(path, profile, QString());
}

void PhosphorProfileRegistry::registerProfile(const QString& path, const Profile& profile, const QString& ownerTag)
{
    // Only emit when the stored value actually changes. Without this
    // guard, a consumer that re-registers on every settings tick (the
    // daemon's publishActiveAnimationProfile fan-out) would produce a
    // storm of profileChanged signals for every bound animation, each
    // forcing a re-resolve even when nothing semantically changed.
    //
    // The owner tag is ALSO compared — re-registering a byte-identical
    // Profile under a different owner is a semantic change (it moves
    // the entry in and out of a loader's replace set on later
    // `reloadFromOwner` calls), so consumers need to see that.
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // A seed write goes to the seed store and must be compared against
        // THAT store. Comparing it against `m_profiles` would report
        // "changed" whenever a byte-identical user entry already sits at the
        // path (same value, different owner) and write the seed into the
        // non-seed store, putting it straight back into pass 2 — the exact
        // shadowing the separate store exists to remove.
        if (!m_lowPrecedenceOwnerTag.isEmpty() && ownerTag == m_lowPrecedenceOwnerTag) {
            auto seed = m_seedProfiles.find(path);
            if (seed == m_seedProfiles.end() || !(*seed == profile)) {
                m_seedProfiles.insert(path, profile);
                changed = true;
            }
        } else {
            auto it = m_profiles.find(path);
            const QString existingOwner = m_owners.value(path);
            if (it == m_profiles.end() || !(*it == profile) || existingOwner != ownerTag) {
                m_profiles.insert(path, profile);
                if (ownerTag.isEmpty()) {
                    m_owners.remove(path);
                } else {
                    m_owners.insert(path, ownerTag);
                }
                changed = true;
            }
        }
    }
    if (!changed) {
        return;
    }
    emitThreadSafe(this, [this, path] {
        Q_EMIT profileChanged(path);
    });
}

void PhosphorProfileRegistry::unregisterProfile(const QString& path)
{
    bool existed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        existed = (m_profiles.remove(path) > 0);
        m_owners.remove(path);
    }
    if (existed) {
        emitThreadSafe(this, [this, path] {
            Q_EMIT profileChanged(path);
        });
    }
}

void PhosphorProfileRegistry::reloadFromOwner(const QString& ownerTag, const QHash<QString, Profile>& profiles)
{
    // An empty owner tag aliases with the direct-owner path and would
    // let a loader silently wipe daemon-published entries on rescan —
    // the exact bug this method exists to prevent. Fail loud so callers
    // can't accidentally bypass the partitioning.
    //
    // Q_ASSERT_X traps in debug; the early return below additionally
    // guards release builds where Q_ASSERT compiles out — without it
    // a release-mode caller passing "" would silently corrupt the
    // partition map. Match-shape with clearOwner() below.
    Q_ASSERT_X(!ownerTag.isEmpty(), "PhosphorProfileRegistry::reloadFromOwner",
               "ownerTag must be non-empty; pass a stable per-publisher identifier");
    if (ownerTag.isEmpty()) {
        qWarning("PhosphorProfileRegistry::reloadFromOwner: refusing empty ownerTag (would alias with direct-owner)");
        return;
    }

    // Two-phase: compute the diff under the lock (so the snapshot is
    // consistent), then emit signals outside the lock.
    QStringList pathsRemoved;
    QStringList pathsChanged;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // The seed layer lives in its own store, so a reload of it replaces
        // that store wholesale. The direct-owner-wins rule below does not
        // apply: it protects untagged entries from being clobbered by a
        // loader, and seeds cannot collide with them any more.
        if (!m_lowPrecedenceOwnerTag.isEmpty() && ownerTag == m_lowPrecedenceOwnerTag) {
            for (auto it = m_seedProfiles.constBegin(); it != m_seedProfiles.constEnd(); ++it) {
                if (!profiles.contains(it.key())) {
                    pathsRemoved.append(it.key());
                }
            }
            for (auto it = profiles.constBegin(); it != profiles.constEnd(); ++it) {
                const auto existing = m_seedProfiles.constFind(it.key());
                if (existing == m_seedProfiles.constEnd() || !(*existing == it.value())) {
                    pathsChanged.append(it.key());
                }
            }
            m_seedProfiles = profiles;
        } else {
            // Paths previously owned by this tag but NOT in the new map —
            // the user deleted their JSON file, so we must unregister.
            for (auto it = m_owners.constBegin(); it != m_owners.constEnd(); ++it) {
                if (it.value() == ownerTag && !profiles.contains(it.key())) {
                    pathsRemoved.append(it.key());
                }
            }
            for (const QString& path : std::as_const(pathsRemoved)) {
                m_profiles.remove(path);
                m_owners.remove(path);
            }

            // Paths in the new map — insert / replace, claiming ownership.
            //
            // Direct-owner (empty `existingOwner`) always wins: a loader
            // rescan does NOT overwrite paths the daemon (or any other
            // untagged caller) registered via `registerProfile(path,
            // profile)`. Without this guard, the user's Settings-slider
            // Global value would be silently clobbered every time their
            // `Global.json` was rescanned, then re-claimed on the next
            // `publishActiveAnimationProfile`, producing a flap. Settings
            // is the live tuning surface; file-based profiles are for
            // paths Settings does NOT publish to. A loader entry
            // colliding with a direct-owned path is silently skipped.
            for (auto it = profiles.constBegin(); it != profiles.constEnd(); ++it) {
                const QString& path = it.key();
                const Profile& p = it.value();
                auto existing = m_profiles.find(path);
                const QString existingOwner = m_owners.value(path);
                if (existing != m_profiles.end() && existingOwner.isEmpty()) {
                    // Direct-owner has highest priority; loader steps aside.
                    continue;
                }
                if (existing == m_profiles.end() || !(*existing == p) || existingOwner != ownerTag) {
                    m_profiles.insert(path, p);
                    m_owners.insert(path, ownerTag);
                    pathsChanged.append(path);
                }
            }
        } // non-seed branch
    }

    if (pathsRemoved.isEmpty() && pathsChanged.isEmpty()) {
        return;
    }

    // Per-path signals — `profilesReloaded` is reserved for truly
    // wholesale ops (`clear`, `reloadAll`). Firing profilesReloaded
    // here would make every bound `PhosphorMotionAnimation` in the
    // process re-resolve TWICE on each targeted update (once from
    // per-path, once from the bulk signal), and a bulk signal
    // additionally wakes every bound animation regardless of which
    // path changed. Per-path covers every change a bound
    // PhosphorMotionAnimation needs to see.
    for (const QString& path : std::as_const(pathsRemoved)) {
        emitThreadSafe(this, [this, path] {
            Q_EMIT profileChanged(path);
        });
    }
    for (const QString& path : std::as_const(pathsChanged)) {
        emitThreadSafe(this, [this, path] {
            Q_EMIT profileChanged(path);
        });
    }
    emitThreadSafe(this, [this, ownerTag] {
        Q_EMIT ownerReloaded(ownerTag);
    });
}

void PhosphorProfileRegistry::clearOwner(const QString& ownerTag)
{
    // Same release-build hardening as reloadFromOwner above — the
    // Q_ASSERT alone compiles out in release, leaving a silent path
    // where an empty tag would loop the entire registry trying to match
    // entries with empty owner string, evicting every direct-owner
    // entry as collateral damage.
    Q_ASSERT_X(!ownerTag.isEmpty(), "PhosphorProfileRegistry::clearOwner",
               "ownerTag must be non-empty; use clear() for the test-only wholesale wipe");
    if (ownerTag.isEmpty()) {
        qWarning("PhosphorProfileRegistry::clearOwner: refusing empty ownerTag (use clear() for wholesale wipe)");
        return;
    }

    QStringList removed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Seeds are not in `m_owners`, so the loop below would find nothing
        // for the seed tag and clearing it would be a silent no-op.
        if (!m_lowPrecedenceOwnerTag.isEmpty() && ownerTag == m_lowPrecedenceOwnerTag) {
            removed = m_seedProfiles.keys();
            m_seedProfiles.clear();
        } else {
            for (auto it = m_owners.constBegin(); it != m_owners.constEnd(); ++it) {
                if (it.value() == ownerTag) {
                    removed.append(it.key());
                }
            }
            for (const QString& path : std::as_const(removed)) {
                m_profiles.remove(path);
                m_owners.remove(path);
            }
        }
    }
    if (removed.isEmpty()) {
        return;
    }
    // Per-path only — same shape as `reloadFromOwner`. `profilesReloaded`
    // is reserved for wholesale ops (`clear`, `reloadAll`) where the
    // registry cannot enumerate which paths changed.
    for (const QString& path : std::as_const(removed)) {
        emitThreadSafe(this, [this, path] {
            Q_EMIT profileChanged(path);
        });
    }
    emitThreadSafe(this, [this, ownerTag] {
        Q_EMIT ownerReloaded(ownerTag);
    });
}

void PhosphorProfileRegistry::reloadAll(const QHash<QString, Profile>& profiles)
{
    // Same value-changed guard as registerProfile — avoid a spurious
    // profilesReloaded emit when a caller replaces the whole set with
    // the byte-identical current content. Owners are all reset to the
    // direct/empty tag — this is intentional "wipe + replace" semantics.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // The seed store counts towards "is this already the whole content".
        // Without it a reloadAll could early-return as a no-op while seeds
        // were still resolving underneath.
        if (m_profiles == profiles && m_owners.isEmpty() && m_seedProfiles.isEmpty()) {
            return;
        }
        m_profiles = profiles;
        m_owners.clear();
        m_seedProfiles.clear();
    }
    emitThreadSafe(this, [this] {
        Q_EMIT profilesReloaded();
    });
}

void PhosphorProfileRegistry::clear()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_profiles.clear();
        m_owners.clear();
        m_seedProfiles.clear();
    }
    emitThreadSafe(this, [this] {
        Q_EMIT profilesReloaded();
    });
}

QString PhosphorProfileRegistry::ownerOf(const QString& path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // A non-seed entry answers with its own tag. A path that exists ONLY as
    // a seed still answers with the seed tag, so the accessor keeps meaning
    // what its name says even though seeds are no longer in `m_owners`.
    const auto it = m_owners.constFind(path);
    if (it != m_owners.constEnd()) {
        return *it;
    }
    if (!m_profiles.contains(path) && m_seedProfiles.contains(path)) {
        return m_lowPrecedenceOwnerTag;
    }
    return QString();
}

QHash<QString, Profile> PhosphorProfileRegistry::snapshot() const
{
    // Non-seed entries. Seeds are a resolution layer, not registered content,
    // and every consumer of this wants what was explicitly registered.
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profiles;
}

QHash<QString, Profile> PhosphorProfileRegistry::snapshotExcludingLowPrecedence() const
{
    // Identical to snapshot() by construction now that seeds live in their
    // own store: `m_profiles` never contains one. Both names are kept — this
    // one documents the intent at the D-Bus getter, which must not publish
    // seeds as if they were user overrides.
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profiles;
}

int PhosphorProfileRegistry::profileCount() const
{
    // Distinct PATHS across both layers, matching resolve()/hasProfile(): a
    // path carrying only a seed still resolves, so counting it as absent would
    // contradict them.
    std::lock_guard<std::mutex> lock(m_mutex);
    int count = m_profiles.size();
    for (auto it = m_seedProfiles.constBegin(); it != m_seedProfiles.constEnd(); ++it) {
        if (!m_profiles.contains(it.key())) {
            ++count;
        }
    }
    return count;
}

bool PhosphorProfileRegistry::hasProfile(const QString& path) const
{
    // Either layer — see resolve().
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profiles.contains(path) || m_seedProfiles.contains(path);
}

} // namespace PhosphorAnimation
