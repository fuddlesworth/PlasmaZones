// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "plasmazones_export.h"

#include <QDBusConnection>
#include <QObject>
#include <QSet>
#include <QString>

class QDBusServiceWatcher;

namespace PlasmaZones {

/**
 * @brief Session-bus sender authentication bound to the running kwin.
 *
 * Several daemon interfaces accept calls that are UI primitives in the
 * wrong hands (thumbnail injection on the overlay interface, the overview's
 * window-move verbs). They bind the caller to @c kwin_wayland by resolving
 * the sender's unique bus name to a PID through @c GetConnectionUnixProcessID
 * and checking the basename of @c /proc/<pid>/exe against the accepted set.
 * The @c /proc/<pid>/exe symlink is kernel-maintained and cannot be rewritten
 * from userspace, unlike @c /proc/<pid>/comm, which @c prctl(PR_SET_NAME) can
 * spoof.
 *
 * Trust is pre-warmed asynchronously against the owner of @c org.kde.KWin at
 * construction and again on every kwin re-registration, so the steady-state
 * cost of a check is one set lookup. Trusted unique names are evicted by a
 * per-name @c QDBusServiceWatcher when their owner unregisters, so a PID reuse
 * after kwin exits cannot inherit trust. The synchronous fallback in
 * @ref isTrustedSender exists only for a call that races a fresh pre-warm.
 *
 * Shared by every adaptor that authenticates kwin, so the accepted binary
 * set and the eviction rule live in exactly one place.
 */
class PLASMAZONES_EXPORT KwinSenderTrust : public QObject
{
    Q_OBJECT

public:
    explicit KwinSenderTrust(QObject* parent = nullptr);

    /**
     * @brief Whether @p uniqueName is a verified kwin sender.
     *
     * Cache hit first. On a miss, a bounded synchronous
     * @c GetConnectionUnixProcessID round-trip over @p bus resolves the PID and
     * @ref validateExeAndTrust admits or rejects. An empty @p uniqueName is
     * accepted: it means the call did not come over the bus (a unit test
     * invoking the slot directly), so there is no remote peer to authorise.
     */
    bool isTrustedSender(const QString& uniqueName, const QDBusConnection& bus);

    /// Test seam: whether @p uniqueName is in the trust cache.
    bool contains(const QString& uniqueName) const
    {
        return m_trusted.contains(uniqueName);
    }

private:
    void prewarmKwinTrust();
    void resolvePidAndTrust(const QString& uniqueName);
    bool validateExeAndTrust(const QString& uniqueName, uint pid);

    QSet<QString> m_trusted;
    QDBusServiceWatcher* m_kwinWatcher = nullptr;
};

} // namespace PlasmaZones
