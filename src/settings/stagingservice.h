// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QString>
#include <QVariantList>

#include <optional>

namespace PlasmaZones {

class Settings;

/// Owns the "staged-but-not-yet-saved" state that the Settings app
/// accumulates between `load()` and `save()`.
///
/// Covers three staging categories:
///   1. **Assignments** — per-(screen × desktop × activity) snapping /
///      tiling layout assignments, plus full-context clears and the
///      atomic mode+layout staging used by the Overview page.
///   2. **Virtual-screen configurations** — staged virtual screen layouts
///      per physical screen, flushed to Settings (for persistence) BEFORE
///      `Settings::save()` and to the daemon (via D-Bus) AFTER.
///   3. **Quick-layout slots** — both snapping and tiling slot writes go
///      to the daemon's mode-keyed LayoutRegistry via D-Bus (after
///      `notifyReload`), flushed together by `flushQuickSlotsToDaemon()`.
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
    /// A single entry in the assignment staging map. `fullCleared` takes
    /// precedence over individual field clears; `stagedMode` (Overview
    /// page's atomic write) takes precedence over the per-field snapping
    /// / tiling fields.
    struct StagedAssignment
    {
        QString screenId;
        int virtualDesktop = 0;
        QString activityId;
        std::optional<QString> snappingLayoutId;
        std::optional<QString> tilingAlgorithmId;
        std::optional<int> stagedMode;
        bool fullCleared = false;
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

    /// Stage a full clear of the (screen × desktop × activity) context.
    void stageFullClear(const QString& screen, int desktop, const QString& activity);

    /// Stage a tiling-only clear (flushes as "mode=0 + no layouts",
    /// reverting the context back to snapping mode).
    void stageTilingClear(const QString& screen, int desktop, const QString& activity);

    /// Atomic mode+layout staging used by the Overview page, bypasses the
    /// per-field paths and flushes through `setAssignmentEntry`.
    void stageAssignmentEntry(const QString& screen, int desktop, const QString& activity, int mode,
                              const QString& snappingLayoutId, const QString& tilingAlgorithmId);

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
    /// `setSaveBatchMode(false)` to amortise the broadcast cost. Clears the
    /// staging map on completion.
    void flushAssignmentsToDaemon();

    // ── Virtual screen staging ────────────────────────────────────────

    /// Stage a virtual-screen configuration for a physical screen. Empty
    /// list ≡ stage a removal.
    void stageVirtualScreenConfig(const QString& physicalScreenId, const QVariantList& screens);
    void stageVirtualScreenRemoval(const QString& physicalScreenId);

    bool hasUnsavedVirtualScreenConfig(const QString& physicalScreenId) const;
    QVariantList stagedVirtualScreenConfig(const QString& physicalScreenId) const;

    /// Persist staged VS configs to Settings (KConfig) for on-disk
    /// durability. Runs BEFORE `Settings::save()` — the subsequent save
    /// writes everything out in one go.
    void flushVirtualScreensToSettings(Settings& settings);

    /// Push staged VS configs to the daemon via D-Bus. Runs AFTER
    /// `Settings::save()` but BEFORE `notifyReload` so virtual screen IDs
    /// exist by the time assignments referencing them are processed.
    /// Clears the staging map on completion.
    void flushVirtualScreensToDaemon();

    // ── Quick layout slots ────────────────────────────────────────────

    void stageSnappingQuickSlot(int slotNumber, const QString& layoutId);
    void stageTilingQuickSlot(int slotNumber, const QString& layoutId);

    /// Returns true if slot has a staged value. Fills @p out with the
    /// staged layout ID (possibly empty).
    bool stagedSnappingQuickSlot(int slotNumber, QString& out) const;
    bool stagedTilingQuickSlot(int slotNumber, QString& out) const;

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

    /// Drop all staged quick-slot edits for the mode (per-page Discard reverts
    /// to the daemon's saved slots; the getters fall back to the daemon when a
    /// slot is not staged).
    void clearSnappingQuickSlots();
    void clearTilingQuickSlots();

    /// Push staged quick-layout slots (both snapping and tiling modes) to the
    /// daemon's mode-keyed LayoutRegistry via D-Bus. Runs AFTER `notifyReload`
    /// so the daemon has the fresh config. Clears both staging maps on
    /// completion.
    void flushQuickSlotsToDaemon();

private:
    StagedAssignment& assignmentEntry(const QString& screen, int desktop, const QString& activity);
    const StagedAssignment* assignmentEntryConst(const QString& screen, int desktop, const QString& activity) const;

    static QString assignmentCacheKey(const QString& screen, int desktop, const QString& activity);

    QHash<QString, StagedAssignment> m_assignments;
    QHash<QString, QVariantList> m_virtualScreenConfigs;
    QHash<int, QString> m_snappingQuickSlots;
    QHash<int, QString> m_tilingQuickSlots;
};

} // namespace PlasmaZones
