// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

namespace PhosphorAnimation {

PhosphorProfileRegistry::PhosphorProfileRegistry() = default;

PhosphorProfileRegistry::~PhosphorProfileRegistry() = default;

PhosphorProfileRegistry& PhosphorProfileRegistry::instance()
{
    static PhosphorProfileRegistry sInstance;
    return sInstance;
}

std::optional<Profile> PhosphorProfileRegistry::resolve(const QString& path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_profiles.constFind(path);
    if (it == m_profiles.constEnd()) {
        return std::nullopt;
    }
    return *it;
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
    if (!changed) {
        return;
    }
    // Emit outside the lock so slot handlers that re-enter (e.g. a
    // consumer's onProfileChanged calling resolve for another path)
    // do not self-deadlock.
    Q_EMIT profileChanged(path);
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
        Q_EMIT profileChanged(path);
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
        for (auto it = profiles.constBegin(); it != profiles.constEnd(); ++it) {
            const QString& path = it.key();
            const Profile& p = it.value();
            auto existing = m_profiles.find(path);
            const QString existingOwner = m_owners.value(path);
            if (existing == m_profiles.end() || !(*existing == p) || existingOwner != ownerTag) {
                m_profiles.insert(path, p);
                m_owners.insert(path, ownerTag);
                pathsChanged.append(path);
            }
        }
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
        Q_EMIT profileChanged(path);
    }
    for (const QString& path : std::as_const(pathsChanged)) {
        Q_EMIT profileChanged(path);
    }
    // Batch-boundary signal for consumers that prefer to coalesce UI
    // updates across a rescan (settings list views rendering tens of
    // paths). Fires exactly once AFTER the per-path storm, only when
    // the call produced changes — the early-return above covers the
    // no-op case.
    Q_EMIT ownerReloaded(ownerTag);
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
    if (removed.isEmpty()) {
        return;
    }
    // Per-path only — same shape as `reloadFromOwner`. `profilesReloaded`
    // is reserved for wholesale ops (`clear`, `reloadAll`) where the
    // registry cannot enumerate which paths changed.
    for (const QString& path : std::as_const(removed)) {
        Q_EMIT profileChanged(path);
    }
    // Match reloadFromOwner's batch-boundary shape so a consumer
    // listening to `ownerReloaded(tag)` also sees the clear-owner
    // case as a batch boundary.
    Q_EMIT ownerReloaded(ownerTag);
}

void PhosphorProfileRegistry::reloadAll(const QHash<QString, Profile>& profiles)
{
    // Same value-changed guard as registerProfile — avoid a spurious
    // profilesReloaded emit when a caller replaces the whole set with
    // the byte-identical current content. Owners are all reset to the
    // direct/empty tag — this is intentional "wipe + replace" semantics.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_profiles == profiles && m_owners.isEmpty()) {
            return;
        }
        m_profiles = profiles;
        m_owners.clear();
    }
    Q_EMIT profilesReloaded();
}

void PhosphorProfileRegistry::clear()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_profiles.clear();
        m_owners.clear();
    }
    Q_EMIT profilesReloaded();
}

QString PhosphorProfileRegistry::ownerOf(const QString& path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_owners.value(path);
}

int PhosphorProfileRegistry::profileCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profiles.size();
}

bool PhosphorProfileRegistry::hasProfile(const QString& path) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_profiles.contains(path);
}

} // namespace PhosphorAnimation
