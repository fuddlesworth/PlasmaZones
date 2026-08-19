// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layoutadaptor.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/LayoutRegistry.h>
#include <PhosphorZones/ScrollingTemplateStore.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/ActivityManager.h>
#include "core/platform/logging.h"
#include "core/utils/utils.h"
#include "core/types/constants.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QScreen>
#include <QSet>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

namespace {
/// D-Bus boundary guard shared by every per-desktop slot in this file:
/// 0 = the base (all-desktops) context, negatives are never a valid desktop
/// number. The batch setters carry their own equivalent checks.
bool validDesktopArg(int virtualDesktop, const char* method)
{
    if (virtualDesktop < 0) {
        qCWarning(lcDbusLayout) << method << "- negative virtualDesktop" << virtualDesktop;
        return false;
    }
    return true;
}
} // namespace

QJsonObject LayoutAdaptor::buildActivityInfoJson(const QString& activityId) const
{
    QJsonObject info;
    info[QLatin1String("id")] = activityId;
    if (m_activityManager) {
        info[QLatin1String("name")] = m_activityManager->activityName(activityId);
        info[QLatin1String("icon")] = m_activityManager->activityIcon(activityId);
    }
    return info;
}

// Virtual Desktop Query
int LayoutAdaptor::getCurrentVirtualDesktop()
{
    return m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
}

// Screen Assignments
// NOTE: Individual assignment methods do not call saveAssignments() directly.
// PhosphorZones::LayoutRegistry auto-persists in assignLayout(), assignLayoutById(),
// clearAssignment(), setAssignmentEntryDirect(), and the batch setAll*() methods.
QString LayoutAdaptor::getLayoutForScreen(const QString& screenId)
{
    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    // Per-output virtual desktops (#648): resolve the queried screen's own
    // desktop, not the active monitor's, so getLayoutForScreen(screenA) issued
    // while screenB is focused still returns screenA's layout.
    int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(resolvedId) : 0;
    QString activity = m_activityManager ? m_activityManager->currentActivity() : QString();

    // Check for autotile assignment first (layoutForScreen returns nullptr for autotile)
    QString assignmentId = m_layoutManager->assignmentIdForScreen(resolvedId, desktop, activity);
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId) || PhosphorLayout::LayoutId::isScrolling(assignmentId)) {
        // Autotile ids and the bare scrolling sentinel have no snap Layout*
        // to resolve; report the id itself so the read path agrees with
        // what the batch setters accept.
        return assignmentId;
    }

    auto* layout = m_layoutManager->layoutForScreen(resolvedId, desktop, activity);
    return layout ? layout->id().toString() : QString();
}

QString LayoutAdaptor::getAssignedLayoutForScreen(const QString& screenId)
{
    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    // Same context resolution as getLayoutForScreen: the queried screen's own
    // desktop plus the current activity.
    int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(resolvedId) : 0;
    QString activity = m_activityManager ? m_activityManager->currentActivity() : QString();

    // Cascade only — empty means the screen has no assignment of its own and
    // the caller must NOT be handed the registry-wide default (the editor
    // would otherwise open the default layout for in-place editing and a
    // save would overwrite it; discussion #858).
    return m_layoutManager->storedAssignmentIdForScreen(resolvedId, desktop, activity);
}

void LayoutAdaptor::assignLayoutToScreen(const QString& screenId, const QString& layoutId)
{
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("assign layout"))) {
        return;
    }

    // For manual layouts, validate UUID and verify layout exists
    PhosphorZones::Layout* layout = nullptr;
    if (!PhosphorLayout::LayoutId::isAutotile(layoutId) && !PhosphorLayout::LayoutId::isScrolling(layoutId)) {
        layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen"));
        if (!layout) {
            return;
        }
    }

    // Warn if screen name is not in the daemon's screen list (e.g. script using wrong name)
    if (!PhosphorScreens::ScreenIdentity::findByIdOrName(screenId)) {
        qCWarning(lcDbusLayout)
            << "assignLayoutToScreen: screen name" << screenId
            << "not found in daemon's screen list. Use org.plasmazones.Screen.getScreens for valid names.";
    }

    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    m_layoutManager->assignLayoutById(resolvedId, 0, QString(), layoutId);
    m_changedScreenIds.insert(resolvedId);

    // Update global active layout when assigning to the primary screen (manual layouts only)
    if (layout) {
        QScreen* primary = Utils::primaryScreen();
        if (primary && PhosphorScreens::ScreenIdentity::identifierFor(primary) == resolvedId) {
            m_layoutManager->setActiveLayout(layout);
        }
    }

    qCInfo(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenId << "(id:" << resolvedId << ")";
}

void LayoutAdaptor::clearAssignment(const QString& screenId)
{
    // Bus boundary: guard the screen name like every assignLayout* sibling does.
    // ScreenIdentity::idForName("") returns "" unchanged, so without this an empty
    // id reached clearAssignment and then shipped as an empty entry in the
    // assignmentChangesApplied list.
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("clear assignment"))) {
        return;
    }
    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    m_layoutManager->clearAssignment(resolvedId);
    m_changedScreenIds.insert(resolvedId);
}

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
        if (!layoutId.isEmpty() && !PhosphorLayout::LayoutId::isAutotile(layoutId)
            && !PhosphorLayout::LayoutId::isScrolling(layoutId)) {
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

QStringList LayoutAdaptor::getAvailableScreenIds()
{
    return m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList();
}

QString LayoutAdaptor::getAllScreenAssignments()
{
    QJsonObject root;
    const int desktopCount = getVirtualDesktopCount();

    // Use effective screen IDs (includes virtual screens when configured)
    // so the KCM sees one entry per virtual screen, not per physical monitor.
    const QStringList screenIds = (m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList());

    for (const QString& screenId : std::as_const(screenIds)) {
        // Walk the screen's stored entries first; only allocate JSON +
        // resolve the connector key if at least one is present. Screen
        // enumeration moved to getAvailableScreenIds(), so this method
        // can stay narrowly scoped to *stored* assignment state without
        // emitting empty objects for unconfigured screens.
        QJsonObject screenObj;
        bool hasAnyStored = false;

        // Per-screen base entry — only emit when explicitly stored.
        // assignmentEntryForScreen would happily synthesize the user's
        // global default here (per-368 fallback), but this JSON is the
        // KCM's view of *stored* state, which it stages and writes back
        // through the per-context setters (this shape is not what
        // setAllScreenAssignments accepts, so it is a readback, not a
        // round-trip). Reporting the synthesized default as stored would
        // shadow future global-default changes on that write-back and
        // defeat the "synthesized fallback only" intent.
        if (m_layoutManager->hasExplicitAssignment(screenId, 0, QString())) {
            hasAnyStored = true;
            auto entry = m_layoutManager->assignmentEntryForScreen(screenId, 0, QString());
            QString effectiveId = entry.activeLayoutId();
            if (!effectiveId.isEmpty()) {
                screenObj[QLatin1String("default")] = effectiveId;
            }
            // Expose all payload fields so the KCM can populate snapping,
            // tiling AND scrolling-template assignments
            if (!entry.snappingLayout.isEmpty()) {
                screenObj[QLatin1String("snappingLayout")] = entry.snappingLayout;
            }
            if (!entry.tilingAlgorithm.isEmpty()) {
                screenObj[QLatin1String("tilingAlgorithm")] = entry.tilingAlgorithm;
            }
            if (!entry.scrollingTemplateLayout.isEmpty()) {
                screenObj[QLatin1String("scrollingTemplate")] = entry.scrollingTemplateLayout;
            }
            screenObj[QLatin1String("mode")] = static_cast<int>(entry.mode);
        }

        // Per-desktop entries (desktop > 0) — only include explicitly assigned
        // desktops, not inherited base defaults.  Without this guard every
        // desktop row in the KCM would show the display default redundantly.
        for (int desktop = 1; desktop <= desktopCount; ++desktop) {
            if (m_layoutManager->hasExplicitAssignment(screenId, desktop, QString())) {
                QString desktopId = m_layoutManager->assignmentIdForScreen(screenId, desktop, QString());
                if (!desktopId.isEmpty()) {
                    hasAnyStored = true;
                    screenObj[QString::number(desktop)] = desktopId;
                }
            }
        }

        if (!hasAnyStored) {
            continue;
        }

        // Derive connector name for the JSON key (KCM compatibility).
        // Virtual screens use their full ID directly (e.g., "physId/vs:0");
        // physical screens use the QScreen connector name for KCM parity.
        QString connectorName;
        if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
            connectorName = screenId;
        } else {
            QScreen* physScreen = PhosphorScreens::ScreenIdentity::findByIdOrName(screenId);
            connectorName = physScreen ? physScreen->name() : screenId;
        }
        screenObj[QLatin1String("screenId")] = screenId;
        root[connectorName] = screenObj;
    }

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// Per-Virtual-Desktop Assignments
QString LayoutAdaptor::getLayoutForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    if (!validDesktopArg(virtualDesktop, "getLayoutForScreenDesktop")) {
        return QString();
    }
    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    QString assignmentId = m_layoutManager->assignmentIdForScreen(resolvedId, virtualDesktop, QString());
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId) || PhosphorLayout::LayoutId::isScrolling(assignmentId)) {
        // Autotile ids and the bare scrolling sentinel have no snap Layout*
        // to resolve; report the id itself so the read path agrees with
        // what the batch setters accept.
        return assignmentId;
    }
    auto* layout = m_layoutManager->layoutForScreen(resolvedId, virtualDesktop, QString());
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreenDesktop(const QString& screenId, int virtualDesktop, const QString& layoutId)
{
    if (!validDesktopArg(virtualDesktop, "assignLayoutToScreenDesktop")) {
        return;
    }
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("assign layout to desktop"))) {
        return;
    }
    if (!validateDesktopNumber(virtualDesktop, QStringLiteral("assign layout to desktop"))) {
        return;
    }

    // Validate UUID for manual layouts, skip for autotile IDs
    if (!PhosphorLayout::LayoutId::isAutotile(layoutId) && !PhosphorLayout::LayoutId::isScrolling(layoutId)) {
        auto* layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen desktop"));
        if (!layout) {
            return;
        }
    }

    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    m_layoutManager->assignLayoutById(resolvedId, virtualDesktop, QString(), layoutId);
    m_changedScreenIds.insert(resolvedId);
    qCInfo(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenId << "(id:" << resolvedId
                         << ") on desktop" << virtualDesktop;

    // PhosphorZones::Layout resolution is triggered by PhosphorZones::LayoutRegistry::layoutAssigned signal
    // → daemon's syncModeFromAssignments(). No direct updateActiveLayout() needed.
}

void LayoutAdaptor::clearAssignmentForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("clear assignment"))) {
        return;
    }
    if (!validDesktopArg(virtualDesktop, "clearAssignmentForScreenDesktop")) {
        return;
    }
    const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    m_layoutManager->clearAssignment(resolvedId, virtualDesktop, QString());
    m_changedScreenIds.insert(resolvedId);
    qCInfo(lcDbusLayout) << "Cleared assignment for screen" << screenId << "on desktop" << virtualDesktop;
}

bool LayoutAdaptor::hasExplicitAssignmentForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    if (!validDesktopArg(virtualDesktop, "hasExplicitAssignmentForScreenDesktop")) {
        return false;
    }
    return m_layoutManager->hasExplicitAssignment(PhosphorScreens::ScreenIdentity::idForName(screenId), virtualDesktop,
                                                  QString());
}

int LayoutAdaptor::getModeForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    if (!validDesktopArg(virtualDesktop, "getModeForScreenDesktop")) {
        return static_cast<int>(PhosphorZones::AssignmentEntry::Snapping);
    }
    return static_cast<int>(m_layoutManager->modeForScreen(PhosphorScreens::ScreenIdentity::idForName(screenId),
                                                           virtualDesktop, QString()));
}

QString LayoutAdaptor::getSnappingLayoutForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    if (!validDesktopArg(virtualDesktop, "getSnappingLayoutForScreenDesktop")) {
        return QString();
    }
    return m_layoutManager->snappingLayoutForScreen(PhosphorScreens::ScreenIdentity::idForName(screenId),
                                                    virtualDesktop, QString());
}

QString LayoutAdaptor::getTilingAlgorithmForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    if (!validDesktopArg(virtualDesktop, "getTilingAlgorithmForScreenDesktop")) {
        return QString();
    }
    return m_layoutManager->tilingAlgorithmForScreen(PhosphorScreens::ScreenIdentity::idForName(screenId),
                                                     virtualDesktop, QString());
}

QString LayoutAdaptor::getScreenStates()
{
    // No m_layoutManager guard: both constructors qFatal on a null manager,
    // and every other slot in this file dereferences it unguarded.
    QJsonArray result;

    // Resolve the context from the LIVE managers, matching getLayoutForScreen and
    // getAssignedLayoutForScreen above rather than the registry's pushed mirrors.
    // The mirrors are written from exactly one place each (the daemon's
    // desktop-switch and activity-switch handlers), so between a real switch and
    // that handler running they lag — and this readback would then describe the
    // previous desktop while its file siblings, and the published active-layout
    // snapshot, already describe the new one. One source per adaptor.
    const QString activity = m_activityManager ? m_activityManager->currentActivity() : QString();

    // Use effective screen IDs (includes virtual screens when configured)
    // so the settings app sees one entry per virtual screen, not per physical monitor.
    const QStringList screenIds = (m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList());

    for (const QString& screenId : std::as_const(screenIds)) {
        // Per-output virtual desktops (#648): each screen resolves its own desktop.
        // The VDM-less fallback (desktop stays 0, the base cascade level) is a
        // test-fixture configuration: the daemon always wires a manager.
        const int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktopForScreen(screenId) : 0;
        const auto entry = m_layoutManager->assignmentEntryForScreen(screenId, desktop, activity);

        QJsonObject obj;
        obj[QLatin1String("screenId")] = screenId;
        obj[QLatin1String("virtualDesktop")] = desktop;
        obj[QLatin1String("activity")] = activity;
        obj[QLatin1String("mode")] = static_cast<int>(entry.mode);

        // Snapping layout — use the resolved layout (includes the default
        // fallback), EXCEPT when the default is suppressed for this context
        // or the context is Scrolling: for Scrolling, layoutForScreen would
        // fall through its settled-nullptr arm to defaultLayout() and the
        // monitor view would render a layout the screen does not use.
        PhosphorZones::Layout* resolvedLayout =
            (entry.mode == PhosphorZones::AssignmentEntry::Scrolling
             || m_layoutManager->isContextActiveLayoutSuppressed(screenId, desktop, activity))
            ? nullptr
            : m_layoutManager->layoutForScreen(screenId, desktop, activity);
        if (resolvedLayout) {
            obj[QLatin1String("layoutId")] = resolvedLayout->id().toString();
            obj[QLatin1String("layoutName")] = resolvedLayout->name();
        } else {
            obj[QLatin1String("layoutId")] = QString();
            obj[QLatin1String("layoutName")] = QString();
        }

        // Per-slot explicit markers: whether THIS exact context tuple pins
        // the field, as opposed to the resolved value arriving through the
        // cascade/default. The Monitors page gates its lossless mode-toggle
        // sibling carry on these, so a pure mode switch can never freeze a
        // cascade default into an explicit assignment.
        //
        // exactContextEntry scans the assignment list, so this loop is O(screens
        // × assignments). Left as is on purpose: getScreenStates is a
        // settings-UI / shortcut-rate call over a handful of screens, and an
        // index would add a cache to keep in sync with every assignment write
        // for no measurable gain.
        const PhosphorZones::AssignmentEntry exactEntry =
            m_layoutManager->exactContextEntry(screenId, desktop, activity);
        obj[QLatin1String("layoutIdExplicit")] = !exactEntry.snappingLayout.isEmpty();
        obj[QLatin1String("algorithmIdExplicit")] = !exactEntry.tilingAlgorithm.isEmpty();

        // Scrolling template — read the RAW entry field, not the mode-gated
        // scrollingTemplateForContext resolver: a template PRESERVED across a
        // mode toggle (the lossless contract) is exactly the state the
        // Monitors page needs to show, and the resolver deliberately answers
        // nullptr for non-Scrolling contexts.
        //
        // Deliberate divergence from getScrollingTemplateLayout: the raw field
        // also skips the resolver's configured DEFAULT-template fallback, so a
        // context that pins no template of its own reports empty here while the
        // strip runs the default. The page needs "what this context stores" to
        // drive its explicit/inherited affordances, not the resolved value.
        obj[QLatin1String("scrollingTemplateId")] = entry.scrollingTemplateLayout;
        if (!entry.scrollingTemplateLayout.isEmpty()) {
            // Name from the native template STORE (templates are no longer
            // layouts). A deleted template answers an empty name, which is
            // what tells the Monitors page it is looking at a context pinned
            // to a template the store no longer holds: a non-empty id beside
            // an empty name is that state and nothing else, and the page says
            // so in prose rather than printing the raw id.
            const PhosphorZones::ScrollingTemplateStore* store = m_layoutManager->scrollingTemplateStore();
            obj[QLatin1String("scrollingTemplateName")] =
                store ? store->templateById(QUuid::fromString(entry.scrollingTemplateLayout)).name : QString();
        } else {
            obj[QLatin1String("scrollingTemplateName")] = QString();
        }
        obj[QLatin1String("scrollingTemplateExplicit")] = !exactEntry.scrollingTemplateLayout.isEmpty();

        // Tiling algorithm — use resolved algorithm (includes fallback)
        const QString algoId = m_layoutManager->tilingAlgorithmForScreen(screenId, desktop, activity);
        obj[QLatin1String("algorithmId")] = algoId;
        // Null-tolerant: D-Bus clients can hit getScreenStates before the
        // composition root finishes wiring setAlgorithmRegistry. Mirror the
        // null-fallback the layoutId branch above does (algoId without a
        // registry yields an empty algorithmName rather than a crash).
        if (!algoId.isEmpty() && m_algorithmRegistry) {
            PhosphorTiles::TilingAlgorithm* algo = m_algorithmRegistry->algorithm(algoId);
            obj[QLatin1String("algorithmName")] = algo ? algo->name() : algoId;
        } else {
            obj[QLatin1String("algorithmName")] = QString();
        }

        result.append(obj);
    }

    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
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
        if (!layoutId.isEmpty() && !PhosphorLayout::LayoutId::isAutotile(layoutId)
            && !PhosphorLayout::LayoutId::isScrolling(layoutId)) {
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

QVariantMap LayoutAdaptor::getActiveLayoutsForScreens()
{
    QVariantMap result;
    for (auto it = m_activeAssignmentByScreen.constBegin(); it != m_activeAssignmentByScreen.constEnd(); ++it) {
        // Omit screens that resolve to nothing rather than shipping an empty
        // string. This is map hygiene, not inertness: ActiveLayout is a
        // non-optional context field, so a reader CANNOT leave it unset — an
        // absent entry and an empty entry both end up compared as an empty
        // string on the far side. What omission buys is that no consumer has to
        // decide what an empty payload means, and the map stays a list of
        // screens that actually resolve a layout.
        //
        // Note what stays true either way: a negated leaf (`NotEquals` /
        // `DoesNotContain`) still matches the engaged-empty value. Only the
        // `Equals ""` shape is closed, and that is closed at authoring time by
        // MatchExpression::isValid rejecting an empty-Equals context-string
        // leaf, not here.
        if (!it.value().isEmpty()) {
            result.insert(it.key(), it.value());
        }
    }
    return result;
}

void LayoutAdaptor::publishActiveAssignments(const QHash<QString, QString>& activeByScreen,
                                             const QSet<QString>& changedScreenIds)
{
    // Capture the values to broadcast BEFORE the snapshot is replaced, so the
    // per-screen comparison below sees the old value, and so the loop reads a
    // local rather than the member it is about to broadcast from (each emission
    // costs the effect a rule-cache invalidation and a decoration sweep, and a
    // subscriber is free to call back into this adaptor).
    QList<std::pair<QString, QString>> toEmit;
    toEmit.reserve(changedScreenIds.size());
    for (const QString& screenId : changedScreenIds) {
        // A screen dropped from the snapshot (monitor unplugged) broadcasts an
        // empty id, which tells subscribers to forget their cached value rather
        // than keep matching against a layout that is no longer on that screen.
        const QString newValue = activeByScreen.value(screenId);
        // The daemon computes changedScreenIds by diffing, so an unchanged
        // screen should not be here at all. Comparing anyway keeps the emission
        // contract local to this method: a caller that over-reports (a future
        // one, or a test) cannot make a subscriber invalidate for a value that
        // did not move.
        if (newValue != m_activeAssignmentByScreen.value(screenId)) {
            toEmit.append({screenId, newValue});
        }
    }

    // Snapshot first, broadcast second: a subscriber that reacts to the signal
    // by calling getActiveLayoutsForScreens must not read the pre-change map.
    m_activeAssignmentByScreen = activeByScreen;
    for (const auto& [screenId, layoutId] : std::as_const(toEmit)) {
        Q_EMIT activeLayoutForScreenChanged(screenId, layoutId);
    }
}

QVariantMap LayoutAdaptor::getAllDesktopAssignments()
{
    QVariantMap result;

    const auto assignments = m_layoutManager->desktopAssignments();
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        // Two-arg `.arg(QString, QString)` for symmetry with
        // getAllActivityAssignments below; convert the desktop int to
        // a string up-front so both helpers use the same call shape.
        QString key = QStringLiteral("%1|%2").arg(it.key().first, QString::number(it.key().second));
        result[key] = QVariantMap{{QStringLiteral("layoutId"), it.value().activeLayoutId()},
                                  {QStringLiteral("scrollingTemplate"), it.value().scrollingTemplateLayout}};
    }

    return result;
}

QVariantMap LayoutAdaptor::getAllActivityAssignments()
{
    QVariantMap result;

    const auto assignments = m_layoutManager->activityAssignments();
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        QString key = QStringLiteral("%1|%2").arg(it.key().first, it.key().second);
        result[key] = QVariantMap{{QStringLiteral("layoutId"), it.value().activeLayoutId()},
                                  {QStringLiteral("scrollingTemplate"), it.value().scrollingTemplateLayout}};
    }

    return result;
}

// KDE Activities Support
void LayoutAdaptor::setActivityManager(PhosphorWorkspaces::ActivityManager* am)
{
    if (m_activityManager) {
        disconnect(m_activityManager, nullptr, this, nullptr);
    }

    m_activityManager = am;
    connectActivitySignals();
}

void LayoutAdaptor::connectActivitySignals()
{
    if (m_activityManager) {
        connect(m_activityManager, &PhosphorWorkspaces::ActivityManager::currentActivityChanged, this,
                &LayoutAdaptor::currentActivityChanged);
        connect(m_activityManager, &PhosphorWorkspaces::ActivityManager::activitiesChanged, this,
                &LayoutAdaptor::activitiesChanged);
    }
}

bool LayoutAdaptor::isActivitiesAvailable()
{
    return PhosphorWorkspaces::ActivityManager::isAvailable();
}

QStringList LayoutAdaptor::getActivities()
{
    return m_activityManager ? m_activityManager->activities() : QStringList{};
}

QString LayoutAdaptor::getCurrentActivity()
{
    return m_activityManager ? m_activityManager->currentActivity() : QString();
}

QString LayoutAdaptor::getActivityInfo(const QString& activityId)
{
    if (!m_activityManager || activityId.isEmpty()) {
        return QStringLiteral("{}");
    }

    return QString::fromUtf8(QJsonDocument(buildActivityInfoJson(activityId)).toJson(QJsonDocument::Compact));
}

QString LayoutAdaptor::getAllActivitiesInfo()
{
    QJsonArray array;

    if (m_activityManager) {
        const auto activityIds = m_activityManager->activities();
        for (const QString& activityId : activityIds) {
            array.append(buildActivityInfoJson(activityId));
        }
    }

    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

// Per-Activity Assignments
QString LayoutAdaptor::getLayoutForScreenActivity(const QString& screenId, const QString& activityId)
{
    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    QString assignmentId = m_layoutManager->assignmentIdForScreen(resolvedId, 0, activityId);
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId) || PhosphorLayout::LayoutId::isScrolling(assignmentId)) {
        // Autotile ids and the bare scrolling sentinel have no snap Layout*
        // to resolve; report the id itself so the read path agrees with
        // what the batch setters accept.
        return assignmentId;
    }
    auto* layout = m_layoutManager->layoutForScreen(resolvedId, 0, activityId);
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreenActivity(const QString& screenId, const QString& activityId,
                                                 const QString& layoutId)
{
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("assign layout to activity"))) {
        return;
    }
    if (!validateNonEmpty(activityId, QStringLiteral("activity ID"), QStringLiteral("assign layout to activity"))) {
        return;
    }

    // Validate UUID for manual layouts, skip for autotile IDs
    if (!PhosphorLayout::LayoutId::isAutotile(layoutId) && !PhosphorLayout::LayoutId::isScrolling(layoutId)) {
        auto* layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen activity"));
        if (!layout) {
            return;
        }
    }

    const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    m_layoutManager->assignLayoutById(resolvedId, 0, activityId, layoutId);
    m_changedScreenIds.insert(resolvedId);

    qCInfo(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenId << "for activity" << activityId;

    // PhosphorZones::Layout resolution is triggered by PhosphorZones::LayoutRegistry::layoutAssigned signal
    // → daemon's syncModeFromAssignments(). No direct updateActiveLayout() needed.
}

void LayoutAdaptor::clearAssignmentForScreenActivity(const QString& screenId, const QString& activityId)
{
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("clear assignment"))) {
        return;
    }
    // Mirror the assign sibling: an empty activity is not this family's key, it
    // is the base (monitor-only) key, so clearing on it would drop a rule the
    // caller did not name.
    if (!validateNonEmpty(activityId, QStringLiteral("activity ID"), QStringLiteral("clear assignment"))) {
        return;
    }
    const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    m_layoutManager->clearAssignment(resolvedId, 0, activityId);
    m_changedScreenIds.insert(resolvedId);
    qCInfo(lcDbusLayout) << "Cleared assignment for screen" << screenId << "activity" << activityId;
}

bool LayoutAdaptor::hasExplicitAssignmentForScreenActivity(const QString& screenId, const QString& activityId)
{
    return m_layoutManager->hasExplicitAssignment(PhosphorScreens::ScreenIdentity::idForName(screenId), 0, activityId);
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
        if (!layoutId.isEmpty() && !PhosphorLayout::LayoutId::isAutotile(layoutId)
            && !PhosphorLayout::LayoutId::isScrolling(layoutId)) {
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

QVariantMap LayoutAdaptor::getAllCombinedAssignments()
{
    QVariantMap result;
    const auto assignments = m_layoutManager->combinedAssignments();
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        // Emit only FULL triples: the setter rejects desktop <= 0 / empty
        // activity, so a base-context entry surfaced here would be silently
        // dropped on a client's read-modify-write round trip. Base-context
        // assignments travel through their own per-axis getters instead.
        if (it.key().virtualDesktop <= 0 || it.key().activity.isEmpty()) {
            continue;
        }
        // Key format mirrors the setter: "screen|desktop|activity". '|' is
        // safe because screen ids may carry ':' (manufacturer:model:serial)
        // and activity ids are UUIDs.
        const QString key =
            QStringLiteral("%1|%2|%3").arg(it.key().screenId).arg(it.key().virtualDesktop).arg(it.key().activity);
        result[key] = QVariantMap{{QStringLiteral("layoutId"), it.value().activeLayoutId()},
                                  {QStringLiteral("scrollingTemplate"), it.value().scrollingTemplateLayout}};
    }
    return result;
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
        if (!layoutId.isEmpty() && !PhosphorLayout::LayoutId::isAutotile(layoutId)
            && !PhosphorLayout::LayoutId::isScrolling(layoutId)) {
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

// Full Assignments (Screen + Desktop + Activity)
QString LayoutAdaptor::getLayoutForScreenDesktopActivity(const QString& screenId, int virtualDesktop,
                                                         const QString& activityId)
{
    if (!validDesktopArg(virtualDesktop, "getLayoutForScreenDesktopActivity")) {
        return QString();
    }
    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    QString assignmentId = m_layoutManager->assignmentIdForScreen(resolvedId, virtualDesktop, activityId);
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId) || PhosphorLayout::LayoutId::isScrolling(assignmentId)) {
        // Autotile ids and the bare scrolling sentinel have no snap Layout*
        // to resolve; report the id itself so the read path agrees with
        // what the batch setters accept.
        return assignmentId;
    }
    auto* layout = m_layoutManager->layoutForScreen(resolvedId, virtualDesktop, activityId);
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreenDesktopActivity(const QString& screenId, int virtualDesktop,
                                                        const QString& activityId, const QString& layoutId)
{
    if (!validDesktopArg(virtualDesktop, "assignLayoutToScreenDesktopActivity")) {
        return;
    }
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("assign layout"))) {
        return;
    }
    if (!validateDesktopNumber(virtualDesktop, QStringLiteral("assign layout"))) {
        return;
    }
    // The Combined family is defined by all three dimensions being pinned
    // (isValidCombinedKey, and setAllCombinedAssignments requires it). An empty
    // activity here writes a Desktop-family rule under a Combined-family slot
    // name, which the combined reader then never reports back.
    if (!validateNonEmpty(activityId, QStringLiteral("activity ID"), QStringLiteral("assign layout"))) {
        return;
    }

    // Validate UUID for manual layouts, skip for autotile IDs
    if (!PhosphorLayout::LayoutId::isAutotile(layoutId) && !PhosphorLayout::LayoutId::isScrolling(layoutId)) {
        auto* layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen desktop activity"));
        if (!layout) {
            return;
        }
    }

    const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    m_layoutManager->assignLayoutById(resolvedId, virtualDesktop, activityId, layoutId);
    m_changedScreenIds.insert(resolvedId);

    qCInfo(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenId << "desktop" << virtualDesktop
                         << "activity" << activityId;

    // PhosphorZones::Layout resolution is triggered by PhosphorZones::LayoutRegistry::layoutAssigned signal
    // → daemon's syncModeFromAssignments(). No direct updateActiveLayout() needed.
}

void LayoutAdaptor::clearAssignmentForScreenDesktopActivity(const QString& screenId, int virtualDesktop,
                                                            const QString& activityId)
{
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("clear assignment"))) {
        return;
    }
    if (!validDesktopArg(virtualDesktop, "clearAssignmentForScreenDesktopActivity")) {
        return;
    }
    // Same Combined-family key requirement as the assign sibling: an empty
    // activity names the Desktop family, whose rule this slot must not clear.
    if (!validateNonEmpty(activityId, QStringLiteral("activity ID"), QStringLiteral("clear assignment"))) {
        return;
    }
    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    m_layoutManager->clearAssignment(resolvedId, virtualDesktop, activityId);
    m_changedScreenIds.insert(resolvedId);
    qCInfo(lcDbusLayout) << "Cleared assignment for screen" << screenId << "desktop" << virtualDesktop << "activity"
                         << activityId;
}

void LayoutAdaptor::setScrollingTemplateLayout(const QString& screenId, int virtualDesktop, const QString& activityId,
                                               const QString& layoutId)
{
    // idForName passthrough: a connector name that matches no known screen
    // comes back verbatim and IS stored — the same contract as every sibling
    // assignment setter here, where a not-yet-connected monitor's assignment
    // must survive until the screen appears.
    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    if (resolvedId.isEmpty()) {
        qCWarning(lcDbusLayout) << "setScrollingTemplateLayout: empty screen ID for" << screenId;
        return;
    }
    if (!validDesktopArg(virtualDesktop, "setScrollingTemplateLayout")) {
        return;
    }
    // Empty clears the context's template, which makes the screen INHERIT the
    // configured default again — every sibling slot setter has a clear form
    // and clearAssignmentForScreenDesktopActivity would wipe the whole
    // entry, which is the wrong tool for "just drop the template".
    //
    // The reserved PhosphorZones::NoScrollingTemplate word is the third form
    // and passes the id check below untouched: it is stored verbatim and
    // means "explicitly no template", which the resolver honours over the
    // default. Validating it as a UUID would refuse the only spelling of
    // that state.
    //
    // The clear is NOT mode-neutral: assignScrollingTemplate stamps
    // AssignmentEntry::Scrolling on the entry it upserts whatever the
    // template id is, so clearing on a Snapping context flips it to
    // Scrolling and materializes an entry that was not there before. The
    // published DocString states this; leaving the mode alone here would
    // desync the clear form from the assigning form, which is why the
    // side effect is contractual rather than guarded.
    if (!layoutId.isEmpty() && layoutId != PhosphorZones::NoScrollingTemplate) {
        // A non-empty id must name an existing native ScrollingTemplate:
        // unlike the mode-only assignment setters there is no sentinel form
        // to accept, and an unknown UUID passed through to the registry
        // would be stored as the explicit no-template word, silently
        // replacing the context's existing choice with an opt-out the caller
        // never asked for. Refusing HERE keeps the prior template.
        //
        // The existence half is store-gated, matching the registry's own
        // check: with no store wired (unit fixtures, embedders) only the
        // UUID format is enforced and a well-formed unknown id IS stored.
        // It stays inert — scrollingTemplateForContext resolves it through
        // the same absent store and answers no template.
        const QUuid parsed = QUuid::fromString(layoutId);
        const PhosphorZones::ScrollingTemplateStore* store = m_layoutManager->scrollingTemplateStore();
        if (parsed.isNull() || (store && !store->contains(parsed))) {
            qCWarning(lcDbusLayout) << "setScrollingTemplateLayout: unknown template" << layoutId << "— refusing";
            return;
        }
    }
    m_layoutManager->assignScrollingTemplate(resolvedId, virtualDesktop, activityId, layoutId);
    m_changedScreenIds.insert(resolvedId);
    qCInfo(lcDbusLayout) << (layoutId.isEmpty() ? "Cleared scrolling template" : "Set scrolling template") << layoutId
                         << "for screen" << screenId << "desktop" << virtualDesktop << "activity" << activityId;
}

QString LayoutAdaptor::getScrollingTemplateLayout(const QString& screenId, int virtualDesktop,
                                                  const QString& activityId)
{
    const QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    if (resolvedId.isEmpty()) {
        qCWarning(lcDbusLayout) << "getScrollingTemplateLayout: empty screen ID for" << screenId;
        return QString();
    }
    if (!validDesktopArg(virtualDesktop, "getScrollingTemplateLayout")) {
        return QString();
    }
    const PhosphorZones::ScrollingTemplate templ =
        m_layoutManager->scrollingTemplateForContext(resolvedId, virtualDesktop, activityId);
    return templ.isValid() ? templ.id.toString() : QString();
}

QString LayoutAdaptor::getScrollingTemplates()
{
    const PhosphorZones::ScrollingTemplateStore* store = m_layoutManager->scrollingTemplateStore();
    if (!store) {
        return QStringLiteral("[]");
    }
    QJsonArray array;
    const QList<PhosphorZones::ScrollingTemplate> templates = store->templates();
    for (const PhosphorZones::ScrollingTemplate& templ : templates) {
        QJsonObject json = templ.toJson();
        // Wire-only enrichment: isSystem is store-derived load state, never
        // part of the persisted schema, but the UI needs it to disable
        // delete on bundled templates.
        json.insert(PhosphorZones::TemplateJsonKeys::IsSystem, templ.isSystem);
        array.append(json);
    }
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString LayoutAdaptor::saveScrollingTemplate(const QString& templateJson)
{
    PhosphorZones::ScrollingTemplateStore* store = m_layoutManager->scrollingTemplateStore();
    if (!store) {
        qCWarning(lcDbusLayout) << "saveScrollingTemplate: no template store wired";
        return QString();
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(templateJson.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qCWarning(lcDbusLayout) << "saveScrollingTemplate: malformed JSON:" << parseError.errorString();
        return QString();
    }
    PhosphorZones::ScrollingTemplate templ = PhosphorZones::ScrollingTemplate::fromJson(doc.object());
    // D-Bus boundary clamp, same as createLayout / updateLayout apply to
    // layout names. The editor mirrors these caps client-side (TopBar.qml's
    // name field, TemplatePropertyPanel.qml's description maximumLength);
    // this boundary clamp is the enforcement, since a direct D-Bus caller
    // never goes through the editor and maximumLength truncates by UTF-16
    // code units anyway.
    templ.name = clampName(templ.name);
    // The description is free text with no name-length counterpart, so it needs
    // its own cap at the same boundary. clampName is the right tool for it too:
    // the surrogate-safe cut is what keeps a truncated emoji out of the stored
    // JSON.
    templ.description = clampName(templ.description, MaxTemplateDescriptionLength);
    // A missing/empty name is the one invalidity a fresh editor form can
    // produce; the store refuses it (returns a null id) and the caller
    // surfaces the refusal.
    const QUuid id = store->saveTemplate(templ);
    if (id.isNull()) {
        qCWarning(lcDbusLayout) << "saveScrollingTemplate: store refused the template";
        return QString();
    }
    qCInfo(lcDbusLayout) << "Saved scrolling template" << id;
    return id.toString();
}

bool LayoutAdaptor::deleteScrollingTemplate(const QString& id)
{
    PhosphorZones::ScrollingTemplateStore* store = m_layoutManager->scrollingTemplateStore();
    const QUuid parsed = QUuid::fromString(id);
    if (!store || parsed.isNull()) {
        qCWarning(lcDbusLayout) << "deleteScrollingTemplate: no template store wired, or malformed id" << id;
        return false;
    }
    if (!store->removeTemplate(parsed)) {
        // The store warns for the bundled-template refusal itself; the unknown
        // id is the arm that would otherwise return false with no trace at all.
        qCWarning(lcDbusLayout) << "deleteScrollingTemplate: store refused to delete" << id;
        return false;
    }
    // The id-keyed scrub the delete flow owes (same purge layout deletion
    // drives): every assignment rule AND every quick slot referencing the
    // deleted template loses that reference; only when the id no longer
    // resolves at all — a user file shadowing a bundled template resurfaces
    // the original under the SAME id, and scrubbing then would drop live
    // assignments to it.
    if (!store->contains(parsed)) {
        // The purge answers true when it changed a rule OR swept a quick
        // slot; either way subscribers holding slot state (the settings
        // quick-shortcut cards) need a refresh hint, and this signal is
        // their only revision bump.
        if (m_layoutManager->purgeLayoutIdFromAssignments(parsed.toString())) {
            Q_EMIT quickLayoutSlotsChanged();
        }
    }
    qCInfo(lcDbusLayout) << "Deleted scrolling template" << id;
    return true;
}

QString LayoutAdaptor::duplicateScrollingTemplate(const QString& id)
{
    PhosphorZones::ScrollingTemplateStore* store = m_layoutManager->scrollingTemplateStore();
    const QUuid parsed = QUuid::fromString(id);
    if (!store || parsed.isNull()) {
        qCWarning(lcDbusLayout) << "duplicateScrollingTemplate: no template store wired, or malformed id" << id;
        return QString();
    }
    const PhosphorZones::ScrollingTemplate source = store->templateById(parsed);
    if (!source.isValid()) {
        qCWarning(lcDbusLayout) << "duplicateScrollingTemplate: unknown template" << id;
        return QString();
    }
    // Name the copy HERE rather than letting duplicateTemplate append the
    // shared suffix itself: the store has no length cap, and once the suffix
    // is on the name there is nothing left to clamp without cutting it off
    // again. Trim the BASE to the reduced budget and re-append, the same shape
    // duplicateLayout uses, so a long name cannot swallow the suffix and leave
    // the copy visually identical to its source.
    const QString suffix = PhosphorZones::LayoutRegistry::duplicateNameSuffix();
    const QUuid copyId =
        store->duplicateTemplate(parsed, clampName(source.name, MaxLayoutNameLength - suffix.size()) + suffix);
    if (copyId.isNull()) {
        qCWarning(lcDbusLayout) << "duplicateScrollingTemplate: store refused to copy" << id;
        return QString();
    }
    qCInfo(lcDbusLayout) << "Duplicated scrolling template" << id << "to" << copyId;
    return copyId.toString();
}

void LayoutAdaptor::setAssignmentEntry(const QString& screenId, int virtualDesktop, const QString& activity, int mode,
                                       const QString& snappingLayout, const QString& tilingAlgorithm)
{
    QString resolvedId = PhosphorScreens::ScreenIdentity::idForName(screenId);
    if (resolvedId.isEmpty()) {
        qCWarning(lcDbusLayout) << "setAssignmentEntry: empty screen ID for" << screenId;
        return;
    }
    if (!validDesktopArg(virtualDesktop, "setAssignmentEntry")) {
        return;
    }

    // Validate snapping layout UUID if non-empty
    if (!snappingLayout.isEmpty()) {
        QUuid uuid = QUuid::fromString(snappingLayout);
        if (uuid.isNull()) {
            qCWarning(lcDbusLayout) << "setAssignmentEntry: invalid snapping layout UUID:" << snappingLayout;
            return;
        }
    }

    // Validate tiling algorithm if non-empty
    if (!tilingAlgorithm.isEmpty()) {
        if (!m_algorithmRegistry || !m_algorithmRegistry->algorithm(tilingAlgorithm)) {
            qCWarning(lcDbusLayout) << "setAssignmentEntry: unknown tiling algorithm:" << tilingAlgorithm;
            return;
        }
    }

    // Reject rather than clamp, like every other argument on this boundary
    // (screenId / layout uuid / algorithm are validated-and-refused):
    // clamping an unknown mode int to the nearest enumerator would silently
    // apply the WRONG engine (this bug shipped once — Scrolling(2) was clamped
    // to Autotile(1) and the third mode button applied Tiling).
    if (mode < 0 || mode > static_cast<int>(PhosphorZones::AssignmentEntry::Scrolling)) {
        qCWarning(lcDbusLayout) << "setAssignmentEntry: out-of-range mode" << mode << "for" << resolvedId;
        return;
    }

    // The desktop was the one argument the comment above claimed to cover but
    // did not. 0 is legal here (the documented screen-level entry); a negative
    // number names a context nothing resolves, and persisting a rule for it grows
    // the rule set with entries no cascade will ever read.
    if (!validateDesktopNumber(virtualDesktop, QStringLiteral("set assignment entry"), /*allowZero=*/true)) {
        return;
    }

    // Seed from the stored entry so the SCROLLING TEMPLATE survives a
    // Monitors-page save: this method's wire signature carries only
    // mode/snapping/tiling, and building a fresh entry would wipe the
    // template the moment the rule rebuild stopped carrying stale copies
    // across (the carryOverNonAssignmentActions exclusion). Same seeding
    // shape assignScrollingTemplate uses.
    PhosphorZones::AssignmentEntry entry = m_layoutManager->exactContextEntry(resolvedId, virtualDesktop, activity);
    entry.mode = static_cast<PhosphorZones::AssignmentEntry::Mode>(mode);
    // Canonicalize to the braced spelling: the purge sweep and the upsert
    // no-op guard both compare exact strings against QUuid::toString().
    entry.snappingLayout = snappingLayout.isEmpty() ? QString() : QUuid::fromString(snappingLayout).toString();
    entry.tilingAlgorithm = tilingAlgorithm;

    m_layoutManager->setAssignmentEntryDirect(resolvedId, virtualDesktop, activity, entry);
    m_changedScreenIds.insert(resolvedId);

    qCInfo(lcDbusLayout) << "setAssignmentEntry: screen=" << resolvedId << "desktop=" << virtualDesktop
                         << "activity=" << activity << "mode=" << mode << "snapping=" << snappingLayout
                         << "tiling=" << tilingAlgorithm << "template=" << entry.scrollingTemplateLayout;
}

void LayoutAdaptor::setSaveBatchMode(bool enabled)
{
    m_suppressScreenLayoutSignal = enabled;
}

void LayoutAdaptor::clearSaveBatchMode()
{
    // Self-healing counterpart to setSaveBatchMode(true). The XML calls the pair
    // "always paired", but nothing enforced it: a settings app that crashes
    // between the two calls, or any session peer calling the setter directly,
    // muted screenLayoutChanged for the daemon's whole remaining lifetime. Since
    // applyAssignmentChanges is the close of every legitimate batch, releasing
    // the flag there bounds the damage to one batch without changing the
    // behaviour of a well-behaved client, which sets it back to false itself.
    m_suppressScreenLayoutSignal = false;
}

void LayoutAdaptor::applyAssignmentChanges()
{
    // Drains the CLIENT's buffer only — the set accumulated by this adaptor's
    // own assignment slots since the last apply. Nothing else writes it.
    //
    // Release the batch suppression FIRST, above the empty-buffer return: a
    // client that called setSaveBatchMode(true) and then died before staging
    // anything is exactly the case this release exists for, and it stages
    // nothing, so releasing after the return would never fire for it.
    clearSaveBatchMode();
    if (m_changedScreenIds.isEmpty()) {
        // Nothing was staged, so there is nothing to apply. The early return
        // matters because downstream an EMPTY set means "every screen"
        // (populateResnapBufferForAllScreens skips its include filter, and the
        // OSD loop treats empty as match-all). Without this, a bus peer calling
        // applyAssignmentChanges with no preceding mutation would force a full
        // resnap of every window plus a per-screen OSD.
        return;
    }
    QSet<QString> changed = std::move(m_changedScreenIds);
    m_changedScreenIds.clear();
    applyAssignmentChangesFor(changed);
}

void LayoutAdaptor::applyAssignmentChangesFor(const QSet<QString>& screenIds)
{
    if (screenIds.isEmpty()) {
        return;
    }
    // Signal is typed as QStringList for D-Bus compatibility (QSet is not
    // marshallable). Receivers that need set semantics convert back.
    Q_EMIT assignmentChangesApplied(QStringList(screenIds.begin(), screenIds.end()));
}

} // namespace PlasmaZones
