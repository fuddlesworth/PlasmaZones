// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The batch-assignment family of the LayoutRegistry adaptor: the four
// setAll* verbs plus the changed-screen marking they share. Split out of
// assignment.cpp by concern when that file crossed the size ceiling; the
// single-context verbs, readers, and scrolling-template verbs stay there.

#include "layoutadaptor.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include <PhosphorLayoutApi/LayoutId.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/ScreenIdentity.h>
#include <QScreen>
#include <QSet>

namespace PlasmaZones {

void LayoutAdaptor::markScreensWithStoredAssignments(AssignmentFamily family)
{
    // Every batch setter routes through LayoutRegistry::applyBatchAssignments,
    // which DROPS every rule of the family before rebuilding from the incoming
    // map. A screen whose assignment is removed by being absent from that map
    // therefore changes, but marking only the map's own keys never recorded it —
    // so it was never resnapped and never appeared in assignmentChangesApplied,
    // and it kept its old placement until something unrelated moved it.
    //
    // Scoped to the family being replaced. A setter that rebuilds the Desktop
    // rules leaves the Monitor-only, Activity and Combined rules untouched, so
    // marking a screen because it holds one of THOSE costs a resnap and a
    // per-screen OSD for a screen whose resolved assignment cannot have moved.
    //
    // The three context families are read from the registry's own family
    // readers, the round-trip counterparts of the batch setters. That also
    // closes the old coverage gap: those readers enumerate every context a
    // family holds, where probing one context per screen from here missed a
    // screen whose only stored entry was for a desktop the user is not
    // currently on. The base family has no such reader, so it is probed per
    // screen with the exact monitor-only tuple.
    //
    // The three family readers return every screen id the registry has EVER stored
    // a rule for, including monitors that are not connected now. The base branch is
    // naturally free of those because it walks the live screen list; the readers are
    // not, so they are intersected with it. Marking a disconnected monitor would put
    // it into the resnap set and the per-screen OSD run for a screen that cannot be
    // resnapped or shown anything, widening the blast radius past what this pass had
    // before the readers replaced the per-screen probe.
    const QStringList liveScreenIds = m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList();
    const QSet<QString> liveScreens(liveScreenIds.cbegin(), liveScreenIds.cend());
    // Without a screen manager (degraded single-argument-constructor mode, test
    // fixtures) there is no live set to filter against. Mark unfiltered, as this
    // pass always did, rather than silently marking nothing.
    const bool filterToLiveScreens = m_screenManager != nullptr;
    const auto markScreen = [this, &liveScreens, filterToLiveScreens](const QString& screenId) {
        if (!filterToLiveScreens || liveScreens.contains(screenId)) {
            m_changedScreenIds.insert(screenId);
        }
    };
    switch (family) {
    case AssignmentFamily::Base: {
        if (!m_screenManager) {
            // Degraded single-argument-constructor mode (test fixtures). Say so
            // rather than returning silently: the batch still replaces the base
            // family, so a screen dropped from the incoming map goes unmarked.
            qCWarning(lcDbusLayout) << "markScreensWithStoredAssignments: no screen manager — base-family screens "
                                       "dropped by this batch will not be marked as changed";
            return;
        }
        for (const QString& screenId : liveScreenIds) {
            // Exact monitor-only shape (desktop 0, no activity) — not the
            // cascade, which would also report a Desktop or Activity rule this
            // setter does not touch.
            if (m_layoutManager->hasExplicitAssignment(screenId, 0, QString())) {
                m_changedScreenIds.insert(screenId);
            }
        }
        break;
    }
    case AssignmentFamily::Desktop: {
        const auto assignments = m_layoutManager->desktopAssignments();
        for (auto it = assignments.constBegin(); it != assignments.constEnd(); ++it) {
            markScreen(it.key().first);
        }
        break;
    }
    case AssignmentFamily::Activity: {
        const auto assignments = m_layoutManager->activityAssignments();
        for (auto it = assignments.constBegin(); it != assignments.constEnd(); ++it) {
            markScreen(it.key().first);
        }
        break;
    }
    case AssignmentFamily::Combined: {
        const auto assignments = m_layoutManager->combinedAssignments();
        for (auto it = assignments.constBegin(); it != assignments.constEnd(); ++it) {
            markScreen(it.key().screenId);
        }
        break;
    }
    }
}

void LayoutAdaptor::setAllScreenAssignments(const QVariantMap& assignments)
{
    markScreensWithStoredAssignments(AssignmentFamily::Base);
    QHash<QString, QString> parsedAssignments;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenIdOrName = it.key();
        QString layoutId = it.value().toString();
        // The reserved snapping opt-out word is a storable value, not a UUID:
        // rejecting it here is DATA LOSS, because the paired getter emits it
        // and the batch apply drops the whole rule family before this
        // validation re-adds entries — a skipped key deletes the stored
        // opt-out on a get->set round trip. Same exemption setAssignmentEntry
        // gives the word.
        if (!layoutId.isEmpty() && requiresManualLayoutValidation(layoutId)) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch screen assignment"));
            if (!uuidOpt) {
                continue;
            }
        }
        const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenIdOrName);
        parsedAssignments[resolvedId] = layoutId;
        m_changedScreenIds.insert(resolvedId);
    }

    m_layoutManager->setAllScreenAssignments(parsedAssignments);
    // Update global active layout for the primary screen so zone overlay/drag see the new layout
    // immediately (same as assignLayoutToScreen). KCM Save uses this path.
    QScreen* primary = Utils::primaryScreen();
    if (primary) {
        PhosphorZones::Layout* primaryLayout =
            m_layoutManager->resolveLayoutForScreen(PhosphorScreens::ScreenIdentity::identifierFor(primary));
        if (primaryLayout) {
            m_layoutManager->setActiveLayout(primaryLayout);
        }
    }

    qCInfo(lcDbusLayout) << "Batch set" << parsedAssignments.size() << "screen assignments";
}

void LayoutAdaptor::setAllDesktopAssignments(const QVariantMap& assignments)
{
    markScreensWithStoredAssignments(AssignmentFamily::Desktop);
    QHash<QPair<QString, int>, QString> parsedAssignments;

    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        // Split on '|' delimiter (screen IDs contain colons, so ':' is not safe)
        int sep = it.key().lastIndexOf(QLatin1Char('|'));
        if (sep < 0) {
            // Backward compat: try legacy ':' delimiter for old KCM round-trips.
            // lastIndexOf is correct here because desktop numbers are always the
            // last component (e.g., "DP-2:3"), and screen IDs contain colons
            // (e.g., "DEL:DELL U2722D:115107:3" → last ':' before "3").
            // Warning: virtual screen IDs (physId/vs:N) also contain ':' — the
            // numeric guard below may misparse "physId/vs:0" as desktop=0.
            // This is caught by the virtualDesktop < 1 check on line below.
            sep = it.key().lastIndexOf(QLatin1Char(':'));
            // Guard: verify the desktop part is actually a number, not part of a screen ID
            if (sep > 0) {
                bool isDesktop = false;
                it.key().mid(sep + 1).toInt(&isDesktop);
                if (!isDesktop) {
                    qCWarning(lcDbusLayout) << "Desktop assignment key has non-numeric desktop part"
                                            << "with ':' delimiter:" << it.key();
                    sep = -1;
                }
            }
        }
        if (sep < 1) {
            qCWarning(lcDbusLayout) << "Invalid desktop assignment key format:" << it.key();
            continue;
        }

        QString screenIdOrName = it.key().left(sep);
        bool ok;
        int virtualDesktop = it.key().mid(sep + 1).toInt(&ok);
        if (!ok || virtualDesktop < 1) {
            qCWarning(lcDbusLayout) << "Invalid virtual desktop number:" << it.key().mid(sep + 1);
            continue;
        }

        QString layoutId = it.value().toString();
        // The reserved snapping opt-out word is a storable value, not a UUID:
        // rejecting it here is DATA LOSS, because the paired getter emits it
        // and the batch apply drops the whole rule family before this
        // validation re-adds entries — a skipped key deletes the stored
        // opt-out on a get->set round trip. Same exemption setAssignmentEntry
        // gives the word.
        if (!layoutId.isEmpty() && requiresManualLayoutValidation(layoutId)) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch desktop assignment"));
            if (!uuidOpt) {
                continue;
            }
        }
        const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenIdOrName);
        parsedAssignments[qMakePair(resolvedId, virtualDesktop)] = layoutId;
        m_changedScreenIds.insert(resolvedId);
    }

    m_layoutManager->setAllDesktopAssignments(parsedAssignments);
    qCInfo(lcDbusLayout) << "Batch set" << parsedAssignments.size() << "desktop assignments";
}

void LayoutAdaptor::setAllActivityAssignments(const QVariantMap& assignments)
{
    markScreensWithStoredAssignments(AssignmentFamily::Activity);
    QHash<QPair<QString, QString>, QString> parsedAssignments;

    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        // Split on '|' delimiter (screen IDs contain colons, so ':' is not safe)
        int sep = it.key().indexOf(QLatin1Char('|'));
        if (sep < 0) {
            // Backward compat: try legacy ':' delimiter for old configs.
            // Use lastIndexOf because activity IDs are UUIDs (contain hyphens, no colons),
            // so the last ':' correctly separates "DEL:DELL U2722D:115107:activity-uuid"
            // into screen ID + activity. For connector-name keys ("DP-2:activity-uuid"),
            // lastIndexOf also works correctly since there's only one ':'.
            // New KCM always sends '|', so this path only triggers for pre-migration data.
            sep = it.key().lastIndexOf(QLatin1Char(':'));
        }
        if (sep < 1) {
            qCWarning(lcDbusLayout) << "Invalid activity assignment key format:" << it.key();
            continue;
        }

        QString screenIdOrName = it.key().left(sep);
        QString activityId = it.key().mid(sep + 1);
        if (screenIdOrName.isEmpty() || activityId.isEmpty()) {
            qCWarning(lcDbusLayout) << "Empty screen or activity in assignment key:" << it.key();
            continue;
        }

        QString layoutId = it.value().toString();
        // The reserved snapping opt-out word is a storable value, not a UUID:
        // rejecting it here is DATA LOSS, because the paired getter emits it
        // and the batch apply drops the whole rule family before this
        // validation re-adds entries — a skipped key deletes the stored
        // opt-out on a get->set round trip. Same exemption setAssignmentEntry
        // gives the word.
        if (!layoutId.isEmpty() && requiresManualLayoutValidation(layoutId)) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch activity assignment"));
            if (!uuidOpt) {
                continue;
            }
        }
        const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenIdOrName);
        parsedAssignments[qMakePair(resolvedId, activityId)] = layoutId;
        m_changedScreenIds.insert(resolvedId);
    }

    m_layoutManager->setAllActivityAssignments(parsedAssignments);
    qCInfo(lcDbusLayout) << "Batch set" << parsedAssignments.size() << "activity assignments";
}

void LayoutAdaptor::setAllCombinedAssignments(const QVariantMap& assignments)
{
    markScreensWithStoredAssignments(AssignmentFamily::Combined);
    QHash<PhosphorZones::CombinedAssignmentKey, QString> parsed;
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        const QString& rawKey = it.key();
        // Split on '|' boundaries — exactly three segments
        // (screen, desktop, activity). Anything else is malformed.
        const int firstSep = rawKey.indexOf(QLatin1Char('|'));
        const int secondSep = (firstSep >= 0) ? rawKey.indexOf(QLatin1Char('|'), firstSep + 1) : -1;
        if (firstSep < 1 || secondSep <= firstSep + 1 || secondSep == rawKey.size() - 1) {
            qCWarning(lcDbusLayout) << "Invalid combined assignment key format:" << rawKey;
            continue;
        }
        const QString screenIdOrName = rawKey.left(firstSep);
        bool ok = false;
        const int virtualDesktop = rawKey.mid(firstSep + 1, secondSep - firstSep - 1).toInt(&ok);
        const QString activityId = rawKey.mid(secondSep + 1);
        if (!ok || virtualDesktop <= 0 || activityId.isEmpty()) {
            qCWarning(lcDbusLayout) << "Invalid combined assignment key fields:" << rawKey;
            continue;
        }
        const QString layoutId = it.value().toString();
        // The reserved snapping opt-out word is a storable value, not a UUID:
        // rejecting it here is DATA LOSS, because the paired getter emits it
        // and the batch apply drops the whole rule family before this
        // validation re-adds entries — a skipped key deletes the stored
        // opt-out on a get->set round trip. Same exemption setAssignmentEntry
        // gives the word.
        if (!layoutId.isEmpty() && requiresManualLayoutValidation(layoutId)) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch combined assignment"));
            if (!uuidOpt) {
                continue;
            }
        }
        const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenIdOrName);
        parsed.insert(PhosphorZones::CombinedAssignmentKey{resolvedId, virtualDesktop, activityId}, layoutId);
        m_changedScreenIds.insert(resolvedId);
    }
    m_layoutManager->setAllCombinedAssignments(parsed);
    qCInfo(lcDbusLayout) << "Batch set" << parsed.size() << "combined assignments";
}
} // namespace PlasmaZones
