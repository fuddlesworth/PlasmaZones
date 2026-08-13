// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>
#include <QVariantList>

#include <functional>
#include <optional>

namespace PlasmaZones {

class Settings;

/// Owns the "staged-but-not-yet-saved" state that the Settings app
/// accumulates between `load()` and `save()`.
///
/// Covers three staging categories:
///   1. **Assignments** — per-(screen × desktop × activity) snapping /
///      tiling layout assignments, plus per-slot clears and the atomic
///      mode+layout staging used by the Overview page.
///   2. **Virtual-screen configurations** — staged virtual screen layouts
///      per physical screen, flushed to Settings (for persistence) BEFORE
///      `Settings::save()` and to the daemon (via D-Bus) AFTER.
///   3. **Quick-layout slots** — the snapping, tiling and scrolling slot
///      writes all go to the daemon's mode-keyed LayoutRegistry via D-Bus
///      (after `notifyReload`), flushed together by
///      `flushQuickSlotsToDaemon()`.
///
/// Orchestrated by SettingsController's save lifecycle — callers are
/// expected to invoke the flush methods in the right order (persistence
/// phase → Settings::save() → D-Bus delivery phase → notifyReload →
/// remaining D-Bus phase). Ordering-staged state (snapping / tiling
/// layout order) stays on SettingsController because it couples to
/// per-page signal emissions.
class StagingService
{
public:
    /// A single entry in the assignment staging map. `stagedMode` (Overview
    /// page's atomic write) takes precedence over the per-field snapping
    /// / tiling fields.
    struct StagedAssignment
    {
        QString screenId;
        int virtualDesktop = 0;
        QString activityId;
        std::optional<QString> snappingLayoutId;
        std::optional<QString> tilingAlgorithmId;
        /// The scrolling template slot. Unlike its two siblings this one is
        /// NOT carried by setAssignmentEntry — the daemon writes it through
        /// its own per-slot setter — so the flush issues a second call for it
        /// after the entry write. Three values, matching the field it lands
        /// in: a template UUID, an empty string (inherit the default), and
        /// PhosphorZones::NoScrollingTemplate (explicitly none). Engaged only
        /// when the user actually touched the control, so a mode toggle on a
        /// scrolling screen never rewrites the template slot.
        std::optional<QString> scrollingTemplateId;
        std::optional<int> stagedMode;
    };

    StagingService() = default;

    /// Reset all staged state. Called from SettingsController::load() +
    /// defaults() — the reload that follows will repopulate any saved
    /// values from Settings/D-Bus.
    void clearAll();

    // ── Assignment staging ────────────────────────────────────────────

    /// Stage a snapping-layout assignment. The snapping and tiling slots are
    /// mutually exclusive in the unified Rule model — one assignment
    /// context carries either a snapping layout or a tiling algorithm, not
    /// both — so staging here clears any staged tiling assignment for the
    /// same context. Callers that want to write both atomically must use
    /// `stageAssignmentEntry`.
    void stageSnapping(const QString& screen, int desktop, const QString& activity, const QString& layoutId);

    /// Stage a tiling-algorithm assignment. See `stageSnapping` — the two
    /// slots are mutually exclusive, so this clears any staged snapping
    /// assignment for the same context.
    void stageTiling(const QString& screen, int desktop, const QString& activity, const QString& layoutId);

    /// Remove any staged entry for the (screen × desktop × activity)
    /// context entirely — a true unstage. After this, the flush leaves the
    /// context's daemon-side assignment untouched. No-op when nothing is
    /// staged for the context.
    void removeStagedAssignment(const QString& screen, int desktop, const QString& activity);

    /// Stage a tiling-only clear (flushes as "mode=0 + no layouts",
    /// reverting the context back to snapping mode).
    void stageTilingClear(const QString& screen, int desktop, const QString& activity);

    /// Atomic mode+layout staging used by the Overview page, bypasses the
    /// per-field paths and flushes through `setAssignmentEntry`.
    void stageAssignmentEntry(const QString& screen, int desktop, const QString& activity, int mode,
                              const QString& snappingLayoutId, const QString& tilingAlgorithmId);

    /// Stage the scrolling template slot for a context. Independent of the
    /// mode/layout staging above: the Overview page calls this alongside
    /// `stageAssignmentEntry` when the template dropdown was touched, and
    /// omits it otherwise so an untouched slot is never rewritten.
    void stageScrollingTemplate(const QString& screen, int desktop, const QString& activity, const QString& templateId);

    /// Out-param query for the scrolling template slot, same shape as
    /// `stagedSnappingLayout`. The staged value may legitimately be an empty
    /// string (inherit the default) or the explicit-none token, so the
    /// return value is the only way to tell "staged" from "not staged".
    bool stagedScrollingTemplate(const QString& screen, int desktop, const QString& activity, QString& out) const;

    /// Out-param query: returns true if the context has a staged
    /// snapping value, writes the staged value (possibly empty string for
    /// a cleared state) into @p out.
    bool stagedSnappingLayout(const QString& screen, int desktop, const QString& activity, QString& out) const;

    /// Same as `stagedSnappingLayout` for the tiling-algorithm slot.
    /// Reattaches the `autotile:` id prefix when needed so callers can
    /// compare against full layout IDs.
    bool stagedTilingLayout(const QString& screen, int desktop, const QString& activity, QString& out) const;

    /// Raw staged-assignment lookup for callers that need the full entry
    /// (e.g., Q_INVOKABLE getStagedAssignment returns a variant-map
    /// projection). Returns nullptr if no entry is staged for the
    /// context.
    const StagedAssignment* stagedAssignmentFor(const QString& screen, int desktop, const QString& activity) const;

    bool hasPendingAssignments() const
    {
        return !m_assignments.isEmpty();
    }

    /// Flush all staged assignments to the daemon via D-Bus. SettingsController
    /// wraps this with `setSaveBatchMode(true)` + `applyAssignmentChanges` +
    /// `setSaveBatchMode(false)` to amortise the broadcast cost.
    ///
    /// Returns false when ANY per-entry call came back as a D-Bus error, and in
    /// that case the staging map is RETAINED. Clearing it on a failed flush was
    /// the bug this contract exists to prevent: the retry Save found nothing
    /// staged, did nothing, and cleared the unsaved badge while the daemon had
    /// never applied the edits. Every call this method makes is an idempotent
    /// setter, so re-flushing the whole retained map on the next Save is safe.
    /// @param templateExists Optional predicate answering whether a scrolling
    /// template UUID still names a live template. Supplied because the
    /// daemon's `setScrollingTemplateLayout` is a VOID slot: it refuses an
    /// unknown id with a warning and a bare return, which on the wire is an
    /// ordinary successful reply, so the flush's transport check cannot see
    /// the refusal. Without this the staged pick was discarded, the map
    /// cleared and the unsaved badge dropped as though it had saved — and the
    /// refusal is reachable in one session, by deleting a template on the
    /// Layouts page while a Monitors-page pick is still staged. Left unset
    /// (fixtures, embedders) the pre-check is skipped and the old behaviour
    /// stands.
    [[nodiscard]] bool flushAssignmentsToDaemon(const std::function<bool(const QString&)>& templateExists = {});

    // ── Virtual screen staging ────────────────────────────────────────

    /// Stage a virtual-screen configuration for a physical screen. Empty
    /// list ≡ stage a removal.
    void stageVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens);
    void stageVirtualScreenRemoval(const QString& physicalScreenId);

    bool hasUnsavedVirtualScreenConfig(const QString& physicalScreenId) const;
    QVariantList stagedVirtualScreenConfig(const QString& physicalScreenId) const;

    /// True if any virtual-screen edit is staged (any physical screen). Backs
    /// the per-page dirty check for the Virtual Screens page.
    bool hasStagedVirtualScreenConfigs() const
    {
        return !m_virtualScreenConfigs.isEmpty();
    }

    /// Drop all staged virtual-screen edits (per-page Discard reverts to the
    /// saved configs; the getters fall back to the daemon otherwise).
    void clearVirtualScreenConfigs();

    /// Persist staged VS configs to Settings (KConfig) for on-disk
    /// durability. Runs BEFORE `Settings::save()` — the subsequent save
    /// writes everything out in one go.
    void flushVirtualScreensToSettings(Settings& settings);

    /// Push staged VS configs to the daemon via D-Bus. Runs AFTER
    /// `Settings::save()` but BEFORE `notifyReload` so virtual screen IDs
    /// exist by the time assignments referencing them are processed.
    ///
    /// Same failure contract as `flushAssignmentsToDaemon`: false when any
    /// per-screen call errored, and the staging map is retained so the retry
    /// Save has something to send.
    [[nodiscard]] bool flushVirtualScreensToDaemon();

    // ── Quick layout slots ────────────────────────────────────────────

    void stageSnappingQuickSlot(int slotNumber, const QString& layoutId);
    void stageTilingQuickSlot(int slotNumber, const QString& layoutId);
    void stageScrollingQuickSlot(int slotNumber, const QString& templateId);

    /// Returns true if slot has a staged value. Fills @p out with the
    /// staged layout ID (possibly empty).
    bool stagedSnappingQuickSlot(int slotNumber, QString& out) const;
    bool stagedTilingQuickSlot(int slotNumber, QString& out) const;
    bool stagedScrollingQuickSlot(int slotNumber, QString& out) const;

    /// True if any quick-slot edit is staged for the mode. Backs the per-page
    /// dirty check for the Quick Shortcuts pages.
    bool hasStagedSnappingQuickSlots() const
    {
        return !m_snappingQuickSlots.isEmpty();
    }
    bool hasStagedTilingQuickSlots() const
    {
        return !m_tilingQuickSlots.isEmpty();
    }
    bool hasStagedScrollingQuickSlots() const
    {
        return !m_scrollingQuickSlots.isEmpty();
    }

    /// Drop all staged quick-slot edits for the mode (per-page Discard reverts
    /// to the daemon's saved slots; the getters fall back to the daemon when a
    /// slot is not staged).
    void clearSnappingQuickSlots();
    void clearTilingQuickSlots();
    void clearScrollingQuickSlots();

    /// Push staged quick-layout slots (snapping, tiling and scrolling modes)
    /// to the daemon's mode-keyed LayoutRegistry via D-Bus. Runs AFTER
    /// `notifyReload` so the daemon has the fresh config.
    ///
    /// Same failure contract as `flushAssignmentsToDaemon`: false when any
    /// per-slot call errored, and the staging map for the failing mode is
    /// retained. All three modes are attempted regardless of the others'
    /// outcome.
    [[nodiscard]] bool flushQuickSlotsToDaemon();

private:
    StagedAssignment& assignmentEntry(const QString& screen, int desktop, const QString& activity);
    const StagedAssignment* assignmentEntryConst(const QString& screen, int desktop, const QString& activity) const;

    static QString assignmentCacheKey(const QString& screen, int desktop, const QString& activity);

    QHash<QString, StagedAssignment> m_assignments;
    QHash<QString, QVariantList> m_virtualScreenConfigs;
    QHash<int, QString> m_snappingQuickSlots;
    QHash<int, QString> m_tilingQuickSlots;
    QHash<int, QString> m_scrollingQuickSlots;
};

} // namespace PlasmaZones
