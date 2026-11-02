// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "screenreshapetracker.h"

#include "common/daemonscreenscale.h"

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
    // Unchecked without a sensor: see the declaration.
    const bool consistent =
        !facts.scaleSensor || pixelSizeMatchesLogicalSize(facts.pendingSize, facts.logicalSize, facts.logicalScale);
    if (!consistent) {
        // A mode the logical geometry has not caught up with, another monitor
        // on the same connector, or a scale still at its coarse fallback until
        // the sensor maps. The awaited state stays as it is, so the report
        // applies as the first one when it does apply.
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

} // namespace PlasmaZones
