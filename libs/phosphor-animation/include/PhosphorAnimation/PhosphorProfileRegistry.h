// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/phosphoranimation_export.h>

#include <QtCore/QHash>
#include <QtCore/QObject>
#include <QtCore/QString>

#include <atomic>
#include <mutex>
#include <optional>

namespace PhosphorAnimation {

/// Registry mapping profile path strings to Profile values. Consumers
/// register profiles here; settings reloads call registerProfile() to
/// publish updates. Thread-safe (internal mutex).
class PHOSPHORANIMATION_EXPORT PhosphorProfileRegistry : public QObject
{
    Q_OBJECT

public:
    explicit PhosphorProfileRegistry(QObject* parent = nullptr);
    ~PhosphorProfileRegistry() override;

    PhosphorProfileRegistry(const PhosphorProfileRegistry&) = delete;
    PhosphorProfileRegistry& operator=(const PhosphorProfileRegistry&) = delete;

    /// Publish @p registry as the process-wide default for QML resolution.
    static void setDefaultRegistry(PhosphorProfileRegistry* registry);

    /// Read the process-wide default; nullptr if none published yet.
    static PhosphorProfileRegistry* defaultRegistry();

    /// Resolve @p path to a Profile if registered.
    std::optional<Profile> resolve(const QString& path) const;

    /// Register or replace the profile at @p path (direct/untagged owner).
    void registerProfile(const QString& path, const Profile& profile);

    /// Register or replace at @p path, stamped with an owner tag.
    void registerProfile(const QString& path, const Profile& profile, const QString& ownerTag);

    /// Remove @p path. Fires profileChanged if it existed.
    void unregisterProfile(const QString& path);

    /// Replace the subset owned by @p ownerTag with @p profiles.
    /// Entries owned by other tags are preserved.
    void reloadFromOwner(const QString& ownerTag, const QHash<QString, Profile>& profiles);

    /// Remove every entry owned by @p ownerTag.
    void clearOwner(const QString& ownerTag);

    /// Wholesale replace the entire registry. TEST-ONLY semantics.
    void reloadAll(const QHash<QString, Profile>& profiles);

    /// Clear the registry. Fires profilesReloaded().
    void clear();

    /// Current owner tag for @p path, or empty string.
    QString ownerOf(const QString& path) const;

    /// Current path count. Thread-safe.
    int profileCount() const;

    /// Is @p path registered? Thread-safe.
    bool hasProfile(const QString& path) const;

Q_SIGNALS:
    /// Fired when a profile is registered, replaced, or unregistered.
    void profileChanged(const QString& path);

    /// Fired only on wholesale operations (reloadAll, clear).
    void profilesReloaded();

    /// Fired at the end of a reloadFromOwner/clearOwner batch if any changes occurred.
    void ownerReloaded(const QString& ownerTag);

private:
    static std::atomic<PhosphorProfileRegistry*> s_defaultRegistry;

    mutable std::mutex m_mutex;
    QHash<QString, Profile> m_profiles;
    QHash<QString, QString> m_owners;
};

} // namespace PhosphorAnimation
