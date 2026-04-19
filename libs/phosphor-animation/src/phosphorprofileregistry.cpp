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
    // Only emit when the stored value actually changes. Without this
    // guard, a consumer that re-registers on every settings tick (the
    // daemon's publishActiveAnimationProfile fan-out) would produce a
    // storm of profileChanged signals for every bound animation, each
    // forcing a re-resolve even when nothing semantically changed.
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_profiles.find(path);
        if (it == m_profiles.end() || !(*it == profile)) {
            m_profiles.insert(path, profile);
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
    }
    if (existed) {
        Q_EMIT profileChanged(path);
    }
}

void PhosphorProfileRegistry::reloadAll(const QHash<QString, Profile>& profiles)
{
    // Same value-changed guard as registerProfile — avoid a spurious
    // profilesReloaded emit when the loader rescans and finds the
    // on-disk set byte-identical to what's already registered.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_profiles == profiles) {
            return;
        }
        m_profiles = profiles;
    }
    Q_EMIT profilesReloaded();
}

void PhosphorProfileRegistry::clear()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_profiles.clear();
    }
    Q_EMIT profilesReloaded();
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
