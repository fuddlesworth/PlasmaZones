// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "core/interfaces/ioverviewpolicy.h"
#include "plasmazones_export.h"

#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QObject>
#include <QPointer>
#include <QString>

class QDBusServiceWatcher;

namespace PlasmaZones {

class IOverviewPolicy;
class KwinSenderTrust;

/**
 * @brief D-Bus surface of the workspace overview (org.plasmazones.Overview).
 *
 * Unconditional: created with the other core adaptors so the interface is
 * always introspectable, and attached to an OverviewController only while
 * the workspaces feature is on. Without a controller every method is a
 * logged no-op and the replay answers empty.
 *
 * Open-state ownership: the unique bus name that called
 * @ref setOverviewOpen(true) owns the open state. A QDBusServiceWatcher on
 * that name resets to closed when it vanishes (an effect crash, a kwin
 * restart), so the daemon never streams to nobody. Only the owner may close.
 *
 * Every inbound verb authenticates the sender as kwin through the shared
 * KwinSenderTrust, the same check the overlay's thumbnail injection uses.
 * The model is a description of the user's windows and the verbs move them,
 * so a peer on the session bus must not be able to drive either.
 */
class PLASMAZONES_EXPORT OverviewAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.plasmazones.Overview")

public:
    explicit OverviewAdaptor(QObject* parent);
    ~OverviewAdaptor() override;

    /// Attach (feature on) or detach (nullptr, feature teardown) the policy
    /// object. Detaching closes the gate and emits closeOverviewRequested so
    /// an open effect lets go.
    void setController(IOverviewPolicy* controller);
    IOverviewPolicy* controller() const;

    /// Daemon-side relays (not D-Bus methods; public non-slot methods are
    /// not exported).
    void requestToggle();
    void requestClose();

    /// Test seam: whether the gate is open and who owns it.
    bool isOpen() const
    {
        return m_open;
    }
    QString ownerName() const
    {
        return m_owner;
    }

public Q_SLOTS:
    void setOverviewOpen(bool open);
    QString overviewModel() const;
    void reportOverviewState(bool open);
    // Verbs. Every screen id passes WorkspaceController::canonicalScreenId
    // on entry; every refusal is a debug log and no change.
    void focusWorkspace(const QString& screenId, const QString& desktopId);
    void moveWindowToWorkspace(const QString& windowId, const QString& screenId, const QString& desktopId, int dropX,
                               int dropY);
    void moveWindowToNewWorkspace(const QString& windowId, const QString& screenId, int sliceIndex, int dropX,
                                  int dropY);
    void reorderWorkspace(const QString& screenId, const QString& desktopId, int newSliceIndex);
    void moveWorkspaceToScreen(const QString& desktopId, const QString& targetScreenId, int sliceIndex);
    void renameWorkspace(const QString& desktopId, const QString& name);
    void pinWorkspace(const QString& desktopId, bool pinned);
    void panStrip(const QString& screenId, const QString& desktopId, int deltaPx);

Q_SIGNALS:
    void overviewModelChanged(const QString& modelJson);
    void overviewStateChanged(bool open);
    void toggleOverviewRequested();
    void closeOverviewRequested();

private:
    bool authenticate();
    /// The call context of the method being dispatched. Qt sets it on the
    /// adaptor's PARENT (the registered object), never on the adaptor, so a
    /// context inherited here would always read "not from D-Bus" and wave
    /// every sender through. Null when the parent carries none (a unit
    /// test's plain QObject host), which counts as a direct call.
    QDBusContext* dbusContext() const;
    /// The unique bus name of the current caller, empty for a direct call.
    QString callerName() const;
    /// The policy to route a verb to, or null (unauthenticated sender, or the
    /// feature is off), with the refusal logged.
    IOverviewPolicy* verbTarget(const char* verb);
    /// Apply an open-state change: gate, controller, watcher, signal.
    void applyOpen(bool open, const QString& owner);
    void onControllerModel(const QString& modelJson);

    QPointer<IOverviewPolicy> m_controller;
    KwinSenderTrust* m_kwinTrust = nullptr;
    bool m_open = false;
    QString m_owner;
    QDBusServiceWatcher* m_ownerWatcher = nullptr;
};

} // namespace PlasmaZones
