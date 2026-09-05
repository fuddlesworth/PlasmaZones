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

#include "config/configdefaults.h"
#include "config/settings.h"
#include "core/platform/logging.h"
#include "daemon/controllers/shortcutmanager.h"
#include "daemon/controllers/shortcutmanager_ids.h"
#include "daemon/controllers/workspacecontroller.h"
#include "daemon/daemon/helpers.h"
#include "daemon/overlayservice.h"
#include "dbus/windowdragadaptor/windowdragadaptor.h"
#include "dbus/windowtrackingadaptor/windowtrackingadaptor.h"

#include "phosphor_i18n.h"

#include <PhosphorEngine/PlacementEngineBase.h>
#include <PhosphorEngine/WindowPlacement.h>
#include <PhosphorEngine/WindowRegistry.h>
#include <PhosphorIdentity/VirtualScreenId.h>
#include <PhosphorPlacement/WindowTrackingService.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorZones/AssignmentEntry.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/WorkspaceReconciler.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QScopeGuard>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringView>
#include <QVector>

#include <algorithm>
#include <optional>

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

/// QKeySequence(QString) answers an empty sequence on anything it cannot
/// parse, so a typo in a named workspace's chord would bind nothing and say
/// nothing. Registering the empty sequence is worse than skipping it (the
/// adhoc id would exist with no key), so this returns empty and the caller
/// leaves the binding out entirely.
QKeySequence parseNamedChord(const QString& raw, const QString& workspaceName)
{
    const QKeySequence sequence(raw);
    if (sequence.isEmpty()) {
        qCWarning(lcDaemon) << "unparseable shortcut" << raw << "on named workspace" << workspaceName
                            << "- leaving it unbound";
    }
    return sequence;
}

QString kwinShortcutBackupPath()
{
    // StateLocation is already app-scoped; see stateFilePath's relocation
    // note. The one-time move from the earlier nested path happens here too —
    // dropping this file would strand the user's stolen KWin chords on
    // "none" forever.
    // .ini, not .json: every reader and writer of this file opens it as
    // QSettings::IniFormat, and the old .json name described a format it never
    // had.
    const QString stateDir = QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    const QString path = stateDir + QStringLiteral("/kwin-shortcut-backup.ini");
    // Two legacy names to adopt, both holding the user's stolen chords: the
    // earlier double-nested path, and the misnamed .json beside the current
    // one. Dropping either would strand those chords on "none" forever.
    const QStringList legacyPaths{stateDir + QStringLiteral("/kwin-shortcut-backup.json"),
                                  stateDir + QStringLiteral("/plasmazones/kwin-shortcut-backup.json"),
                                  stateDir + QStringLiteral("/plasmazones/kwin-shortcut-backup.ini")};
    for (const QString& legacyPath : legacyPaths) {
        if (QFile::exists(path) || !QFile::exists(legacyPath)) {
            continue;
        }
        // The destination directory is not guaranteed to exist yet (a legacy
        // path can live one level DEEPER, so its existence says nothing about
        // the parent being writable); rename fails silently otherwise and the
        // user's stolen chords stay stranded in the old file.
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile::rename(legacyPath, path);
    }
    return path;
}

/// The adhoc ids bound for named workspaces on the last run. The ids are
/// derived from the workspace NAME, so a rename or a delete performed while
/// the daemon is DOWN leaves the old id registered in kglobalshortcutsrc
/// forever: the backend's transient scrub only covers the first registration
/// of the SAME id, and the teardown purge walks only the ids this process
/// holds. Persisting the list is what lets the next bring-up unregister what
/// no declaration names any more.
QString namedShortcutIdsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::StateLocation)
        + QStringLiteral("/workspace-shortcut-ids.ini");
}

QStringList loadBoundNamedShortcutIds()
{
    const QSettings store(namedShortcutIdsPath(), QSettings::IniFormat);
    return store.value(QStringLiteral("Shortcuts/boundIds")).toStringList();
}

void saveBoundNamedShortcutIds(const QStringList& ids)
{
    QDir().mkpath(QFileInfo(namedShortcutIdsPath()).absolutePath());
    QSettings store(namedShortcutIdsPath(), QSettings::IniFormat);
    if (ids.isEmpty()) {
        store.remove(QStringLiteral("Shortcuts/boundIds"));
    } else {
        store.setValue(QStringLiteral("Shortcuts/boundIds"), ids);
    }
    store.sync();
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
    // Already up. The rearm lambda below carries this check on its own arm;
    // the DIRECT callers (start(), and a re-entered start after a stop) did
    // not, and a second pass here would build a second controller and a
    // second m_workspaceWiring over the first — the old wiring's connections
    // are severed only by the unique_ptr reset, so the previous controller
    // would be destroyed mid-session with its state file written from a
    // half-torn map.
    if (m_workspaceController) {
        return;
    }
    // Enabling the feature (or granting consent) at runtime re-enters this
    // gate without a daemon restart.
    if (!m_workspaceRearmConnected) {
        m_workspaceRearmConnected = true;
        const auto rearm = [this]() {
            // These connections outlive stop() (they hang off m_settings, not
            // m_workspaceWiring, so the runtime-disable teardown cannot reach
            // them and must not — they are what re-enables the feature). A
            // settings save on a STOPPED daemon would otherwise re-run the
            // whole bring-up on torn-down dependencies.
            if (!m_running || m_shuttingDown) {
                return;
            }
            const bool wantOn = m_settings->workspacesEnabled();
            if (wantOn && !m_workspaceController) {
                initializeWorkspaces();
            } else if (!wantOn && m_workspaceController) {
                teardownWorkspaces();
                qCInfo(lcDaemon) << "dynamic workspaces disabled at runtime";
            }
        };
        connect(m_settings.get(), &Settings::workspacesEnabledChanged, this, rearm);
        connect(m_settings.get(), &Settings::workspacesManageKWinPerOutputChanged, this, rearm);
    }
    if (!m_settings->workspacesEnabled()) {
        return;
    }
    // Dependency gate BEFORE the consent write: enableKWinPerOutputDesktops
    // changes the user's kwinrc and restarts KWin's desktop layout, and doing
    // that on a bring-up that is about to bail for a missing dependency
    // reconfigures the session for a feature that never starts.
    if (!m_virtualDesktopManager || !m_windowRegistry || !m_screenManager) {
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
    connect(m_workspaceController.get(), &WorkspaceController::desktopsReapRequested, wiring,
            [this, forEachEngine](const QList<int>& desktops) {
                if (desktops.isEmpty()) {
                    return;
                }
                // Cancel BEFORE the prunes, the ordering both count-based
                // siblings in start.cpp use. The engines cancel their own
                // drag-insert previews, but cancelDragInsertIfActive also stops
                // the drag-scroll heartbeat and clears the drop indicator, and
                // neither of those is engine state — a desktop deleted mid
                // drag-insert otherwise left the indicator painted and the
                // timer running for the rest of the session.
                if (m_windowDragAdaptor) {
                    m_windowDragAdaptor->cancelDragInsertPreviews();
                }
                // The whole removal batch runs under one latch: a reap is a
                // CONTEXT prune, and handleEngineWindowsReleased must not read
                // it as a mode exit (see m_reapingDesktopStateDepth). Raised
                // as a depth through a scope guard so a nested reap cannot
                // lower it out from under this batch, and so no future early
                // return can leave it raised.
                {
                    ++m_reapingDesktopStateDepth;
                    const auto reapGuard = qScopeGuard([this] {
                        --m_reapingDesktopStateDepth;
                    });
                    for (int desktop : desktops) {
                        forEachEngine([desktop](PhosphorEngine::PlacementEngineBase* engine) {
                            engine->reapDesktopState(desktop);
                        });
                    }
                }

                // The unified placement store's per-record desktop tag: a
                // record on a dead desktop degrades to 0 (unknown) and the
                // effect's next report re-stamps it; KWin relocates the real
                // window either way.
                if (m_windowTrackingAdaptor) {
                    if (auto* service = m_windowTrackingAdaptor->service()) {
                        const QSet<int> dead(desktops.cbegin(), desktops.cend());
                        const int changed =
                            service->placementStore().transform([&dead](PhosphorEngine::WindowPlacement& record) {
                                if (record.virtualDesktop > 0 && dead.contains(record.virtualDesktop)) {
                                    record.virtualDesktop = 0;
                                    return true;
                                }
                                return false;
                            });
                        // transform only MUTATES and counts; it sets no dirty
                        // bit of its own, and saveState is dirty-gated — so
                        // without this the degraded records were live in
                        // memory and absent from the file, and the next
                        // session restored windows onto desktops that no
                        // longer exist. Same shape as the excluded-app prune
                        // in lifecycle.cpp. markDirty is what emits
                        // stateChanged, which is the save trigger.
                        if (changed > 0) {
                            service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
                        }
                        // The strip snapshots are keyed by
                        // "screen|desktop|activity", so the reap changed what
                        // a save would write for them too. Their sole
                        // producer is the engine's placementChanged, which the
                        // desktop prune does not raise.
                        //
                        // Marked unconditionally, unlike the placement bit
                        // above. reapDesktopState returns void, so there is no
                        // "the batch touched nothing" answer to gate on, and
                        // the alternative — walking every strip snapshot key
                        // for a dead desktop — costs more than the save it
                        // would occasionally skip. A reap batch is rare (it
                        // needs an actual desktop removal) and the worst case
                        // is one redundant debounced write.
                        service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyScrollStrips);
                    }
                }
                // Daemon-side per-context order cache keyed by desktop NUMBER.
                // Left behind, a dead desktop's key survives the renumber
                // untouched (it is absent from oldToNew) and a survivor
                // shifted into that number would land on an identical key.
                if (!m_lastEngineOrders.isEmpty()) {
                    const QSet<int> dead(desktops.cbegin(), desktops.cend());
                    for (auto it = m_lastEngineOrders.begin(); it != m_lastEngineOrders.end();) {
                        if (dead.contains(it.key().desktop)) {
                            it = m_lastEngineOrders.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }
                // Same story for the per-mode disabled-desktop gates: the
                // count-based sweep in start.cpp stands down once this
                // controller has adopted, so this is the only cleanup they
                // get from then on.
                // Composite entries are "screenId/desktopNumber" and virtual
                // screen ids contain '/' themselves, so the number is the
                // segment after the LAST one (the split the renumber arm
                // below uses).
                if (m_settings) {
                    const QSet<int> dead(desktops.cbegin(), desktops.cend());
                    bool anyChanged = false;
                    for (const auto mode : PhosphorZones::allModes()) {
                        QStringList disabled = m_settings->disabledDesktops(mode);
                        const auto removed =
                            std::remove_if(disabled.begin(), disabled.end(), [&dead](const QString& entry) {
                                const int slash = entry.lastIndexOf(QLatin1Char('/'));
                                if (slash < 0) {
                                    return false;
                                }
                                bool ok = false;
                                const int entryDesktop = QStringView(entry).mid(slash + 1).toInt(&ok);
                                return ok && dead.contains(entryDesktop);
                            });
                        if (removed == disabled.end()) {
                            continue;
                        }
                        disabled.erase(removed, disabled.end());
                        m_settings->setDisabledDesktops(mode, disabled);
                        anyChanged = true;
                    }
                    // Once for the whole batch, not once per removed desktop:
                    // each save is preceded by a full allModes() scan.
                    if (anyChanged) {
                        m_settings->save();
                    }
                }
            });
    connect(m_workspaceController.get(), &WorkspaceController::desktopRenumberRequested, wiring,
            [this, forEachEngine](const QHash<int, int>& oldToNew) {
                // Cancel before the rewrite, the same ordering the reap arm and
                // both count-based siblings use: the daemon-side drop indicator
                // and drag-scroll heartbeat are not engine state and no engine
                // clears them.
                if (m_windowDragAdaptor) {
                    m_windowDragAdaptor->cancelDragInsertPreviews();
                }
                forEachEngine([&oldToNew](PhosphorEngine::PlacementEngineBase* engine) {
                    engine->renumberDesktopState(oldToNew);
                });
                if (m_windowTrackingAdaptor) {
                    if (auto* service = m_windowTrackingAdaptor->service()) {
                        const int changed =
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
                        // transform mutates and counts, nothing more — no dirty
                        // bit, and saveState is dirty-gated, so a shift that
                        // nothing else dirtied never reached the file and the
                        // next session restored every window one renumber
                        // behind.
                        if (changed > 0) {
                            service->markDirty(PhosphorPlacement::WindowTrackingService::DirtyWindowPlacements);
                        }
                    }
                }
                // Daemon-side per-context caches keyed by desktop NUMBER. The
                // count-based sweep in start.cpp stands down once this
                // controller has ADOPTED (it only knows counts and would
                // destroy the wrong desktop's state), so from that point this
                // renumber pass is the only thing that keeps them aligned with
                // the engines.
                if (!m_lastEngineOrders.isEmpty()) {
                    QHash<TilingStateKey, QStringList> remapped;
                    remapped.reserve(m_lastEngineOrders.size());
                    // Two passes so a SHIFTED key always beats a key the map
                    // says nothing about. An unmapped key keeps its number
                    // verbatim, and a survivor shifted into that number
                    // produces an identical key; inserting in hash order would
                    // pick a winner at random and could hand a live context
                    // the other one's window order. The shifted key is the one
                    // the engines just renumbered to, so it wins and the
                    // unmapped duplicate is dropped.
                    //
                    // On the live path the collision below does not arise: the
                    // reap arm has already erased every key on a REMOVED
                    // desktop, and no surviving number is both unmapped and a
                    // renumber target. It is kept as defence in depth for a
                    // producer that ever hands over a partial map, or a reap
                    // that does not precede the renumber — the alternative is
                    // a silent random winner for a live context's window order.
                    for (auto it = m_lastEngineOrders.constBegin(); it != m_lastEngineOrders.constEnd(); ++it) {
                        if (it.key().desktop <= 0 || !oldToNew.contains(it.key().desktop)) {
                            continue;
                        }
                        TilingStateKey key = it.key();
                        key.desktop = oldToNew.value(key.desktop);
                        remapped.insert(key, it.value());
                    }
                    for (auto it = m_lastEngineOrders.constBegin(); it != m_lastEngineOrders.constEnd(); ++it) {
                        if (it.key().desktop > 0 && oldToNew.contains(it.key().desktop)) {
                            continue;
                        }
                        if (remapped.contains(it.key())) {
                            qCWarning(lcDaemon)
                                << "dropping stale window order for desktop" << it.key().desktop << "on screen"
                                << it.key().screenId << "- a renumbered desktop now owns that number";
                            continue;
                        }
                        remapped.insert(it.key(), it.value());
                    }
                    m_lastEngineOrders = remapped;
                }
                if (m_settings) {
                    // Composite entries are "screenId/desktopNumber"; virtual
                    // screen ids contain '/' themselves, so the number is the
                    // segment after the LAST one (same split
                    // pruneDisabledDesktopEntries uses).
                    bool anyChanged = false;
                    for (const auto mode : PhosphorZones::allModes()) {
                        QStringList disabled = m_settings->disabledDesktops(mode);
                        bool modeChanged = false;
                        for (QString& entry : disabled) {
                            const int slash = entry.lastIndexOf(QLatin1Char('/'));
                            if (slash < 0) {
                                continue;
                            }
                            bool ok = false;
                            const int desktop = QStringView(entry).mid(slash + 1).toInt(&ok);
                            if (!ok || desktop <= 0) {
                                continue;
                            }
                            const int mapped = oldToNew.value(desktop, desktop);
                            if (mapped == desktop) {
                                continue;
                            }
                            entry = entry.left(slash + 1) + QString::number(mapped);
                            modeChanged = true;
                        }
                        if (modeChanged) {
                            m_settings->setDisabledDesktops(mode, disabled);
                            anyChanged = true;
                        }
                    }
                    if (anyChanged) {
                        m_settings->save();
                    }
                }
                // Window metadata carries its own copy of the desktop number,
                // and KWin will NOT re-push it: desktopsChanged notifies the
                // desktops LIST, while a renumber mutates each
                // VirtualDesktop's x11DesktopNumber behind a different signal
                // the effect does not relay. Left alone, every tracked
                // window's virtualDesktop stays on the pre-renumber number
                // while the map and the engines have already shifted, and the
                // controller's census then buckets windows onto the wrong
                // desktop id. Remap here, same shape as the store transform
                // above: 0 and below are the sticky/unknown sentinels.
                if (m_windowRegistry) {
                    const QStringList instanceIds = m_windowRegistry->instanceIds();
                    for (const QString& instanceId : instanceIds) {
                        const auto meta = m_windowRegistry->metadata(instanceId);
                        if (!meta) {
                            continue;
                        }
                        PhosphorEngine::WindowMetadata updated = *meta;
                        bool changed = false;
                        const int mapped = updated.virtualDesktop > 0
                            ? oldToNew.value(updated.virtualDesktop, updated.virtualDesktop)
                            : updated.virtualDesktop;
                        if (mapped != updated.virtualDesktop) {
                            updated.virtualDesktop = mapped;
                            changed = true;
                        }
                        for (int& desktop : updated.virtualDesktops) {
                            if (desktop <= 0) {
                                continue;
                            }
                            const int mappedEntry = oldToNew.value(desktop, desktop);
                            if (mappedEntry != desktop) {
                                desktop = mappedEntry;
                                changed = true;
                            }
                        }
                        if (changed) {
                            // upsert re-emits metadataChanged, which is what
                            // re-keys the controller's census onto the new
                            // numbers. WindowMetadata comparison is by value,
                            // so an unchanged record would be a silent no-op
                            // anyway; the guard just skips the copy.
                            m_windowRegistry->upsert(instanceId, updated);
                        }
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
    connect(m_workspaceController.get(), &WorkspaceController::windowWorkspaceMoveRequested, wiring,
            [this](const QString& windowId, const QString& targetScreenId, int targetDesktop,
                   const QString& targetDesktopId, const QString& direction, bool moveOutput) {
                if (m_windowTrackingAdaptor) {
                    m_windowTrackingAdaptor->moveWindowToWorkspaceVerb(windowId, targetScreenId, targetDesktop,
                                                                       targetDesktopId, direction, moveOutput);
                }
            });
    wireWorkspaceOsdHints(wiring);
    // Focused-screen tracking (fork 3): externally created desktops adopt to
    // the screen the user is working on, resolved from the effect's activation
    // reports (already in the effect id space; extract the physical output).
    // Both adaptor-sourced connects are guarded: m_windowTrackingAdaptor is
    // wired in init() and is null on a bring-up that ran without it (a test
    // fixture, an init that failed before the adaptors), and connect() with a
    // null sender is a runtime warning plus a silently dead connection.
    if (m_windowTrackingAdaptor) {
        connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::windowActivated, wiring,
                [this](const QString& windowId, const QString& screenId) {
                    Q_UNUSED(windowId)
                    if (!screenId.isEmpty()) {
                        m_workspaceController->reconciler().setFocusedScreen(
                            PhosphorIdentity::VirtualScreenId::extractPhysicalId(screenId));
                    }
                });
    }

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
                    // Same gate every other navigation-feedback card honours
                    // (OSD style None, global suppression, the per-context
                    // rule); this one was bypassing it.
                    if (m_overlayService && navigationOsdAllowed(screenId)) {
                        m_overlayService->showDisabledOsd(
                            PhosphorI18n::tr("Moving a column needs a scrolling screen.", "OSD hint"), screenId);
                    }
                    return;
                }
                // The column is enumerated HERE, at chord time, while the move
                // itself defers behind the reconciler's ledger. That is
                // deliberate: the user pressed the chord on the column they
                // were looking at, so the membership that press meant is the
                // one that should travel, not whatever the strip holds once
                // the structural churn settles.
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
    connect(m_shortcutManager.get(), &ShortcutManager::workspaceMoveSlotRequested, wiring, [this](int slot) {
        // Quick-layout model: the slot's chord is fixed; what it moves the
        // window TO is the named workspace assigned in the settings app. An
        // unassigned slot does nothing (like a quick-layout slot with no
        // layout picked).
        // Trimmed on the read, like the declaration side: the target is
        // matched against the declared NAME, which was trimmed when it was
        // bound, so an untrimmed target here never matches anything.
        const QString name = m_settings->workspaceSlotTarget(slot - 1).trimmed();
        if (name.isEmpty()) {
            return;
        }
        const QString windowId = m_windowTrackingAdaptor ? m_windowTrackingAdaptor->lastActiveWindowId() : QString();
        m_workspaceController->moveWindowToNamedWorkspace(name, windowId);
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
        // Batched on BOTH halves: the per-id register flushes to the backend
        // once each, and on the Portal backend every flush supersedes the
        // prior in-flight Response, so a user with several named workspaces
        // lost the confirmation for all but the last chord.
        // Anything this process bound, PLUS anything the LAST run left behind.
        // Without the persisted half, a workspace renamed or deleted while the
        // daemon was down keeps its name-derived adhoc id registered for good.
        QStringList stale = m_workspaceNamedShortcutIds;
        for (const QString& id : loadBoundNamedShortcutIds()) {
            if (!stale.contains(id)) {
                stale.append(id);
            }
        }
        m_shortcutManager->unregisterAdhocShortcuts(stale);
        m_workspaceNamedShortcutIds.clear();
        QVector<PhosphorShortcutsIntegration::IAdhocRegistrar::AdhocBinding> bindings;
        // Two names differing only in surrounding whitespace trim to the same
        // string and would build the same adhoc id, so the second silently
        // replaced the first's callback. The declaration side collapses them
        // the same way, so the first one wins here too.
        QSet<QString> seenNames;
        const QVariantList entries = m_settings->workspacesNamedEntries();
        for (const QVariant& value : entries) {
            const QVariantMap entry = value.toMap();
            const QString name = entry.value(ConfigDefaults::namedEntryNameField()).toString().trimmed();
            if (name.isEmpty()) {
                continue;
            }
            if (seenNames.contains(name)) {
                qCWarning(lcDaemon) << "duplicate named workspace" << name
                                    << "- keeping the first declaration's shortcuts";
                continue;
            }
            seenNames.insert(name);
            const QString focusChord = entry.value(ConfigDefaults::namedEntryFocusShortcutField()).toString();
            if (!focusChord.isEmpty()) {
                const QKeySequence sequence = parseNamedChord(focusChord, name);
                if (!sequence.isEmpty()) {
                    const QString id = ShortcutIds::workspaceNamedFocusId(name);
                    bindings.append({id, sequence,
                                     PhosphorI18n::tr("Focus Workspace \"%1\"", "named workspace shortcut").arg(name),
                                     [this, name]() {
                                         m_workspaceController->focusNamedWorkspace(name);
                                     }});
                    m_workspaceNamedShortcutIds.append(id);
                }
            }
            const QString moveChord = entry.value(ConfigDefaults::namedEntryMoveShortcutField()).toString();
            if (!moveChord.isEmpty()) {
                const QKeySequence sequence = parseNamedChord(moveChord, name);
                if (!sequence.isEmpty()) {
                    const QString id = ShortcutIds::workspaceNamedMoveId(name);
                    bindings.append(
                        {id, sequence,
                         PhosphorI18n::tr("Move Window to Workspace \"%1\"", "named workspace shortcut").arg(name),
                         [this, name]() {
                             const QString windowId =
                                 m_windowTrackingAdaptor ? m_windowTrackingAdaptor->lastActiveWindowId() : QString();
                             m_workspaceController->moveWindowToNamedWorkspace(name, windowId);
                         }});
                    m_workspaceNamedShortcutIds.append(id);
                }
            }
        }
        m_shortcutManager->registerAdhocShortcuts(bindings);
        saveBoundNamedShortcutIds(m_workspaceNamedShortcutIds);
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
    if (m_windowTrackingAdaptor) {
        const auto handleModeReport = [this](bool enabled) {
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
        };
        connect(m_windowTrackingAdaptor, &WindowTrackingAdaptor::perOutputDesktopsModeReported, wiring,
                handleModeReport);
        // The signal is change-gated, so a wiring built AFTER the effect
        // already reported (a runtime enable, or consent granted mid-session)
        // never hears the report that matters and the self-heal above never
        // runs. Replay the adaptor's stored answer once here. nullopt means
        // the effect has not reported at all, which is not a divergence —
        // only a stored false is.
        if (m_windowTrackingAdaptor->perOutputDesktopsMode() == std::optional<bool>(false)) {
            handleModeReport(false);
        }
    }

    // ── Stock KWin desktop-shortcut takeover (Phase 5) ─────────────────────
    // The stock desktop-switch actions (the "Switch One Desktop" quad plus
    // the two Walk Through Desktops chords) iterate the GLOBAL pool, tripping
    // owner-wins snap-back on nearly every press, and the quad's Down/Up
    // chords are the focus verbs' defaults. With the rebind toggle on, back
    // each chord
    // up (state dir, once) and clear it via the KGlobalAccel foreign-rebind
    // pass-through; restore on toggle-off. If the backend cannot rebind
    // (portal), nothing is stolen and snap-back-with-hint stays the story.
    static const QStringList KWinDesktopActions{
        QStringLiteral("Switch One Desktop Up"), QStringLiteral("Switch One Desktop Down"),
        QStringLiteral("Switch One Desktop to the Left"), QStringLiteral("Switch One Desktop to the Right"),
        QStringLiteral("Walk Through Desktops"), QStringLiteral("Walk Through Desktops (Reverse)"),
        // KWin's own Overview holds Meta+W, the overview toggle's default.
        QStringLiteral("Overview")};
    const auto applyStockRebind = [this]() {
        if (!m_settings->workspacesRebindKWinShortcuts()) {
            restoreKWinShortcutBackup(m_shortcutManager.get());
            return;
        }
        QDir().mkpath(QFileInfo(kwinShortcutBackupPath()).absolutePath());
        QSettings backup(kwinShortcutBackupPath(), QSettings::IniFormat);
        for (const QString& action : KWinDesktopActions) {
            // A backup entry already present means the chord is ALREADY
            // stolen, so nothing re-reads it. That also means a user who
            // re-binds the stock chord by hand while we hold the backup keeps
            // it: re-stealing would fight the user's explicit choice, and the
            // restore on toggle-off would then overwrite what they chose with
            // the pre-theft value. Deliberate, not an oversight.
            if (backup.contains(action)) {
                continue;
            }
            const std::optional<QList<QKeySequence>> current =
                m_shortcutManager->foreignShortcuts(QStringLiteral("kwin"), action);
            if (!current) {
                // The query FAILED (wrong backend, kglobalacceld unreachable).
                // That is not "unbound": stealing on the strength of it would
                // clear a live chord with no backup written, and the later
                // restore would then have nothing to give back. Skip the
                // action entirely and let the next pass try again.
                qCWarning(lcDaemon) << "could not read KWin shortcut" << action << "- leaving it alone";
                continue;
            }
            if (current->isEmpty()) {
                continue; // genuinely unbound; nothing to steal or restore
            }
            // The backup is written and FLUSHED before the clear, per action.
            // The other order lost the user's chords outright: a crash (or a
            // kglobalaccel hiccup) between the clear and the post-loop sync
            // left every chord cleared with nothing on disk to restore from.
            // A backup for a clear that then failed is harmless — the restore
            // simply rewrites a value that is already live.
            // The FULL binding (primary + alternates) round-trips.
            backup.setValue(action, QKeySequence::listToString(*current, QKeySequence::PortableText));
            backup.sync();
            if (!m_shortcutManager->setForeignShortcuts(QStringLiteral("kwin"), action, {})) {
                qCWarning(lcDaemon) << "could not clear KWin shortcut" << action
                                    << "- its backup entry stands and the next restore rewrites what is already live";
            }
        }
        backup.sync();
    };
    applyStockRebind();
    connect(m_settings.get(), &Settings::workspacesRebindKWinShortcutsChanged, wiring, applyStockRebind);

    // Desktop-cap degradation hint (once per episode, reconciler-gated).
    connect(
        &m_workspaceController->reconciler(), &PhosphorWorkspaces::WorkspaceReconciler::capReached, wiring, [this]() {
            // Screen-less card (the cap is global), but it still answers to
            // the same suppression gate as the sibling hints above — an OSD
            // style of None or a suppressed session must not get one.
            if (m_overlayService && navigationOsdAllowed(QString())) {
                m_overlayService->showDisabledOsd(PhosphorI18n::tr("Workspace limit reached.", "OSD hint"), QString());
            }
        });

    // RouteToWorkspace rule resolver: the rules pipeline calls this on the
    // open path (rules_placement.cpp). True = routed (positional
    // RouteToDesktop is skipped); false = name unrealized, fall through.
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setWorkspaceRouteResolver(
            [this](const QString& name, const QString& windowId, bool moveOutput, QString* ownerScreenOut) {
                // > 0 is the REALIZED desktop the move was issued to (the caller
                // resolves its placement context on that number, outranking any
                // positional RouteToDesktop in the same cascade). 0 means the name
                // is not realized and the positional route applies; < 0 means the
                // name IS declared but is momentarily unresolvable, and the
                // positional route must NOT stand in for it.
                if (!m_workspaceController) {
                    return static_cast<int>(WorkspaceController::WorkspaceRouteVerdict::Unrealized);
                }
                return m_workspaceController->routeWindowToNamedWorkspace(name, windowId, moveOutput, ownerScreenOut);
            });
    }

    // Window → screen resolver for the owner-wins reunion arm and the sticky
    // predicate for the named-workspace verbs; both are the daemon's shared
    // answers (daemon/overview.cpp), the overview builder reads the same two.
    m_workspaceController->setWindowScreenResolver([this](const QString& windowId) {
        return trackedWindowScreen(windowId);
    });
    m_workspaceController->setWindowStickyPredicate([this](const QString& windowId) {
        return isTrackedWindowSticky(windowId);
    });

    m_workspaceController->start();
    initializeOverview();
    qCInfo(lcDaemon) << "dynamic workspaces active";
}

void Daemon::teardownWorkspaces()
{
    if (!m_workspaceController) {
        // Nothing was built (feature off, dormant gate, or already torn down).
        // The KWin chord backup is still restored below on purpose: the
        // takeover survives in the state file across a restart, so a stop()
        // with the feature since disabled must still give the chords back.
        if (m_shortcutManager) {
            restoreKWinShortcutBackup(m_shortcutManager.get());
        }
        return;
    }
    // Sever EVERY connection this feature made (m_workspaceWiring is the
    // receiver context for all of them — shortcut verbs, settings reactions,
    // the adaptor probe), drop the adhoc named binds whose lambdas capture the
    // controller, then tear the controller down (its dtor writes the final
    // state file) and hand KWin its chords back. The kwinrc write is
    // deliberately NOT reverted (stated in the UI).
    teardownOverview();
    m_workspaceWiring.reset();
    if (m_shortcutManager) {
        m_shortcutManager->unregisterAdhocShortcuts(m_workspaceNamedShortcutIds);
    }
    // Cleared whether or not the unregister ran: these ids name bindings of a
    // registry that is about to be destroyed anyway, and carrying them into
    // the next start() would hand unregisterAdhocShortcuts a list of ids the
    // fresh registry has never heard of.
    m_workspaceNamedShortcutIds.clear();
    // Nothing is bound any more, so the next bring-up has nothing to purge.
    saveBoundNamedShortcutIds({});
    m_workspaceController.reset();
    if (m_windowTrackingAdaptor) {
        m_windowTrackingAdaptor->setWorkspaceRouteResolver(nullptr);
        // The map stream stopped with the wiring, which leaves the effect
        // holding the LAST published map forever. The interface promises an
        // empty payload while the feature is off, and the effect reads empty
        // as "clear".
        m_windowTrackingAdaptor->setWorkspaceMapPayload(QString());
    }
    if (m_shortcutManager) {
        restoreKWinShortcutBackup(m_shortcutManager.get());
    }
}

} // namespace PlasmaZones
