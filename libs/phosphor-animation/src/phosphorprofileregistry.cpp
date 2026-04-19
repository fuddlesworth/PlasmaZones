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
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_profiles.insert(path, profile);
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
    {
        std::lock_guard<std::mutex> lock(m_mutex);
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
