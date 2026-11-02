// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHash>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace PlasmaZones {

/**
 * @brief The daemon's bookkeeping for a screen whose shape (the size,
 *        orientation and aspect ratio a rule can read) changes under the
 *        assignment verdict its windows were laid out by.
 *
 * Pure logic over injected lookups, so the repeat sequences it exists for (a
 * report ahead of the geometry it describes, a monitor swapped on one
 * connector, a clear and a fresh report, a diff that absorbs a verdict) run in
 * a test without a Daemon. It holds four sets of state.
 *
 * Reshape records, keyed by effective screen id: the assignment id the
 * daemon's active-assignment snapshot held before the screen's shape changed.
 * A diff that runs between the change and the reconcile without applying (a
 * screen add, a layout list change) folds the new verdict into the snapshot,
 * after which the reconcile's own diff comes back empty for that screen. The
 * recorded id is what still tells it the verdict moved.
 *
 * Compositor pixel-size reports, keyed by connector name. A report is APPLIED
 * (the daemon re-seeds and persists it and reconciles the screens it
 * reshaped) only when it agrees with the screen the daemon tracks on that
 * connector (@ref reportMatchesLogicalSize). The report travels over D-Bus and
 * the QScreen change over Wayland, in no fixed order, so a report can describe
 * a mode the daemon's logical geometry does not show yet, or a monitor that
 * replaced the one the daemon still tracks on the same connector. Such a
 * report is PARKED: its reshape is recorded but nothing is resnapped, saved or
 * seeded, and the daemon evaluates it again on the screen's geometry or scale
 * change and on its connector's add. A report that has not been applied since
 * start-up, since its screen was added, or since its connector's report was
 * cleared is AWAITED: it replaces a stand-in with the real size, so the change
 * it causes is the screen settling and applies without an OSD.
 *
 * The silent set, keyed by effective screen id: the screens whose verdict
 * change the running reconcile applies without an OSD.
 */
class ScreenReshapeTracker
{
public:
    /// What the daemon's active-assignment snapshot holds for one screen.
    struct SnapshotEntry
    {
        QString assignmentId;
        QString shapeToken;
    };

    /// What the tracker reads of the daemon, injected so it runs without one.
    struct Lookups
    {
        /// The snapshot's entry for an effective screen, nullopt when the
        /// snapshot does not hold the screen yet.
        std::function<std::optional<SnapshotEntry>(const QString& screenId)> snapshot;
        /// The screen's shape token as the layout registry reads it now.
        std::function<QString(const QString& screenId)> shapeToken;
    };

    /// What the daemon knows about a connector's report when it asks.
    struct ReportFacts
    {
        /// The daemon tracks a screen with an identifier on the connector.
        bool tracked = false;
        /// The connector holds a compositor report (false after a clear).
        bool reported = false;
        /// The report agrees with the tracked screen's logical geometry and
        /// scale (@ref reportMatchesLogicalSize).
        bool consistent = false;
    };

    enum class ReportAction {
        /// Nothing to do: no tracked screen on the connector to apply it to.
        Ignore,
        /// Record the reshape and wait for the screen to catch up.
        Park,
        /// Reconcile the screens the report reshaped.
        Apply,
    };

    struct ReportVerdict
    {
        ReportAction action = ReportAction::Ignore;
        /// The reconcile applies the verdict change without an OSD.
        bool silent = false;
        /// Re-seed and persist the reported size under the screen's identifier.
        bool persist = false;
    };

    explicit ScreenReshapeTracker(Lookups lookups);

    // ─── Reshape records ─────────────────────────────────────────────────

    /// Record the screens among @p screenIds whose shape differs from the one
    /// the snapshot was resolved under, and return them. A screen the snapshot
    /// does not hold yet is skipped, because its first diff primes it. The
    /// FIRST record for a screen stands until it is taken or dropped, so a
    /// second reshape in between still compares against the verdict the
    /// windows were laid out under.
    QStringList noteReshaped(const QStringList& screenIds);

    /// Consume every record and return the recorded screens still in the
    /// snapshot whose assignment id now differs from the recorded one.
    QSet<QString> takeVerdictChanges();

    /// Drop the records of @p screenIds, which an apply or a context switch
    /// has laid out under their current verdict.
    void dropRecords(const QStringList& screenIds);

    /// Drop the records whose screen id satisfies @p matches.
    void dropRecordsWhere(const std::function<bool(const QString& screenId)>& matches);

    /// Drop every record.
    void clearRecords();

    bool hasRecord(const QString& screenId) const;

    // ─── Compositor pixel-size reports ───────────────────────────────────

    /// Whether @p reported (device pixels) is the mode of an output whose
    /// logical size is @p logical at @p scale: each side within scale + 1
    /// device pixels of the logical side times the scale. The compositor
    /// derives the logical size by dividing the mode by the scale and rounding,
    /// which moves the product by up to half the scale. A different mode, a
    /// rotation the logical geometry has not caught up with, a coarse integer
    /// scale standing in for a fractional one, and another monitor's size all
    /// land outside. False for an empty size and a non-positive scale. The
    /// layout editor accepts a pixel size by the same test.
    static bool reportMatchesLogicalSize(const QSize& reported, const QSize& logical, qreal scale);

    /// Decide what @p connectorName's report, described by @p facts, means
    /// now. Called on every report change for the connector and again when a
    /// parked report may have become consistent. A cleared report on a
    /// tracked connector arms the awaited state, so the next report applies
    /// silently, and its own reshape (to the seed or to no size) is silent
    /// too. A consistent report applies, silently when it was awaited.
    ReportVerdict evaluateReport(const QString& connectorName, const ReportFacts& facts);

    /// The daemon began tracking a screen on @p connectorName (start-up or a
    /// screen add): its next applied report is awaited. Call before
    /// evaluating the report the connector may already hold.
    void screenAdded(const QString& connectorName);

    /// The daemon stopped tracking the screen on @p connectorName. A report
    /// it still holds is evaluated afresh by the next add.
    void screenRemoved(const QString& connectorName);

    bool isParked(const QString& connectorName) const;
    bool isAwaitingFirstReport(const QString& connectorName) const;

    // ─── Silent applies ──────────────────────────────────────────────────

    void markSilent(const QStringList& screenIds);
    bool isSilent(const QString& screenId) const;
    void clearSilent();

    /// Drop all state (daemon stop).
    void reset();

private:
    Lookups m_lookups;
    QHash<QString, QString> m_preChangeAssignmentIds;
    QSet<QString> m_awaitingFirstReport;
    QSet<QString> m_parkedReports;
    QSet<QString> m_silentScreens;
};

} // namespace PlasmaZones
