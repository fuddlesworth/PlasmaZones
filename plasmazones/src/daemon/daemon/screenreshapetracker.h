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

namespace PhosphorScreens {
class ScreenManager;
struct PhysicalScreen;
}

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
 * Compositor pixel-size reports, keyed by connector name. ScreenManager holds
 * each report PENDING, and no reader sees it until the daemon accepts it. A
 * report is APPLIED (accepted, so it becomes what ScreenManager::pixelSize
 * answers, then re-seeded and persisted, and the screens it reshaped
 * reconciled) only when it agrees with the screen the daemon tracks on that
 * connector (pixelSizeMatchesLogicalSize, in common/daemonscreenscale.h). The
 * report travels over D-Bus and the QScreen change over Wayland, in no fixed
 * order, so a report can describe a mode the daemon's logical geometry does
 * not show yet, or a monitor that replaced the one the daemon still tracks on
 * the same connector. Such a report, and any report evaluated before the
 * screen's scale sensor has mapped, is PARKED: it stays pending, so the
 * screen's shape, its verdict and every rule keep the size accepted (or
 * seeded) before it, and the daemon evaluates it again on the screen's
 * geometry or scale change, on its scale sensor's map, and on its
 * connector's add. A report that has not been applied since start-up, since
 * its screen was added, or since its connector's report was cleared is
 * AWAITED: it replaces a stand-in with the real size, so the change it causes
 * is the screen settling and applies without an OSD.
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
        /// The connector's PENDING report (ScreenManager::pendingPixelSize),
        /// empty after a clear.
        QSize pendingSize;
        /// The tracked screen's logical size and logical scale, the report
        /// is checked against.
        QSize logicalSize;
        qreal logicalScale = 0.0;
        /// The tracked screen has a geometry sensor
        /// (ScreenManager::hasLogicalScaleSensor), so @ref logicalScale is,
        /// or will be once the sensor maps, the compositor's own scale.
        bool scaleSensor = false;
        /// The sensor has mapped (ScreenManager::isLogicalScaleSensorMapped),
        /// so @ref logicalScale is the compositor's own scale now.
        bool scaleSensorMapped = false;
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

    /// Decide what @p connectorName's pending report, described by @p facts,
    /// means now. Called on every report change for the connector and again
    /// when a parked report may have become consistent. A cleared report on a
    /// tracked connector applies (the daemon accepts the clear) and arms the
    /// awaited state, so the next report applies silently, and its own
    /// reshape (to the seed or to no size) is silent too. A report applies
    /// when it agrees with the tracked screen (pixelSizeMatchesLogicalSize),
    /// silently when it was awaited, and parks otherwise.
    ///
    /// A screen with no geometry sensor applies every report unchecked. Its
    /// logical scale is the coarse QScreen fallback for good (the integer
    /// ceiling, 2 for a 1.25 output), against which a fractional output's
    /// report never agrees, and no later scale change would re-evaluate it,
    /// so the check would park the report forever and leave the screen's
    /// rules without a size. The compositor's report is the authority on the
    /// mode, and the check exists only to wait for the daemon's geometry to
    /// catch up with it.
    ///
    /// A screen whose sensor exists but has not mapped parks every report
    /// unchecked. The fallback scale there is no basis for a verdict either
    /// way: a 5K monitor at 200 % and a 4K one at 150 % share a 2560x1440
    /// logical size and the fallback 2, so the outgoing 5K's report would
    /// pass the check against the incoming 4K. The sensor's map announces
    /// the real scale (ScreenManager::logicalScaleSensorMapped, on an integer
    /// output too) and the daemon evaluates the report again then.
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

/// Where @ref admitPixelSizeReport sends a report it persists: the host saves
/// @p pixelSize as the size of the monitor @p screenIdentifier names, for the
/// next session to seed from.
using PixelSizePersistSink = std::function<void(const QString& screenIdentifier, const QSize& pixelSize)>;

/// Fills the scale fields of @p facts (logicalScale, scaleSensor,
/// scaleSensorMapped) for the tracked @p screen.
using ReportScaleReader =
    std::function<void(const PhosphorScreens::PhysicalScreen& screen, ScreenReshapeTracker::ReportFacts& facts)>;

/**
 * @brief Evaluate @p connectorName's pending compositor report against the
 *        screen @p screens tracks on that connector, and act on the verdict.
 *
 * The report and the screen's logical geometry are read from @p screens, and
 * its scale through @p readScale, or from @p screens (ScreenManager's
 * logicalScale, hasLogicalScaleSensor and isLogicalScaleSensorMapped) when
 * none is given. On Apply the pending report, or the pending clear, is
 * accepted (ScreenManager::acceptCompositorPixelSize), and when the verdict
 * also persists, the size is re-seeded under the screen's identifier
 * (ScreenManager::seedPixelSize) and handed to @p persist. A parked report is
 * left pending. Reacting to the reshape an apply causes is the caller's.
 * Returns the tracker's verdict.
 */
ScreenReshapeTracker::ReportVerdict admitPixelSizeReport(ScreenReshapeTracker& tracker,
                                                         PhosphorScreens::ScreenManager& screens,
                                                         const QString& connectorName,
                                                         const PixelSizePersistSink& persist,
                                                         const ReportScaleReader& readScale = {});

} // namespace PlasmaZones
