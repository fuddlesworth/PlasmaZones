// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorWorkspaces/WorkspaceMap.h>
#include <phosphorworkspaces_export.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace PhosphorWorkspaces {

/// One declared named workspace (config-driven; persistent while empty).
struct NamedWorkspace
{
    QString name; ///< unique, non-empty
    QString outputId; ///< pinned screen; empty = unpinned
    int position = -1; ///< preferred slice index; -1 = before the trailing empty
};

/// The dynamic-workspace lifecycle state machine. Owns the WorkspaceMap and the
/// pending-op ledger (echo safety). All inputs are plain method calls — no
/// D-Bus, no daemon types — so unit tests drive it with scripted sequences.
/// Outputs are signals the controller wires to VirtualDesktopManager (KWin
/// calls), the engines (reap/renumber), and the stream/persistence layer.
///
/// Echo-safety contract: every request* signal the reconciler emits is recorded
/// in the ledger FIRST. Incoming notifications try to match-and-retire a ledger
/// entry; matched notifications update the map but trigger no reactive policy.
/// Unmatched notifications are external and get the full policy response.
/// Entries expire after LedgerTimeoutMs with a resyncRequested() so the
/// controller can re-pull authoritative state.
class PHOSPHORWORKSPACES_EXPORT WorkspaceReconciler : public QObject
{
    Q_OBJECT

public:
    explicit WorkspaceReconciler(QObject* parent = nullptr);

    static constexpr int LedgerTimeoutMs = 2000;
    static constexpr int DestroyDebounceMs = 300;

    WorkspaceMap& map();
    const WorkspaceMap& map() const;
    quint64 generation() const;

    /// KWin's desktop-count ceiling. Trailing-empty appends are suspended at
    /// the cap (capReached() hints once per episode). Default 20; the
    /// controller may adjust after live probing.
    void setDesktopCap(int cap);
    /// The screen adoption falls back to for externally created desktops and
    /// for slices orphaned before Phase-4 hotplug memory exists.
    void setFocusedScreen(const QString& screenId);

    // ── Inputs (notifications; plain calls) ─────────────────────────────────
    /// Authoritative ordered KWin id list settled (from desktopListChanged).
    /// Computes the old→new int renumber mapping from the id delta, repairs the
    /// map, adopts unowned ids, then runs invariant maintenance.
    void onDesktopListSettled(const QStringList& ids);
    /// Early id-only echo of a KWin desktop creation.
    void onKwinDesktopCreated(const QString& desktopId);
    /// Early id-only echo of a KWin desktop removal.
    void onKwinDesktopRemoved(const QString& desktopId);
    /// A screen's current desktop report (1-based int, effect path). Returns
    /// true when the report was a matched echo of our own SetCurrent (the
    /// caller then skips its own reactive policy for it).
    bool onScreenDesktopReport(const QString& screenId, int desktop);
    /// Window population for a desktop id changed (controller-maintained).
    void onPopulationChanged(const QString& desktopId, int windowCount);
    void onScreenAdded(const QString& screenId);
    void onScreenRemoved(const QString& screenId);
    /// Screen order recomputed by the controller (geometry left-to-right).
    void onScreenOrderChanged(const QStringList& order);

    /// First-run / restart adoption (plan §4.3): distribute `ids` (KWin global
    /// order) over screens given each screen's current desktop id. Existing map
    /// content is kept where consistent (restore path passes a pre-loaded map).
    void adoptAll(const QStringList& ids, const QHash<QString, QString>& currentDesktopIdByScreen);

    /// Reconcile the declared named workspaces against the map (called after
    /// adoption and on every declaration change). Per declaration, in order:
    /// an entry already carrying the name is kept (transferred to its pinned
    /// output when needed); else an unnamed desktop whose KWin name matches
    /// (`kwinNames` is aligned with the settled id list) is claimed; else a
    /// desktop is created with the name (cap-guarded — a refusal logs and
    /// hints, never crashes). Map names with no surviving declaration revert
    /// to dynamic (requestSetDesktopName clears the KWin name; destroy-on-
    /// empty may then reap the desktop).
    void applyNamedWorkspaces(const QList<NamedWorkspace>& declarations, const QStringList& kwinNames);

    // ── Verb support ────────────────────────────────────────────────────────
    /// True while any structural op (create/remove) is open — verb execution
    /// defers until the ledger is quiet (renumbering window, plan §4.5).
    bool hasPendingStructuralOps() const;
    /// The desktop id a screen currently shows, resolved from the last report
    /// against the settled id list; empty when unknown.
    QString currentDesktopIdOf(const QString& screenId) const;
    /// The id `delta` slots up (-1) / down (+1) from the screen's current
    /// desktop WITHIN ITS OWN SLICE (foreign desktops are skipped by
    /// construction); empty at the slice edge (no wrap) or when unresolved.
    QString desktopIdAtOffset(const QString& screenId, int delta) const;
    /// The id at 0-based position `sliceIndex` of the screen's slice, or empty.
    QString desktopIdAtSliceIndex(const QString& screenId, int sliceIndex) const;

    /// Issue a ledgered per-screen switch (focus verbs and snap-back).
    /// Refused (returns false) while a SetCurrent for the screen is already
    /// open — the single-correction rule that breaks re-assertion loops.
    bool issueSetCurrent(const QString& screenId, const QString& desktopId);
    /// Owner-wins correction: return `screenId` to its last-shown owned
    /// desktop (fallback: its slice head). No-op when the screen already
    /// shows an owned desktop. Returns true when a correction was issued.
    bool snapBack(const QString& screenId);
    /// Reorder the screen's CURRENT workspace within its slice by delta
    /// (map order only — KWin's global order self-repairs opportunistically).
    bool reorderCurrentWorkspace(const QString& screenId, int delta);
    /// Re-own the screen's current workspace to `targetScreenId`, inserted
    /// before that screen's trailing empty. Window relocation is the
    /// controller's job (§4.2); this is the map half. Returns the moved
    /// desktop id (empty on failure).
    QString transferCurrentWorkspace(const QString& screenId, const QString& targetScreenId);

Q_SIGNALS:
    // Outputs → KWin (via VirtualDesktopManager), ledgered before emit.
    void requestCreateDesktop(uint position, const QString& name);
    void requestRemoveDesktop(const QString& desktopId);
    void requestSetCurrent(const QString& screenId, const QString& desktopId);
    /// Sync a desktop's KWin name (named-workspace claim / unname). Creates
    /// carry their name in createDesktop and never ride this.
    void requestSetDesktopName(const QString& desktopId, const QString& name);

    /// Map mutated (any structural or metadata change). The controller streams
    /// (change-gated) and persists on this.
    void mapChanged();
    /// Desktops renumbered: oldInt → newInt for survivors; `removed` lists the
    /// old ints whose desktops vanished (engines reap those, then renumber).
    void renumberComputed(const QHash<int, int>& oldToNew, const QList<int>& removed);
    /// An unmatched (external) switch put screenId on a desktop owned by
    /// another screen — Phase 2 wires snap-back; Phase 1 logs only.
    void foreignSwitchDetected(const QString& screenId, const QString& desktopId, const QString& ownerScreenId);
    /// The desktop cap stopped a trailing-empty append (once per episode).
    void capReached();
    /// A ledger entry expired; controller should re-pull authoritative state.
    void resyncRequested();

private:
    struct PendingOp
    {
        enum class Kind {
            Create,
            Remove,
            SetCurrent
        };
        Kind kind;
        QString desktopId; ///< Remove/SetCurrent target
        QString screenId; ///< Create owner / SetCurrent screen
        int sliceIndex = 0; ///< Create insertion point
        QString name; ///< Create name (named workspaces)
        qint64 deadline = 0;
    };

    void ledgerAdd(PendingOp op);
    void expireLedger();
    /// Trailing-empty + slice-never-empty repair for every screen.
    void maintainInvariants();
    void maintainScreen(const QString& screenId);
    void scheduleDestroyCheck(const QString& desktopId);
    void requestCreateAt(const QString& screenId, int sliceIndex, const QString& name);
    bool isDesktopEmpty(const QString& desktopId) const;
    /// The trailing entry of a slice iff it is an empty dynamic desktop.
    QString trailingEmptyOf(const QString& screenId) const;
    void adoptExternal(const QString& desktopId);
    void bumpGeneration();

    WorkspaceMap m_map;
    quint64 m_generation = 0;
    QStringList m_lastIds; ///< id list as of the last settled reply
    QHash<QString, int> m_population; ///< desktopId → window count
    QHash<QString, int> m_currentByScreen; ///< screenId → 1-based global int
    /// screenId → last OWNED desktop id it showed (the snap-back target).
    QHash<QString, QString> m_lastOwnedByScreen;
    QString m_focusedScreen;
    int m_desktopCap = 20;
    bool m_capHintShown = false;
    QList<PendingOp> m_ledger;
    QTimer m_ledgerTimer;
    QHash<QString, QTimer*> m_destroyTimers; ///< desktopId → debounce
};

} // namespace PhosphorWorkspaces
