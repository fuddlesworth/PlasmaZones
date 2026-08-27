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
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QSettings>
#include <QStandardPaths>

#ifdef USE_KDE_FRAMEWORKS
#include <KConfig>
#include <KConfigGroup>
#endif

namespace PlasmaZones {

namespace {
/// Consent-gated kwinrc write (plan §7): turn PerOutputVirtualDesktops on and
/// reconfigure KWin. The setting applies live (verified against KWin 6.7
/// source); never called without the user's recorded consent, and never the
/// reverse direction (we do not revert on disable).
void enableKWinPerOutputDesktops()
{
#ifdef USE_KDE_FRAMEWORKS
    // KConfig, never QSettings: QSettings::sync() rewrites the WHOLE file in
    // its own ini dialect, mangling KConfig-only constructs elsewhere in
    // kwinrc (nested [group][subgroup] headers, [$e]/[$i] markers, localized
    // keys). KConfig writes just this key.
    KConfig kwinrc(QStringLiteral("kwinrc"));
    KConfigGroup windows(&kwinrc, QStringLiteral("Windows"));
    windows.writeEntry(QStringLiteral("PerOutputVirtualDesktops"), true);
    kwinrc.sync();
#else
    // Qt-only build: no KConfig available. The whole-file rewrite risk above
    // is accepted for this build flavor (it has no KDE session to protect).
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kwinrc");
    QSettings kwinrc(path, QSettings::IniFormat);
    kwinrc.setValue(QStringLiteral("Windows/PerOutputVirtualDesktops"), true);
    kwinrc.sync();
#endif
    QDBusConnection::sessionBus().asyncCall(
        QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"),
                                       QStringLiteral("org.kde.KWin"), QStringLiteral("reconfigure")));
    qCInfo(lcDaemon) << "wrote PerOutputVirtualDesktops=true to kwinrc and asked KWin to reconfigure (user consent)";
}

QString kwinShortcutBackupPath()
{
    // StateLocation is already app-scoped; see stateFilePath's relocation
    // note. The one-time move from the earlier nested path happens here too —
    // dropping this file would strand the user's stolen KWin chords on
    // "none" forever.
    const QString path =
        QStandardPaths::writableLocation(QStandardPaths::StateLocation) + QStringLiteral("/kwin-shortcut-backup.json");
    const QString legacyPath = QStandardPaths::writableLocation(QStandardPaths::StateLocation)
        + QStringLiteral("/plasmazones/kwin-shortcut-backup.json");
    if (!QFile::exists(path) && QFile::exists(legacyPath)) {
        QFile::rename(legacyPath, path);
    }
    return path;
}

/// Give KWin its stolen desktop-switch chords back (feature or rebind toggle
/// turned off). Consumes the backup file entry by entry — but ONLY entries
/// whose restore call succeeded: dropping an entry after a failed call
/// (kglobalaccel unreachable) would strand the user's chord on "none"
/// forever, while keeping it means the next restore attempt tries again.
void restoreKWinShortcutBackup(ShortcutManager* shortcutManager)
{
    QSettings backup(kwinShortcutBackupPath(), QSettings::IniFormat);
    const QStringList backedUp = backup.allKeys();
    for (const QString& action : backedUp) {
        // listFromString also reads the pre-multi-sequence single-string
        // format (a one-element list).
        const QList<QKeySequence> sequences = QKeySequence::listFromString(backup.value(action).toString());
        if (shortcutManager->setForeignShortcuts(QStringLiteral("kwin"), action, sequences)) {
            backup.remove(action);
        } else {
            qCWarning(lcDaemon) << "could not restore KWin shortcut" << action << "- keeping its backup entry";
        }
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
                // Runtime disable: sever EVERY connection this feature made
                // (m_workspaceWiring is the receiver context for all of them —
                // shortcut verbs, settings reactions, the adaptor probe), drop
                // the adhoc named binds whose lambdas capture the controller,
                // then tear the controller down (its dtor writes the final
                // state file) and hand KWin its chords back. The kwinrc write
                // is deliberately NOT reverted (stated in the UI).
                m_workspaceWiring.reset();
                for (const QString& id : std::as_const(m_workspaceNamedShortcutIds)) {
                    m_shortcutManager->unregisterAdhocShortcut(id);
                }
                m_workspaceNamedShortcutIds.clear();
                m_workspaceController.reset();
                if (m_windowTrackingAdaptor) {
                    m_windowTrackingAdaptor->setWorkspaceRouteResolver(nullptr);
                }
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
    // Every connection below hangs off this context object so a runtime
    // disable can sever them all at once (see the rearm lambda above). The
    // daemon (`this`) stays captured in the lambdas — it outlives the wiring.
    m_workspaceWiring = std::make_unique<QObject>();
    QObject* const wiring = m_workspaceWiring.get();

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
    connect(m_workspaceController.get(), &WorkspaceController::desktopReapRequested, wiring,
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
    connect(m_workspaceController.get(), &WorkspaceController::desktopRenumberRequested, wiring,
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
    connect(m_workspaceController.get(), &WorkspaceController::workspaceMapPublished, wiring,
            [this](const QString& mapJson) {
                if (m_windowTrackingAdaptor) {
                    m_windowTrackingAdaptor->setWorkspaceMapPayload(mapJson);
                }
            });

    // ── Verb execution channels ────────────────────────────────────────────
    // Per-screen switch: reconciler-ledgered SetCurrent → effect command
    // (effects->setCurrentDesktop(desktop, output)); the answering
    // desktopChanged report retires the ledger entry.
    connect(m_workspaceController.get(), &WorkspaceController::screenDesktopSwitchRequested, wiring,
            [this](const QString& screenId, int desktop) {
                if (m_windowTrackingAdaptor) {
                    Q_EMIT m_windowTrackingAdaptor->setScreenDesktopRequested(screenId, desktop);
                }
            });
    // Window relocation: the same handoff machinery the cross-mode
    // directional moves use, same-engine allowed (plan §4.2 reuse).
    connect(
        m_workspaceController.get(), &WorkspaceController::windowWorkspaceMoveRequested, wiring,
        [this](const QString& windowId, const QString& targetScreenId, int targetDesktop, const QString& direction) {
            if (m_windowTrackingAdaptor) {
                m_windowTrackingAdaptor->moveWindowToWorkspaceVerb(windowId, targetScreenId, targetDesktop, direction);
            }
        });
    // Owner-wins snap-back hint (plain prose; toggleable).
    connect(m_workspaceController.get(), &WorkspaceController::snapBackOccurred, wiring,
            [this](const QString& screenId) {
                if (m_settings && m_settings->workspacesSnapBackOsdHint() && m_overlayService) {
                    m_overlayService->showDisabledOsd(
                        PhosphorI18n::tr("That workspace is on another monitor.", "OSD hint"), screenId);
                }
            });
    // A window mapped onto a workspace during its removal window and KWin
    // swept it to an arbitrary neighbour; the controller re-issued a move to
    // the owner's current workspace (plan §4.3 destroy step 4) — hint it.
    connect(m_workspaceController.get(), &WorkspaceController::windowDisplacedByRemoval, wiring,
            [this](const QString& screenId) {
                if (m_settings && m_settings->workspacesSnapBackOsdHint() && m_overlayService) {
                    m_overlayService->showDisabledOsd(
                        PhosphorI18n::tr("That workspace closed. The window moved to the current one.", "OSD hint"),
                        screenId);
                }
            });
    // Focused-screen tracking (fork 3): externally created desktops adopt to
    // the screen the user is working on, resolved from the effect's activation
    // reports (already in the effect id space; extract the physical output).
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::windowActivated, wiring,
            [this](const QString& windowId, const QString& screenId) {
                Q_UNUSED(windowId)
                if (!screenId.isEmpty()) {
                    m_workspaceController->reconciler().setFocusedScreen(
                        PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
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
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceFocusRequested, wiring,
            [this, actingScreen](int delta) {
                m_workspaceController->focusWorkspace(actingScreen(), delta);
            });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceMoveWindowRequested, wiring,
            [this, actingScreen](int delta) {
                const QString windowId =
                    m_windowTrackingAdaptor ? m_windowTrackingAdaptor->lastActiveWindowId() : QString();
                m_workspaceController->moveWindowToWorkspace(actingScreen(), windowId, delta);
            });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceMoveColumnRequested, wiring,
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
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceReorderRequested, wiring,
            [this, actingScreen](int delta) {
                m_workspaceController->moveWorkspace(actingScreen(), delta);
            });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceMoveToMonitorRequested, wiring,
            [this, actingScreen](const QString& direction) {
                m_workspaceController->moveWorkspaceToOutput(actingScreen(), direction);
            });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceMoveSlotRequested, wiring,
            [this, actingScreen](int slot) {
                const QString windowId =
                    m_windowTrackingAdaptor ? m_windowTrackingAdaptor->lastActiveWindowId() : QString();
                m_workspaceController->moveWindowToWorkspaceAt(actingScreen(), windowId, slot - 1);
            });
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceFocusSlotRequested, wiring,
            [this, actingScreen](int slot) {
                m_workspaceController->focusWorkspaceAt(actingScreen(), slot - 1);
            });

    // ── Named workspaces ───────────────────────────────────────────────────
    // Declarations flow from config; per-name focus/move chords bind as
    // adhoc (transient) shortcuts re-bound on every declaration change, the
    // quick-layout-slot pattern. The bound-id list is a daemon member so the
    // runtime-disable teardown can unregister them too.
    const auto rebindNamedShortcuts = [this]() {
        for (const QString& id : std::as_const(m_workspaceNamedShortcutIds)) {
            m_shortcutManager->unregisterAdhocShortcut(id);
        }
        m_workspaceNamedShortcutIds.clear();
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
                m_workspaceNamedShortcutIds.append(id);
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
                m_workspaceNamedShortcutIds.append(id);
            }
        }
    };
    m_workspaceController->applyNamedDeclarations(m_settings->workspacesNamedEntries());
    rebindNamedShortcuts();
    connect(m_settings.get(), &Settings::workspacesNamedEntriesChanged, wiring, [this, rebindNamedShortcuts]() {
        m_workspaceController->applyNamedDeclarations(m_settings->workspacesNamedEntries());
        rebindNamedShortcuts();
    });

    // Authoritative gate arm: the effect probes the running compositor's mode
    // at bringup. A divergence from the kwinrc read that admitted us here
    // means the mode never actually applied (a missed reconfigure, or the
    // file changed back). With the user's consent latch on, self-heal by
    // re-issuing the consent write + reconfigure; without it, warn — the
    // controller keeps running and KWin's next reconfigure resolves it.
    connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::perOutputDesktopsModeReported, wiring,
            [this](bool enabled) {
                if (enabled) {
                    return;
                }
                if (m_settings->workspacesManageKWinPerOutput()) {
                    qCWarning(lcDaemon) << "compositor reports per-output virtual desktops OFF while dynamic "
                                           "workspaces are active; re-applying the consented kwinrc write";
                    enableKWinPerOutputDesktops();
                } else {
                    qCWarning(lcDaemon) << "compositor reports per-output virtual desktops OFF while dynamic "
                                           "workspaces are active; kwinrc and the running KWin disagree "
                                           "(missing reconfigure?)";
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
        QStringLiteral("Switch One Desktop Up"),          QStringLiteral("Switch One Desktop Down"),
        QStringLiteral("Switch One Desktop to the Left"), QStringLiteral("Switch One Desktop to the Right"),
        QStringLiteral("Walk Through Desktops"),          QStringLiteral("Walk Through Desktops (Reverse)")};
    const auto applyStockRebind = [this]() {
        if (!m_settings->workspacesRebindKWinShortcuts()) {
            restoreKWinShortcutBackup(m_shortcutManager.get());
            return;
        }
        QDir().mkpath(QFileInfo(kwinShortcutBackupPath()).absolutePath());
        QSettings backup(kwinShortcutBackupPath(), QSettings::IniFormat);
        for (const QString& action : kKWinDesktopActions) {
            if (!backup.contains(action)) {
                const QList<QKeySequence> current = m_shortcutManager->foreignShortcuts(QStringLiteral("kwin"), action);
                if (current.isEmpty()) {
                    continue; // already unbound; nothing to steal or restore
                }
                if (m_shortcutManager->setForeignShortcuts(QStringLiteral("kwin"), action, {})) {
                    // The FULL binding (primary + alternates) round-trips.
                    backup.setValue(action, QKeySequence::listToString(current, QKeySequence::PortableText));
                }
            }
        }
        backup.sync();
    };
    applyStockRebind();
    connect(m_settings.get(), &Settings::workspacesRebindKWinShortcutsChanged, wiring, applyStockRebind);

    // Desktop-cap degradation hint (once per episode, reconciler-gated).
    connect(
        &m_workspaceController->reconciler(), &PhosphorWorkspaces::WorkspaceReconciler::capReached, wiring, [this]() {
            if (m_overlayService) {
                m_overlayService->showDisabledOsd(PhosphorI18n::tr("Workspace limit reached.", "OSD hint"), QString());
            }
        });

    // RouteToWorkspace rule resolver: the rules pipeline calls this on the
    // open path (rules_placement.cpp). True = routed (positional
    // RouteToDesktop is skipped); false = name unrealized, fall through.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setWorkspaceRouteResolver([this](const QString& name, const QString& windowId) {
            // Returns true only when the move was ISSUED (name realized,
            // desktop resolvable, no structural churn deferring the verb) —
            // a true suppresses the positional RouteToDesktop fallback, so a
            // deferred-then-failed move must instead return false here and
            // let the number route the window.
            return m_workspaceController && m_workspaceController->routeWindowToNamedWorkspace(name, windowId);
        });
    }

    // Window → screen resolver for the owner-wins reunion arm: an engine's
    // live tracking wins; the placement store's last managed-context screen
    // covers untracked (floating) windows that still carry a record. Empty
    // means "cannot vouch" and the controller skips the check.
    m_workspaceController->setWindowScreenResolver([this](const QString& windowId) -> QString {
        for (PhosphorEngine::PlacementEngineBase* engine :
             {m_scrollEngine.get(), m_autotileEngine.get(), m_snapEngine.get()}) {
            if (engine && engine->isWindowTracked(windowId)) {
                return engine->screenForTrackedWindow(windowId);
            }
        }
        if (m_windowTrackingAdaptor) {
            if (auto* service = m_windowTrackingAdaptor->service()) {
                if (const auto record = service->placementStore().peekExact(windowId)) {
                    return record->screenId;
                }
            }
        }
        return QString();
    });

    m_workspaceController->start();
    qCInfo(lcDaemon) << "dynamic workspaces active";
}

} // namespace PlasmaZones
