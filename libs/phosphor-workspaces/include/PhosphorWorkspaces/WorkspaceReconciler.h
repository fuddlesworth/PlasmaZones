// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorWorkspaces/WorkspaceMap.h>
#include <phosphorworkspaces_export.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

namespace PhosphorWorkspaces {

/// One declared named workspace (config-driven; persistent while empty).
struct PHOSPHORWORKSPACES_EXPORT NamedWorkspace
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
    /// How many times a removal KWin never answered is re-armed before the
    /// desktop is left alone. A genuinely refused removal (KWin declining to
    /// drop that desktop) would otherwise re-arm, re-issue, expire and resync
    /// forever; after this many rounds the surplus empty desktop simply stays.
    /// The budget is restored the moment the desktop's population changes or
    /// the desktop actually goes away.
    static constexpr int MaxRemovalRefusals = 3;
    /// How many times a create KWin never answered is re-driven for one screen
    /// before that screen stops asking. The mirror of MaxRemovalRefusals, and
    /// for the same reason: expireLedger re-runs maintenance after a Create
    /// expiry so a transient stall retries, and without a budget a create KWin
    /// permanently refuses becomes one D-Bus call per LedgerTimeoutMs for the
    /// life of the session. Deliberately NOT tied to cap-learning — the probe
    /// only concludes when the id list is byte-identical across two expiries,
    /// which a machine with any other desktop churn never reaches. The budget
    /// is restored when a create lands for that screen, when the desktop count
    /// drops (headroom came back), and when the screen goes away.
    static constexpr int MaxCreateRefusals = 3;
    /// KWin's desktop ceiling (VirtualDesktopManager::maximum() in current
    /// KWin). Shared by the daemon's gate and the settings app's cap badge. It
    /// is the STARTING value only: the real ceiling is learned from the
    /// compositor at runtime, in expireLedger, and forgotten again as soon as a
    /// create succeeds or the live count exceeds it.
    static constexpr int DefaultDesktopCap = 20;
    /// How many Create expiries in one episode (the id list unchanged
    /// throughout) it takes before the ceiling is believed. KWin refuses a
    /// create past its maximum with no reply at all, and so does a stalled bus,
    /// so a single unanswered create is not evidence of a cap — see
    /// expireLedger.
    static constexpr int CapProbeExpiries = 2;
    /// How many times a KWin rename that never took effect is re-pushed before
    /// the desktop is left with the name it has. The mirror of
    /// MaxRemovalRefusals, for the same reason: applyNamedWorkspaces runs on
    /// every settle, so a permanently declined rename would re-fire forever.
    /// The budget is restored when KWin's names agree, when the name reverts to
    /// dynamic, and when the desktop goes away.
    static constexpr int MaxNamePushRefusals = 3;

    WorkspaceMap& map();
    const WorkspaceMap& map() const;
    quint64 generation() const;

    /// KWin's desktop-count ceiling. Trailing-empty appends are suspended at
    /// the cap (capReached() hints once per episode). Test-only seam: nothing
    /// in the daemon calls this, because the production path starts at
    /// DefaultDesktopCap and learns the compositor's real ceiling itself from
    /// repeated create expiry. Tests use it to build a small-cap world without
    /// having to stall twenty creates.
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
    /// adoption and on every declaration change; implemented in
    /// WorkspaceReconcilerNamed.cpp). Per declaration, in order:
    /// an entry already carrying the name is kept (transferred to its pinned
    /// output when needed); else an unnamed desktop whose KWin name matches
    /// (`kwinNames` is aligned with the settled id list) is claimed; else a
    /// desktop is created with the name (cap-guarded — a refusal logs and
    /// hints, never crashes). Map names with no surviving declaration revert
    /// to dynamic (requestSetDesktopName clears the KWin name; destroy-on-
    /// empty may then reap the desktop).
    ///
    /// `kwinNames` MUST be KWin's raw names (VirtualDesktopManager::
    /// rawDesktopNames()), where an empty entry means "unnamed". Passing the
    /// display form breaks identity: a workspace declared as "Desktop 3" would
    /// claim an unnamed desktop whose placeholder happens to read that way.
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

    // ── By-id verbs (the workspace overview) ────────────────────────────────
    /// Move @p desktopId to @p newSliceIndex inside its own slice. Map order
    /// only, like reorderCurrentWorkspace: KWin's global order self-repairs.
    bool reorderWorkspace(const QString& desktopId, int newSliceIndex);
    /// Re-own @p desktopId to @p targetScreenId at @p sliceIndex (clamped to
    /// the slice, and never after that screen's trailing empty). Refused when
    /// the source slice would drop to zero, the target is unknown, or the
    /// desktop is unowned. Returns the moved id, empty on refusal. Window
    /// relocation is the controller's job, as for transferCurrentWorkspace.
    QString transferWorkspace(const QString& desktopId, const QString& targetScreenId, int sliceIndex);
    /// Ledger a Create at @p sliceIndex of @p screenId (0 = before the first
    /// entry, sliceSize = after the last) and tag the settled desktop
    /// RESERVED: the destroy debounce and the surplus-empties sweep skip a
    /// reserved desktop until its first population report, so a workspace
    /// created for a drop survives the moment before its window arrives.
    /// Refused (false) under the create budget / cap rules of maintenance.
    bool requestInsertWorkspace(const QString& screenId, int sliceIndex);
    /// Drop the reservation of @p desktopId (the drop that created it never
    /// delivered its window); the next maintenance pass treats it as any
    /// other empty dynamic desktop.
    void releaseReservation(const QString& desktopId);
    bool isReserved(const QString& desktopId) const
    {
        return m_reservedDesktops.contains(desktopId);
    }
    /// Push a KWin name for a DYNAMIC workspace through the name ledger (the
    /// named-declaration path pushes its own). Refused for a desktop the map
    /// does not own.
    bool requestRename(const QString& desktopId, const QString& name);
    /// The screen's trailing empty dynamic desktop, or empty.
    QString trailingEmptyOf(const QString& screenId) const;

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
    /// A window populated `desktopId` while our removeDesktop for it was in
    /// flight (plan §4.3 destroy step 4): KWin will sweep its windows to an
    /// arbitrary neighbour when the removal lands. The controller snapshots
    /// the census and re-routes those windows to the owner's current
    /// workspace once the desktop is gone.
    void removalRaceDetected(const QString& desktopId, const QString& ownerScreenId);
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
        /// Create only: the GLOBAL position the request asked KWin to insert
        /// at (WorkspaceMap::globalPositionForInsert at request time). The
        /// settle path matches open Creates against the new id's index in
        /// KWin's list with it, because the ledger's order (oldest request
        /// first) and the settled list's order (KWin position) are independent
        /// rankings and disagree whenever the screen that asked second owns an
        /// earlier slice.
        int globalPosition = 0;
        QString name; ///< Create name (named workspaces)
        qint64 deadline = 0;
        bool reserved = false; ///< Create: tag the settled desktop reserved (see requestInsertWorkspace)
    };

    void ledgerAdd(PendingOp op);
    void expireLedger();
    /// KWin answered one of our creates: forget the cap-probe evidence and
    /// restore the default ceiling if a previous episode had lowered it.
    void noteCreateSucceeded();
    /// A create landed for this screen: hand back its retry budget.
    void noteCreateLandedFor(const QString& screenId);
    /// True while a Remove for this desktop is open in the ledger.
    bool hasPendingRemove(const QString& desktopId) const;
    /// True while a Create owned by this screen is open in the ledger.
    bool hasPendingCreate(const QString& screenId) const;
    /// Open Create entries — the desktops KWin still owes us. Counted into the
    /// cap gate so a burst of requests cannot overshoot the ceiling.
    int pendingCreateCount() const;
    /// Drop every open ledger entry that targets this desktop id.
    void retireLedgerFor(const QString& desktopId);
    /// Pair the ids that are NEW in a settled list (in settled-list order)
    /// against the open Creates, for the case where the settle beat (or
    /// replaced) the id-only echoes. Rank, not ledger order and not per-id
    /// distance: the echo path can match FIFO because KWin echoes creations in
    /// the order it performed them, but a settled list is ordered by POSITION,
    /// and with several Creates open the screen that asked second can own the
    /// earlier slot. Distance cannot decide it either, because a batch of
    /// requests all record positions that ignore their sibling creates and so
    /// run uniformly low. Consumes every op it pairs; unpaired ids are left
    /// for the caller to adopt. Returns id → the Create it belongs to.
    QHash<QString, PendingOp> takeSettledCreates(const QStringList& newIds);
    /// Insert a paired settled create on its requesting screen. Returns false
    /// when that screen is gone, leaving the caller to adopt normally.
    bool applySettledCreate(const PendingOp& op, const QString& desktopId);
    /// Trailing-empty + slice-never-empty repair for every screen.
    void maintainInvariants();
    void maintainScreen(const QString& screenId);
    void scheduleDestroyCheck(const QString& desktopId);
    void requestCreateAt(const QString& screenId, int sliceIndex, const QString& name, bool reserved = false);
    bool isDesktopEmpty(const QString& desktopId) const;
    /// The slot a new entry takes so it lands BEFORE the screen's trailing
    /// empty: the slice size, or one less when a trailing empty exists.
    int insertIndexBeforeTrailingEmpty(const QString& screenId) const;
    /// Where a named declaration's workspace belongs in a slice: its declared
    /// position clamped to the last slot BEFORE the trailing empty (-1 means
    /// exactly that slot).
    int namedSliceIndex(const QString& screenId, int declaredPosition) const;
    /// Adopt an externally created desktop onto the focused screen (or the
    /// first screen). Returns false when no screen exists to adopt onto, so
    /// the caller does not announce a map change that never happened.
    bool adoptExternal(const QString& desktopId);
    void bumpGeneration();
    /// Owner-wins check for one screen's recorded current desktop; emits
    /// foreignSwitchDetected when it shows another screen's desktop (and no
    /// correction is already in flight for it).
    void evaluateForeign(const QString& screenId);

    WorkspaceMap m_map;
    quint64 m_generation = 0;
    QStringList m_lastIds; ///< id list as of the last settled reply
    QHash<QString, int> m_population; ///< desktopId → window count
    QHash<QString, int> m_currentByScreen; ///< screenId → 1-based global int
    /// screenId → last OWNED desktop id it showed (the snap-back target).
    QHash<QString, QString> m_lastOwnedByScreen;
    QString m_focusedScreen;
    int m_desktopCap = DefaultDesktopCap;
    bool m_capHintShown = false;
    QList<PendingOp> m_ledger;
    QTimer m_ledgerTimer;
    QHash<QString, QTimer*> m_destroyTimers; ///< desktopId → debounce
    /// Desktops created for a drop whose window has not arrived yet (see
    /// requestInsertWorkspace); exempt from destruction until then.
    QSet<QString> m_reservedDesktops;
    /// Desktops already reported through removalRaceDetected while their
    /// Remove is open — the signal is an edge, not a level, so every further
    /// population increment on the same doomed desktop must stay quiet.
    QSet<QString> m_racedDesktops;
    struct NamePush
    {
        QString name;
        qint64 deadline = 0;
        int refusals = 0; ///< pushes of this name KWin never acknowledged
    };
    /// desktopId → the KWin name we last pushed for it and when that push
    /// stops being assumed in flight. applyNamedWorkspaces runs on every
    /// settle, so without this an identical push re-fires each time KWin
    /// declines the rename or its name list simply lags. Cleared the moment
    /// KWin's names agree, when the name reverts to dynamic, and when the
    /// desktop goes away.
    QHash<QString, NamePush> m_namePushes;
    /// desktopId → how many of our Removes for it expired unanswered. Bounds
    /// the re-arm below MaxRemovalRefusals; cleared on the desktop's removal,
    /// on any population change, and when the id leaves KWin's list.
    QHash<QString, int> m_removalRefusals;
    /// screenId → how many of our Creates for it expired unanswered. Bounds
    /// the post-expiry re-drive below MaxCreateRefusals; cleared when a create
    /// for the screen lands, when the desktop count drops, and when the screen
    /// is removed.
    QHash<QString, int> m_createRefusals;
    /// The id list the current cap-probe episode is being counted against, and
    /// how many Create expiries it has seen. An episode ends the moment the
    /// list changes or a create succeeds; see expireLedger and CapProbeExpiries.
    QStringList m_capProbeIds;
    int m_capProbeExpiries = 0;
};

} // namespace PhosphorWorkspaces
