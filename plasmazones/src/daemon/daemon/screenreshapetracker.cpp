// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenreshapetracker.h"

#include "common/daemonscreenscale.h"

#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/PhysicalScreen.h>

#include <iterator>
#include <utility>

namespace PlasmaZones {

ScreenReshapeTracker::ScreenReshapeTracker(Lookups lookups)
    : m_lookups(std::move(lookups))
{
}

QStringList ScreenReshapeTracker::noteReshaped(const QStringList& screenIds)
{
    QStringList reshaped;
    for (const QString& screenId : screenIds) {
        const std::optional<SnapshotEntry> snapshot = m_lookups.snapshot(screenId);
        if (!snapshot || snapshot->shapeToken == m_lookups.shapeToken(screenId)) {
            continue;
        }
        if (!m_preChangeAssignmentIds.contains(screenId)) {
            m_preChangeAssignmentIds.insert(screenId, snapshot->assignmentId);
        }
        reshaped.append(screenId);
    }
    return reshaped;
}

QSet<QString> ScreenReshapeTracker::takeVerdictChanges()
{
    QSet<QString> moved;
    for (auto it = m_preChangeAssignmentIds.constBegin(); it != m_preChangeAssignmentIds.constEnd(); ++it) {
        const std::optional<SnapshotEntry> snapshot = m_lookups.snapshot(it.key());
        if (snapshot && snapshot->assignmentId != it.value()) {
            moved.insert(it.key());
        }
    }
    m_preChangeAssignmentIds.clear();
    return moved;
}

void ScreenReshapeTracker::dropRecords(const QStringList& screenIds)
{
    for (const QString& screenId : screenIds) {
        m_preChangeAssignmentIds.remove(screenId);
    }
}

void ScreenReshapeTracker::dropRecordsWhere(const std::function<bool(const QString& screenId)>& matches)
{
    for (auto it = m_preChangeAssignmentIds.begin(); it != m_preChangeAssignmentIds.end();) {
        it = matches(it.key()) ? m_preChangeAssignmentIds.erase(it) : std::next(it);
    }
}

void ScreenReshapeTracker::clearRecords()
{
    m_preChangeAssignmentIds.clear();
}

bool ScreenReshapeTracker::hasRecord(const QString& screenId) const
{
    return m_preChangeAssignmentIds.contains(screenId);
}

ScreenReshapeTracker::ReportVerdict ScreenReshapeTracker::evaluateReport(const QString& connectorName,
                                                                         const ReportFacts& facts)
{
    ReportVerdict verdict;
    if (!facts.tracked) {
        // Nothing the daemon tracks to apply it to. The connector's add
        // evaluates whatever report it holds by then.
        m_parkedReports.remove(connectorName);
        return verdict;
    }
    if (facts.pendingSize.isEmpty()) {
        // The compositor dropped the output, or a seed moved under a screen
        // with no report pending. Either way, once the daemon accepts the
        // clear, the screen answers a stand-in (its seed, or no size) until
        // its next report, which settles it like a first one, and the move to
        // the stand-in is no change the user made.
        m_parkedReports.remove(connectorName);
        m_awaitingFirstReport.insert(connectorName);
        verdict.action = ReportAction::Apply;
        verdict.silent = true;
        return verdict;
    }
    // Applied unchecked without a sensor, parked unchecked before it maps:
    // see the declaration.
    const bool consistent = !facts.scaleSensor
        || (facts.scaleSensorMapped
            && pixelSizeMatchesLogicalSize(facts.pendingSize, facts.logicalSize, facts.logicalScale));
    if (!consistent) {
        // A mode the logical geometry has not caught up with, another monitor
        // on the same connector, or a sensor that has not mapped. The awaited
        // state stays as it is, so the report applies as the first one when
        // it does apply.
        m_parkedReports.insert(connectorName);
        verdict.action = ReportAction::Park;
        return verdict;
    }
    m_parkedReports.remove(connectorName);
    verdict.action = ReportAction::Apply;
    verdict.silent = m_awaitingFirstReport.remove(connectorName);
    verdict.persist = true;
    return verdict;
}

void ScreenReshapeTracker::screenAdded(const QString& connectorName)
{
    m_awaitingFirstReport.insert(connectorName);
}

void ScreenReshapeTracker::screenRemoved(const QString& connectorName)
{
    m_awaitingFirstReport.remove(connectorName);
    m_parkedReports.remove(connectorName);
}

bool ScreenReshapeTracker::isParked(const QString& connectorName) const
{
    return m_parkedReports.contains(connectorName);
}

bool ScreenReshapeTracker::isAwaitingFirstReport(const QString& connectorName) const
{
    return m_awaitingFirstReport.contains(connectorName);
}

void ScreenReshapeTracker::markSilent(const QStringList& screenIds)
{
    for (const QString& screenId : screenIds) {
        m_silentScreens.insert(screenId);
    }
}

bool ScreenReshapeTracker::isSilent(const QString& screenId) const
{
    return m_silentScreens.contains(screenId);
}

void ScreenReshapeTracker::clearSilent()
{
    m_silentScreens.clear();
}

void ScreenReshapeTracker::reset()
{
    m_preChangeAssignmentIds.clear();
    m_awaitingFirstReport.clear();
    m_parkedReports.clear();
    m_silentScreens.clear();
}

ScreenReshapeTracker::ReportVerdict admitPixelSizeReport(ScreenReshapeTracker& tracker,
                                                         PhosphorScreens::ScreenManager& screens,
                                                         const QString& connectorName,
                                                         const PixelSizePersistSink& persist,
                                                         const ReportScaleReader& readScale)
{
    // A report for an output the host does not track yet (the compositor and
    // the host see an output arrive in no fixed order) is left to that
    // output's add, which evaluates it against the screen it then has.
    const PhosphorScreens::PhysicalScreen screen = screens.physicalScreenFor(connectorName);
    ScreenReshapeTracker::ReportFacts facts;
    facts.tracked = screen.isValid() && !screen.identifier.isEmpty();
    // The PENDING report: nothing reads it until it is accepted below, so a
    // report this call parks moves no rule, no verdict and no seed.
    const QSize pendingSize = screens.pendingPixelSize(connectorName).value_or(QSize());
    if (facts.tracked) {
        facts.pendingSize = pendingSize;
        facts.logicalSize = screen.geometry.size();
        if (readScale) {
            readScale(screen, facts);
        } else {
            facts.logicalScale = screens.logicalScale(screen);
            facts.scaleSensor = screens.hasLogicalScaleSensor(screen);
            facts.scaleSensorMapped = screens.isLogicalScaleSensorMapped(screen);
        }
    }
    const ScreenReshapeTracker::ReportVerdict verdict = tracker.evaluateReport(connectorName, facts);
    if (verdict.action != ScreenReshapeTracker::ReportAction::Apply) {
        return verdict;
    }
    // The report (or the clear) becomes what ScreenManager::pixelSize answers
    // for this screen, the registry's shape provider and getScreenInfo
    // included, before the caller notes the reshape it causes.
    screens.acceptCompositorPixelSize(connectorName);
    if (verdict.persist) {
        // What the compositor reports now is next session's seed, and this
        // session's too: re-seeding a monitor with an accepted report answers
        // nothing new (an accepted report outranks a seed, so the manager
        // stays silent), but it is what a replug of the same monitor reads
        // until a report is accepted for it again. Only this screen's, under
        // its own identifier, and only once the report agrees with it: another
        // connector's report may be parked, describing a monitor that has not
        // arrived. The host merges, so a monitor unplugged now keeps the size
        // it last had.
        screens.seedPixelSize(screen.identifier, pendingSize);
        if (persist) {
            persist(screen.identifier, pendingSize);
        }
    }
    return verdict;
}

} // namespace PlasmaZones
