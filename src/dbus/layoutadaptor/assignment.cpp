// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../layoutadaptor.h"
#include <PhosphorZones/Layout.h>
#include "../../core/layoutmanager.h"
#include "../../core/virtualdesktopmanager.h"
#include "../../core/activitymanager.h"
#include "../../core/logging.h"
#include "../../core/utils.h"
#include "../../core/constants.h"
#include <PhosphorScreens/Manager.h>
#include <PhosphorScreens/VirtualScreen.h>
#include <PhosphorTiles/AlgorithmRegistry.h>
#include <PhosphorTiles/TilingAlgorithm.h>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QScreen>
#include <PhosphorScreens/ScreenIdentity.h>

namespace PlasmaZones {

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
// LayoutManager auto-persists in assignLayout(), assignLayoutById(),
// clearAssignment(), setAssignmentEntryDirect(), and the batch setAll*() methods.
QString LayoutAdaptor::getLayoutForScreen(const QString& screenId)
{
    int desktop = m_virtualDesktopManager ? m_virtualDesktopManager->currentDesktop() : 0;
    QString activity = m_activityManager ? m_activityManager->currentActivity() : QString();
    QString resolvedId = Phosphor::Screens::ScreenIdentity::idForName(screenId);

    // Check for autotile assignment first (layoutForScreen returns nullptr for autotile)
    QString assignmentId = m_layoutManager->assignmentIdForScreen(resolvedId, desktop, activity);
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
        return assignmentId;
    }

    auto* layout = m_layoutManager->layoutForScreen(resolvedId, desktop, activity);
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreen(const QString& screenId, const QString& layoutId)
{
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("assign layout"))) {
        return;
    }

    // For manual layouts, validate UUID and verify layout exists
    PhosphorZones::Layout* layout = nullptr;
    if (!PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen"));
        if (!layout) {
            return;
        }
    }

    // Warn if screen name is not in the daemon's screen list (e.g. script using wrong name)
    if (!Phosphor::Screens::ScreenIdentity::findByIdOrName(screenId)) {
        qCWarning(lcDbusLayout)
            << "assignLayoutToScreen: screen name" << screenId
            << "not found in daemon's screen list. Use org.plasmazones.Screen.getScreens for valid names.";
    }

    QString resolvedId = Phosphor::Screens::ScreenIdentity::idForName(screenId);
    m_layoutManager->assignLayoutById(resolvedId, 0, QString(), layoutId);

    // Update global active layout when assigning to the primary screen (manual layouts only)
    if (layout) {
        QScreen* primary = Utils::primaryScreen();
        if (primary && Phosphor::Screens::ScreenIdentity::identifierFor(primary) == resolvedId) {
            m_layoutManager->setActiveLayout(layout);
        }
    }

    qCInfo(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenId << "(id:" << resolvedId << ")";
}

void LayoutAdaptor::clearAssignment(const QString& screenId)
{
    m_layoutManager->clearAssignment(Phosphor::Screens::ScreenIdentity::idForName(screenId));
}

void LayoutAdaptor::setAllScreenAssignments(const QVariantMap& assignments)
{
    QHash<QString, QString> parsedAssignments;
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        const QString& screenIdOrName = it.key();
        QString layoutId = it.value().toString();
        if (!layoutId.isEmpty() && !PhosphorLayout::LayoutId::isAutotile(layoutId)) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch screen assignment"));
            if (!uuidOpt) {
                continue;
            }
        }
        parsedAssignments[Phosphor::Screens::ScreenIdentity::idForName(screenIdOrName)] = layoutId;
    }

    m_layoutManager->setAllScreenAssignments(parsedAssignments);
    // Update global active layout for the primary screen so zone overlay/drag see the new layout
    // immediately (same as assignLayoutToScreen). KCM Save uses this path.
    QScreen* primary = Utils::primaryScreen();
    if (primary) {
        PhosphorZones::Layout* primaryLayout =
            m_layoutManager->resolveLayoutForScreen(Phosphor::Screens::ScreenIdentity::identifierFor(primary));
        if (primaryLayout) {
            m_layoutManager->setActiveLayout(primaryLayout);
        }
    }

    qCInfo(lcDbusLayout) << "Batch set" << parsedAssignments.size() << "screen assignments";
}

QString LayoutAdaptor::getAllScreenAssignments()
{
    QJsonObject root;
    const int desktopCount = getVirtualDesktopCount();

    // Use effective screen IDs (includes virtual screens when configured)
    // so the KCM sees one entry per virtual screen, not per physical monitor.
    const QStringList screenIds = (m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList());

    for (const QString& screenId : std::as_const(screenIds)) {
        // Derive connector name for the JSON key (KCM compatibility)
        // Virtual screens use their full ID directly (e.g., "physId/vs:0");
        // physical screens use the QScreen connector name for KCM parity.
        QString connectorName;
        if (PhosphorIdentity::VirtualScreenId::isVirtual(screenId)) {
            connectorName = screenId;
        } else {
            QScreen* physScreen = Phosphor::Screens::ScreenIdentity::findByIdOrName(screenId);
            connectorName = physScreen ? physScreen->name() : screenId;
        }
        QJsonObject screenObj;

        // Explicit assignment entry with mode, snappingLayout, tilingAlgorithm
        auto entry = m_layoutManager->assignmentEntryForScreen(screenId, 0, QString());
        QString effectiveId = entry.activeLayoutId();
        if (!effectiveId.isEmpty()) {
            screenObj[QLatin1String("default")] = effectiveId;
        }

        // Expose both fields so the KCM can populate snapping AND tiling assignments
        if (!entry.snappingLayout.isEmpty()) {
            screenObj[QLatin1String("snappingLayout")] = entry.snappingLayout;
        }
        if (!entry.tilingAlgorithm.isEmpty()) {
            screenObj[QLatin1String("tilingAlgorithm")] = entry.tilingAlgorithm;
        }
        screenObj[QLatin1String("mode")] = static_cast<int>(entry.mode);

        // Per-desktop entries (desktop > 0) — only include explicitly assigned
        // desktops, not inherited base defaults.  Without this guard every
        // desktop row in the KCM would show the display default redundantly.
        for (int desktop = 1; desktop <= desktopCount; ++desktop) {
            if (m_layoutManager->hasExplicitAssignment(screenId, desktop, QString())) {
                QString desktopId = m_layoutManager->assignmentIdForScreen(screenId, desktop, QString());
                if (!desktopId.isEmpty()) {
                    screenObj[QString::number(desktop)] = desktopId;
                }
            }
        }

        if (!screenObj.isEmpty()) {
            // Key by connector name for KCM compatibility (D-Bus boundary translates on save)
            // Include screenId inside the object for consumers that need it
            screenObj[QLatin1String("screenId")] = screenId;
            root[connectorName] = screenObj;
        }
    }

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// Per-Virtual-Desktop Assignments
QString LayoutAdaptor::getLayoutForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    QString resolvedId = Phosphor::Screens::ScreenIdentity::idForName(screenId);
    QString assignmentId = m_layoutManager->assignmentIdForScreen(resolvedId, virtualDesktop, QString());
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
        return assignmentId;
    }
    auto* layout = m_layoutManager->layoutForScreen(resolvedId, virtualDesktop, QString());
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreenDesktop(const QString& screenId, int virtualDesktop, const QString& layoutId)
{
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("assign layout to desktop"))) {
        return;
    }

    // Validate UUID for manual layouts, skip for autotile IDs
    if (!PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        auto* layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen desktop"));
        if (!layout) {
            return;
        }
    }

    QString resolvedId = Phosphor::Screens::ScreenIdentity::idForName(screenId);
    m_layoutManager->assignLayoutById(resolvedId, virtualDesktop, QString(), layoutId);
    qCInfo(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenId << "(id:" << resolvedId
                         << ") on desktop" << virtualDesktop;

    // PhosphorZones::Layout resolution is triggered by LayoutManager::layoutAssigned signal
    // → daemon's syncModeFromAssignments(). No direct updateActiveLayout() needed.
}

void LayoutAdaptor::clearAssignmentForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    m_layoutManager->clearAssignment(Phosphor::Screens::ScreenIdentity::idForName(screenId), virtualDesktop, QString());
    qCInfo(lcDbusLayout) << "Cleared assignment for screen" << screenId << "on desktop" << virtualDesktop;
}

bool LayoutAdaptor::hasExplicitAssignmentForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    return m_layoutManager->hasExplicitAssignment(Phosphor::Screens::ScreenIdentity::idForName(screenId),
                                                  virtualDesktop, QString());
}

int LayoutAdaptor::getModeForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    return static_cast<int>(m_layoutManager->modeForScreen(Phosphor::Screens::ScreenIdentity::idForName(screenId),
                                                           virtualDesktop, QString()));
}

QString LayoutAdaptor::getSnappingLayoutForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    return m_layoutManager->snappingLayoutForScreen(Phosphor::Screens::ScreenIdentity::idForName(screenId),
                                                    virtualDesktop, QString());
}

QString LayoutAdaptor::getTilingAlgorithmForScreenDesktop(const QString& screenId, int virtualDesktop)
{
    return m_layoutManager->tilingAlgorithmForScreen(Phosphor::Screens::ScreenIdentity::idForName(screenId),
                                                     virtualDesktop, QString());
}

QString LayoutAdaptor::getScreenStates()
{
    if (!m_layoutManager)
        return QStringLiteral("[]");

    QJsonArray result;

    const int desktop = m_layoutManager->currentVirtualDesktop();
    const QString activity = m_layoutManager->currentActivity();

    // Use effective screen IDs (includes virtual screens when configured)
    // so the settings app sees one entry per virtual screen, not per physical monitor.
    const QStringList screenIds = (m_screenManager ? m_screenManager->effectiveScreenIds() : QStringList());

    for (const QString& screenId : std::as_const(screenIds)) {
        const auto entry = m_layoutManager->assignmentEntryForScreen(screenId, desktop, activity);

        QJsonObject obj;
        obj[QLatin1String("screenId")] = screenId;
        obj[QLatin1String("virtualDesktop")] = desktop;
        obj[QLatin1String("activity")] = activity;
        obj[QLatin1String("mode")] = static_cast<int>(entry.mode);

        // Snapping layout — use resolved layout (includes default fallback)
        PhosphorZones::Layout* resolvedLayout = m_layoutManager->layoutForScreen(screenId, desktop, activity);
        if (resolvedLayout) {
            obj[QLatin1String("layoutId")] = resolvedLayout->id().toString();
            obj[QLatin1String("layoutName")] = resolvedLayout->name();
        } else {
            obj[QLatin1String("layoutId")] = QString();
            obj[QLatin1String("layoutName")] = QString();
        }

        // Tiling algorithm — use resolved algorithm (includes fallback)
        const QString algoId = m_layoutManager->tilingAlgorithmForScreen(screenId, desktop, activity);
        obj[QLatin1String("algorithmId")] = algoId;
        if (!algoId.isEmpty()) {
            auto* registry = m_algorithmRegistry;
            PhosphorTiles::TilingAlgorithm* algo = registry->algorithm(algoId);
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
        if (!layoutId.isEmpty() && !PhosphorLayout::LayoutId::isAutotile(layoutId)) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch desktop assignment"));
            if (!uuidOpt) {
                continue;
            }
        }
        parsedAssignments[qMakePair(Phosphor::Screens::ScreenIdentity::idForName(screenIdOrName), virtualDesktop)] =
            layoutId;
    }

    m_layoutManager->setAllDesktopAssignments(parsedAssignments);
    qCInfo(lcDbusLayout) << "Batch set" << parsedAssignments.size() << "desktop assignments";
}

QVariantMap LayoutAdaptor::getAllDesktopAssignments()
{
    QVariantMap result;

    const auto assignments = m_layoutManager->desktopAssignments();
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        QString key = QStringLiteral("%1|%2").arg(it.key().first).arg(it.key().second);
        result[key] = it.value();
    }

    return result;
}

QVariantMap LayoutAdaptor::getAllActivityAssignments()
{
    QVariantMap result;

    const auto assignments = m_layoutManager->activityAssignments();
    for (auto it = assignments.begin(); it != assignments.end(); ++it) {
        QString key = QStringLiteral("%1|%2").arg(it.key().first, it.key().second);
        result[key] = it.value();
    }

    return result;
}

// KDE Activities Support
void LayoutAdaptor::setActivityManager(ActivityManager* am)
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
        connect(m_activityManager, &ActivityManager::currentActivityChanged, this,
                &LayoutAdaptor::currentActivityChanged);
        connect(m_activityManager, &ActivityManager::activitiesChanged, this, &LayoutAdaptor::activitiesChanged);
    }
}

bool LayoutAdaptor::isActivitiesAvailable()
{
    return ActivityManager::isAvailable();
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
    QString resolvedId = Phosphor::Screens::ScreenIdentity::idForName(screenId);
    QString assignmentId = m_layoutManager->assignmentIdForScreen(resolvedId, 0, activityId);
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
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
    if (!PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        auto* layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen activity"));
        if (!layout) {
            return;
        }
    }

    m_layoutManager->assignLayoutById(Phosphor::Screens::ScreenIdentity::idForName(screenId), 0, activityId, layoutId);

    qCInfo(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenId << "for activity" << activityId;

    // PhosphorZones::Layout resolution is triggered by LayoutManager::layoutAssigned signal
    // → daemon's syncModeFromAssignments(). No direct updateActiveLayout() needed.
}

void LayoutAdaptor::clearAssignmentForScreenActivity(const QString& screenId, const QString& activityId)
{
    m_layoutManager->clearAssignment(Phosphor::Screens::ScreenIdentity::idForName(screenId), 0, activityId);
    qCInfo(lcDbusLayout) << "Cleared assignment for screen" << screenId << "activity" << activityId;
}

bool LayoutAdaptor::hasExplicitAssignmentForScreenActivity(const QString& screenId, const QString& activityId)
{
    return m_layoutManager->hasExplicitAssignment(Phosphor::Screens::ScreenIdentity::idForName(screenId), 0,
                                                  activityId);
}

void LayoutAdaptor::setAllActivityAssignments(const QVariantMap& assignments)
{
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
        if (!layoutId.isEmpty() && !PhosphorLayout::LayoutId::isAutotile(layoutId)) {
            auto uuidOpt = parseAndValidateUuid(layoutId, QStringLiteral("batch activity assignment"));
            if (!uuidOpt) {
                continue;
            }
        }
        parsedAssignments[qMakePair(Phosphor::Screens::ScreenIdentity::idForName(screenIdOrName), activityId)] =
            layoutId;
    }

    m_layoutManager->setAllActivityAssignments(parsedAssignments);
    qCInfo(lcDbusLayout) << "Batch set" << parsedAssignments.size() << "activity assignments";
}

// Full Assignments (Screen + Desktop + Activity)
QString LayoutAdaptor::getLayoutForScreenDesktopActivity(const QString& screenId, int virtualDesktop,
                                                         const QString& activityId)
{
    QString resolvedId = Phosphor::Screens::ScreenIdentity::idForName(screenId);
    QString assignmentId = m_layoutManager->assignmentIdForScreen(resolvedId, virtualDesktop, activityId);
    if (PhosphorLayout::LayoutId::isAutotile(assignmentId)) {
        return assignmentId;
    }
    auto* layout = m_layoutManager->layoutForScreen(resolvedId, virtualDesktop, activityId);
    return layout ? layout->id().toString() : QString();
}

void LayoutAdaptor::assignLayoutToScreenDesktopActivity(const QString& screenId, int virtualDesktop,
                                                        const QString& activityId, const QString& layoutId)
{
    if (!validateNonEmpty(screenId, QStringLiteral("screen name"), QStringLiteral("assign layout"))) {
        return;
    }

    // Validate UUID for manual layouts, skip for autotile IDs
    if (!PhosphorLayout::LayoutId::isAutotile(layoutId)) {
        auto* layout = getValidatedLayout(layoutId, QStringLiteral("assign layout to screen desktop activity"));
        if (!layout) {
            return;
        }
    }

    m_layoutManager->assignLayoutById(Phosphor::Screens::ScreenIdentity::idForName(screenId), virtualDesktop,
                                      activityId, layoutId);

    qCInfo(lcDbusLayout) << "Assigned layout" << layoutId << "to screen" << screenId << "desktop" << virtualDesktop
                         << "activity" << activityId;

    // PhosphorZones::Layout resolution is triggered by LayoutManager::layoutAssigned signal
    // → daemon's syncModeFromAssignments(). No direct updateActiveLayout() needed.
}

void LayoutAdaptor::clearAssignmentForScreenDesktopActivity(const QString& screenId, int virtualDesktop,
                                                            const QString& activityId)
{
    QString resolvedId = Phosphor::Screens::ScreenIdentity::idForName(screenId);
    m_layoutManager->clearAssignment(resolvedId, virtualDesktop, activityId);
    m_changedScreenIds.insert(resolvedId);
    qCInfo(lcDbusLayout) << "Cleared assignment for screen" << screenId << "desktop" << virtualDesktop << "activity"
                         << activityId;
}

void LayoutAdaptor::setAssignmentEntry(const QString& screenId, int virtualDesktop, const QString& activity, int mode,
                                       const QString& snappingLayout, const QString& tilingAlgorithm)
{
    QString resolvedId = Phosphor::Screens::ScreenIdentity::idForName(screenId);
    if (resolvedId.isEmpty()) {
        qCWarning(lcDbusLayout) << "setAssignmentEntry: empty screen ID for" << screenId;
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

    AssignmentEntry entry;
    entry.mode = static_cast<AssignmentEntry::Mode>(qBound(0, mode, 1));
    entry.snappingLayout = snappingLayout;
    entry.tilingAlgorithm = tilingAlgorithm;

    m_layoutManager->setAssignmentEntryDirect(resolvedId, virtualDesktop, activity, entry);
    m_changedScreenIds.insert(resolvedId);

    qCInfo(lcDbusLayout) << "setAssignmentEntry: screen=" << resolvedId << "desktop=" << virtualDesktop
                         << "activity=" << activity << "mode=" << mode << "snapping=" << snappingLayout
                         << "tiling=" << tilingAlgorithm;
}

void LayoutAdaptor::setSaveBatchMode(bool enabled)
{
    m_suppressScreenLayoutSignal = enabled;
}

void LayoutAdaptor::applyAssignmentChanges()
{
    QSet<QString> changed = std::move(m_changedScreenIds);
    m_changedScreenIds.clear();
    // Signal is typed as QStringList for D-Bus compatibility (QSet is not
    // marshallable). Receivers that need set semantics convert back.
    Q_EMIT assignmentChangesApplied(QStringList(changed.begin(), changed.end()));
}

} // namespace PlasmaZones
