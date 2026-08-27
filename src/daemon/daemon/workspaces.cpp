// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// ═══════════════════════════════════════════════════════════════════════════════
// Daemon — dynamic per-monitor workspaces wiring
//
// Constructs the WorkspaceController behind the feature gate (setting +
// KWin per-output mode) and fans its identity-based reap/renumber out to all
// three placement engines and the unified placement store. The controller
// owns the model and the census; this file owns only what needs daemon
// members: the engine loop, the store transform, and the D-Bus stream relay.
// ═══════════════════════════════════════════════════════════════════════════════

#include "daemon/daemon.h"

#include "config/settings.h"
#include "core/platform/logging.h"
#include "daemon/controllers/shortcutmanager.h"
#include "daemon/controllers/workspacecontroller.h"
#include "daemon/daemon/helpers.h"
#include "daemon/overlayservice.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "phosphor_i18n.h"

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QKeySequence>
#include <QSettings>
#include <QStandardPaths>

namespace PlasmaZones {

namespace {
/// Consent-gated kwinrc write (plan §7): turn PerOutputVirtualDesktops on and
/// reconfigure KWin. The setting applies live (verified against KWin 6.7
/// source); never called without the user's recorded consent, and never the
/// reverse direction (we do not revert on disable).
void enableKWinPerOutputDesktops()
{
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc");
    QSettings kwinrc(path, QSettings::IniFormat);
    kwinrc.setValue(QStringLiteral("Windows/PerOutputVirtualDesktops"), true);
    kwinrc.sync();
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                       QStringLiteral("org.kde.KWin"), QStringLiteral("reconfigure")));
    qCInfo(lcDaemon) << "wrote PerOutputVirtualDesktops=true to kwinrc and asked KWin to reconfigure (user consent)";
}

QString kwinShortcutBackupPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation)
        + QStringLiteral("/plasmazones/kwin-shortcut-backup.json");
}

/// Give KWin its stolen desktop-switch chords back (feature or rebind toggle
/// turned off). Consumes the backup file entry by entry.
void restoreKWinShortcutBackup(ShortcutManager* shortcutManager)
{
    QSettings backup(kwinShortcutBackupPath(), QSettings::IniFormat);
    const QStringList backedUp = backup.allKeys();
    for (const QString& action : backedUp) {
        shortcutManager->setForeignShortcut(QStringLiteral("kwin"), action,
                                            QKeySequence(backup.value(action).toString()));
        backup.remove(action);
    }
    backup.sync();
}
} // namespace

void Daemon::initializeWorkspaces()
{
    if (!m_settings) {
        return;
    }
    // Enabling the feature (or granting consent) at runtime re-enters this
    // gate without a daemon restart.
    if (!m_workspaceRearmConnected) {
        m_workspaceRearmConnected = true;
        const auto rearm = [this]() {
            const bool wantOn = m_settings->workspacesEnabled();
            if (wantOn && !m_workspaceController) {
                initializeWorkspaces();
            } else if (!wantOn && m_workspaceController) {
                // Runtime disable: tear the controller down (its dtor writes
                // the final state file; every connection dies with it) and
                // hand KWin its chords back. The kwinrc write is deliberately
                // NOT reverted (stated in the UI).
                m_workspaceController.reset();
                restoreKWinShortcutBackup(m_shortcutManager.get());
                qCInfo(lcDaemon) << "dynamic workspaces disabled at runtime";
            }
        };
        connect(m_settings.get(), &Settings::workspacesEnabledChanged, this, rearm);
        connect(m_settings.get(), &Settings::workspacesManageKWinPerOutputChanged, this, rearm);
    }
    if (!m_settings->workspacesEnabled()) {
        return;
    }
    if (!WorkspaceController::kwinPerOutputEnabled()) {
        if (m_settings->workspacesManageKWinPerOutput()) {
            enableKWinPerOutputDesktops();
        } else {
            qCWarning(lcDaemon) << "dynamic workspaces enabled but KWin per-output virtual desktops is off and no "
                                   "consent to manage it; feature stays dormant";
            return;
        }
    }
    if (!m_virtualDesktopManager || !m_windowRegistry || !m_screenManager) {
        return;
    }

    m_workspaceController = std::make_unique<WorkspaceController>(m_virtualDesktopManager.get(), m_windowRegistry.get(),
                                                                  m_screenManager.get());

    // Identity-based engine fan-out: reap the removed desktops, then shift the
    // survivors — in every engine, whichever mode its screens run.
    const auto forEachEngine = [this](const std::function<void(PhosphorEngine::PlacementEngineBase*)>& fn) {
        for (PhosphorEngine::PlacementEngineBase* engine :
             {m_autotileEngine.get(), m_snapEngine.get(), m_scrollEngine.get()}) {
            if (engine) {
                fn(engine);
            }
        }
    };
    connect(m_workspaceController.get(), &WorkspaceController::desktopReapRequested, this,
            [this, forEachEngine](int desktop) {
                forEachEngine([desktop](PhosphorEngine::PlacementEngineBase* engine) {
                    engine->reapDesktopState(desktop);
                });
                // The unified placement store's per-record desktop tag: a
                // record on the dead desktop degrades to 0 (unknown) and the
                // effect's next report re-stamps it; KWin relocates the real
                // window either way.
                if (m_windowTrackingAdaptor) {
                    if (auto* service = m_windowTrackingAdaptor->service()) {
                        service->placementStore().transform([desktop](PhosphorEngine::WindowPlacement& record) {
                            if (record.virtualDesktop == desktop) {
                                record.virtualDesktop = 0;
                                return true;
                            }
                            return false;
                        });
                    }
                }
            });
    connect(m_workspaceController.get(), &WorkspaceController::desktopRenumberRequested, this,
            [this, forEachEngine](const QHash<int, int>& oldToNew) {
                forEachEngine([&oldToNew](PhosphorEngine::PlacementEngineBase* engine) {
                    engine->renumberDesktopState(oldToNew);
                });
                if (m_windowTrackingAdaptor) {
                    if (auto* service = m_windowTrackingAdaptor->service()) {
                        service->placementStore().transform([&oldToNew](PhosphorEngine::WindowPlacement& record) {
                            if (record.virtualDesktop <= 0) {
                                return false; // 0 = sticky/unknown sentinel
                            }
                            const int mapped = oldToNew.value(record.virtualDesktop, record.virtualDesktop);
                            if (mapped == record.virtualDesktop) {
                                return false;
                            }
                            record.virtualDesktop = mapped;
                            return true;
                        });
                    }
                }
            });

    // Change-gated stream to the effect (replay handled by the adaptor query).
    connect(m_workspaceController.get(), &WorkspaceController::workspaceMapPublished, this,
            [this](const QString& mapJson) {
                if (m_windowTrackingAdaptor) {
                    m_windowTrackingAdaptor->setWorkspaceMapPayload(mapJson);
                }
            });

    // ── Verb execution channels ────────────────────────────────────────────
    // Per-screen switch: reconciler-ledgered SetCurrent → effect command
    // (effects->setCurrentDesktop(desktop, output)); the answering
    // desktopChanged report retires the ledger entry.
    connect(m_workspaceController.get(), &WorkspaceController::screenDesktopSwitchRequested, this,
            [this](const QString& screenId, int desktop) {
                if (m_windowTrackingAdaptor) {
                    Q_EMIT m_windowTrackingAdaptor->setScreenDesktopRequested(screenId, desktop);
                }
            });
    // Window relocation: the same handoff machinery the cross-mode
    // directional moves use, same-engine allowed (plan §4.2 reuse).
    connect(
        m_workspaceController.get(), &WorkspaceController::windowWorkspaceMoveRequested, this,
        [this](const QString& windowId, const QString& targetScreenId, int targetDesktop, const QString& direction) {
            if (m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->moveWindowToWorkspaceVerb(windowId, targetScreenId, targetDesktop, direction);
            }
        });
    // Owner-wins snap-back hint (plain prose; toggleable).
    connect(m_workspaceController.get(), &WorkspaceController::snapBackOccurred, this, [this](const QString& screenId) {
        if (m_settings && m_settings->workspacesSnapBackOsdHint() && m_overlayService) {
            m_overlayService->showDisabledOsd(PhosphorI18n::tr("That workspace is on another monitor.", "OSD hint"),
                                              screenId);
        }
    });

    // ── Shortcut verbs ─────────────────────────────────────────────────────
    // The acting screen is the shortcut screen's PHYSICAL output (the map and
    // the per-output desktops both key physical ids); the acting window is
    // the daemon's tracked active window.
    const auto actingScreen = [this]() {
        return PhosphorIdentity::VirtualScreenId::extractPhysicalId(
            resolveShortcutScreenId(m_screenManager.get(), m_windowTrackingAdaptor));
    };
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceFocusRequested, this, [this, actingScreen](int delta) {
        m_workspaceController->focusWorkspace(actingScreen(), delta);
    });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceMoveWindowRequested, this,
            [this, actingScreen](int delta) {
                const QString windowId =
                    m_windowTrackingAdaptor ? m_windowTrackingAdaptor->lastActiveWindowId() : QString();
                m_workspaceController->moveWindowToWorkspace(actingScreen(), windowId, delta);
            });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceMoveColumnRequested, this,
            [this, actingScreen](int delta) {
                const QString screenId = actingScreen();
                auto* scroll = qobject_cast<PhosphorScrollEngine::ScrollEngine*>(m_scrollEngine.get());
                const QStringList column = scroll ? scroll->focusedColumnWindows(screenId) : QStringList();
                if (column.isEmpty()) {
                    // Not a scrolling screen (or empty strip): the verb is
                    // scrolling-scoped by definition; hint instead of no-op.
                    if (m_overlayService) {
                        m_overlayService->showDisabledOsd(
                            PhosphorI18n::tr("Moving a column needs a scrolling screen.", "OSD hint"), screenId);
                    }
                    return;
                }
                m_workspaceController->moveColumnToWorkspace(screenId, column, delta);
            });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceReorderRequested, this,
            [this, actingScreen](int delta) {
                m_workspaceController->moveWorkspace(actingScreen(), delta);
            });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceMoveToMonitorRequested, this,
            [this, actingScreen](const QString& direction) {
                m_workspaceController->moveWorkspaceToOutput(actingScreen(), direction);
            });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceFocusSlotRequested, this,
            [this, actingScreen](int slot) {
                m_workspaceController->focusWorkspaceAt(actingScreen(), slot - 1);
            });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceMoveSlotRequested, this,
            [this, actingScreen](int slot) {
                const QString windowId =
                    m_windowTrackingAdaptor ? m_windowTrackingAdaptor->lastActiveWindowId() : QString();
                m_workspaceController->moveWindowToWorkspaceAt(actingScreen(), windowId, slot - 1);
            });

    // ── Named workspaces ───────────────────────────────────────────────────
    // Declarations flow from config; per-name focus/move chords bind as
    // adhoc (transient) shortcuts re-bound on every declaration change, the
    // quick-layout-slot pattern. The shared_ptr carries the currently-bound
    // id set between rebinds without a daemon member.
    const auto boundNamedIds = std::make_shared<QStringList>();
    const auto rebindNamedShortcuts = [this, boundNamedIds]() {
        for (const QString& id : std::as_const(*boundNamedIds)) {
            m_shortcutManager->unregisterAdhocShortcut(id);
        }
        boundNamedIds->clear();
        const QVariantList entries = m_settings->workspacesNamedEntries();
        for (const QVariant& value : entries) {
            const QVariantMap entry = value.toMap();
            const QString name = entry.value(QStringLiteral("name")).toString().trimmed();
            if (name.isEmpty()) {
                continue;
            }
            const QString focusChord = entry.value(QStringLiteral("focusShortcut")).toString();
            if (!focusChord.isEmpty()) {
                const QString id = QStringLiteral("workspace_named_focus:") + name;
                m_shortcutManager->registerAdhocShortcut(
                    id, QKeySequence(focusChord),
                    PhosphorI18n::tr("Focus Workspace \"%1\"", "named workspace shortcut").arg(name), [this, name]() {
                        m_workspaceController->focusNamedWorkspace(name);
                    });
                boundNamedIds->append(id);
            }
            const QString moveChord = entry.value(QStringLiteral("moveShortcut")).toString();
            if (!moveChord.isEmpty()) {
                const QString id = QStringLiteral("workspace_named_move:") + name;
                m_shortcutManager->registerAdhocShortcut(
                    id, QKeySequence(moveChord),
                    PhosphorI18n::tr("Move Window to Workspace \"%1\"", "named workspace shortcut").arg(name),
                    [this, name]() {
                        const QString windowId =
                            m_windowTrackingAdaptor ? m_windowTrackingAdaptor->lastActiveWindowId() : QString();
                        m_workspaceController->moveWindowToNamedWorkspace(name, windowId);
                    });
                boundNamedIds->append(id);
            }
        }
    };
    m_workspaceController->applyNamedDeclarations(m_settings->workspacesNamedEntries());
    rebindNamedShortcuts();
    connect(m_settings.get(), &Settings::workspacesNamedEntriesChanged, this, [this, rebindNamedShortcuts]() {
        m_workspaceController->applyNamedDeclarations(m_settings->workspacesNamedEntries());
        rebindNamedShortcuts();
    });

    // Authoritative gate arm: the effect probes the running compositor's mode
    // at bringup. A divergence from the kwinrc read that admitted us here
    // means the file changed without a reconfigure — surface it loudly; the
    // controller keeps running (KWin's next reconfigure resolves it).
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::perOutputDesktopsModeReported, this, [](bool enabled) {
        if (!enabled) {
            qCWarning(lcDaemon) << "compositor reports per-output virtual desktops OFF while dynamic workspaces "
                                   "are active; kwinrc and the running KWin disagree (missing reconfigure?)";
        }
    });

    // ── Stock KWin desktop-shortcut takeover (Phase 5) ─────────────────────
    // The stock "Switch One Desktop" quad iterates the GLOBAL pool, tripping
    // owner-wins snap-back on nearly every press, and its Down/Up chords are
    // the focus verbs' defaults. With the rebind toggle on, back each chord
    // up (state dir, once) and clear it via the KGlobalAccel foreign-rebind
    // pass-through; restore on toggle-off. If the backend cannot rebind
    // (portal), nothing is stolen and snap-back-with-hint stays the story.
    static const QStringList kKWinDesktopActions{
        QStringLiteral("Switch One Desktop Up"), QStringLiteral("Switch One Desktop Down"),
        QStringLiteral("Switch One Desktop to the Left"), QStringLiteral("Switch One Desktop to the Right")};
    const auto applyStockRebind = [this]() {
        if (!m_settings->workspacesRebindKWinShortcuts()) {
            restoreKWinShortcutBackup(m_shortcutManager.get());
            return;
        }
        QDir().mkpath(QFileInfo(kwinShortcutBackupPath()).absolutePath());
        QSettings backup(kwinShortcutBackupPath(), QSettings::IniFormat);
        for (const QString& action : kKWinDesktopActions) {
            if (!backup.contains(action)) {
                const QKeySequence current = m_shortcutManager->foreignShortcut(QStringLiteral("kwin"), action);
                if (current.isEmpty()) {
                    continue; // already unbound; nothing to steal or restore
                }
                if (m_shortcutManager->setForeignShortcut(QStringLiteral("kwin"), action, QKeySequence())) {
                    backup.setValue(action, current.toString(QKeySequence::PortableText));
                }
            }
        }
        backup.sync();
    };
    applyStockRebind();
    connect(m_settings.get(), &Settings::workspacesRebindKWinShortcutsChanged, this, applyStockRebind);

    // Desktop-cap degradation hint (once per episode, reconciler-gated).
    connect(&m_workspaceController->reconciler(), &PhosphorWorkspaces::WorkspaceReconciler::capReached, this, [this]() {
        if (m_overlayService) {
            m_overlayService->showDisabledOsd(PhosphorI18n::tr("Workspace limit reached.", "OSD hint"), QString());
        }
    });

    m_workspaceController->start();
    qCInfo(lcDaemon) << "dynamic workspaces active";
}

} // namespace PlasmaZones
