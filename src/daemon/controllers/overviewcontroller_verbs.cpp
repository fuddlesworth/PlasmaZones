// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// The overview's verbs: the policy behind org.plasmazones.Overview's methods.
// Every screen id arrives canonical from the adaptor; every refusal is a
// debug log and no change, and the effect learns the outcome from the next
// model. Placement itself happens exactly once, inside the target engine's
// handoffReceive, reached through the WorkspaceController's by-id move.

#include "overviewcontroller.h"

#include "config/configdefaults.h"
#include "core/platform/logging.h"
#include "overviewdropresolver.h"
#include "workspacecontroller.h"

#include <PhosphorEngine/IOverviewModelSource.h>
#include <PhosphorScreens/Manager.h>
#include <PhosphorScrollEngine/ScrollEngine.h>
#include <PhosphorWorkspaces/VirtualDesktopManager.h>
#include <PhosphorWorkspaces/WorkspaceReconciler.h>
#include <PhosphorZones/LayoutRegistry.h>

namespace PlasmaZones {

void OverviewController::setScrollEngine(PhosphorScrollEngine::ScrollEngine* engine)
{
    m_scrollEngine = engine;
}

void OverviewController::setStripDirtyMarker(std::function<void()> marker)
{
    m_stripDirtyMarker = std::move(marker);
}

void OverviewController::setNamedEntriesAccess(NamedEntriesAccess access)
{
    m_namedEntries = std::move(access);
}

QPoint OverviewController::toGlobal(const QString& screenId, int x, int y) const
{
    const QRect output = m_screens ? m_screens->screenGeometry(screenId) : QRect();
    return QPoint(output.x() + x, output.y() + y);
}

PhosphorEngine::HandoffIntent OverviewController::intentFor(const QString& screenId, const QString& desktopId,
                                                            int dropX, int dropY) const
{
    PhosphorEngine::HandoffIntent intent;
    intent.dropPos = toGlobal(screenId, dropX, dropY);
    // A scrolling target's slot is resolved here, against the engine's own
    // strip for that context: the snap and autotile arms resolve theirs
    // from the drop point in the cross-mode move.
    if (!m_sources.scrolling || !m_vdm || !m_layouts) {
        return intent;
    }
    const int desktop = m_vdm->desktopIndexOf(desktopId);
    if (desktop <= 0
        || m_layouts->modeForScreen(screenId, desktop, currentActivity())
            != PhosphorZones::AssignmentEntry::Scrolling) {
        return intent;
    }
    const PhosphorEngine::PlacementStateKey key{screenId, desktop, currentActivity()};
    const auto strip = m_sources.scrolling->overviewStripFor(key);
    if (!strip) {
        // No strip yet (never visited, or stashed): the receive appends.
        return intent;
    }
    const ScrollDropTarget target = resolveScrollDrop(*strip, *intent.dropPos, stripIsVertical(*strip));
    intent.insertIndex = target.column;
    intent.insertTileIndex = target.tile;
    return intent;
}

void OverviewController::focusWorkspace(const QString& screenId, const QString& desktopId)
{
    if (!m_workspaces) {
        return;
    }
    m_workspaces->focusWorkspaceById(screenId, desktopId);
}

void OverviewController::moveWindowToWorkspace(const QString& windowId, const QString& screenId,
                                               const QString& desktopId, int dropX, int dropY)
{
    if (!m_workspaces || windowId.isEmpty()) {
        return;
    }
    if (m_windowStickyPredicate && m_windowStickyPredicate(windowId)) {
        qCDebug(lcDaemon) << "overview: refusing to move sticky window" << windowId;
        return;
    }
    m_workspaces->moveWindowToWorkspaceById(windowId, screenId, desktopId,
                                            intentFor(screenId, desktopId, dropX, dropY));
}

void OverviewController::moveWindowToNewWorkspace(const QString& windowId, const QString& screenId, int sliceIndex,
                                                  int dropX, int dropY)
{
    if (!m_workspaces || windowId.isEmpty()) {
        return;
    }
    if (m_windowStickyPredicate && m_windowStickyPredicate(windowId)) {
        qCDebug(lcDaemon) << "overview: refusing to move sticky window" << windowId;
        return;
    }
    PhosphorEngine::HandoffIntent intent;
    intent.dropPos = toGlobal(screenId, dropX, dropY);
    // A new workspace has no strip to resolve against: its first column.
    intent.insertIndex = 0;
    m_workspaces->moveWindowToNewWorkspace(windowId, screenId, sliceIndex, intent);
}

void OverviewController::reorderWorkspace(const QString& screenId, const QString& desktopId, int newSliceIndex)
{
    if (!m_workspaces) {
        return;
    }
    if (!m_workspaces->reorderWorkspaceById(screenId, desktopId, newSliceIndex)) {
        return;
    }
    // A named workspace's declaration carries its position; leave the two
    // agreeing so the next re-apply does not put it back.
    const QString name = m_workspaces->reconciler().map().entryFor(desktopId).name;
    if (!name.isEmpty()) {
        rewriteDeclaration(name, [newSliceIndex](QVariantMap& entry) {
            entry.insert(ConfigDefaults::namedEntryPositionField(), newSliceIndex);
        });
    }
}

void OverviewController::moveWorkspaceToScreen(const QString& desktopId, const QString& targetScreenId, int sliceIndex)
{
    if (!m_workspaces) {
        return;
    }
    const QString name = m_workspaces->reconciler().map().entryFor(desktopId).name;
    if (!m_workspaces->moveWorkspaceToScreenById(desktopId, targetScreenId, sliceIndex)) {
        return;
    }
    // A pinned workspace's declaration names its output; a transfer is the
    // user re-pinning it.
    if (!name.isEmpty()) {
        rewriteDeclaration(name, [targetScreenId, sliceIndex](QVariantMap& entry) {
            if (!entry.value(ConfigDefaults::namedEntryOutputField()).toString().trimmed().isEmpty()) {
                entry.insert(ConfigDefaults::namedEntryOutputField(), targetScreenId);
            }
            entry.insert(ConfigDefaults::namedEntryPositionField(), sliceIndex);
        });
    }
}

void OverviewController::renameWorkspace(const QString& desktopId, const QString& name)
{
    if (!m_workspaces) {
        return;
    }
    const QString trimmed = name.trimmed().left(ConfigDefaults::WorkspaceNameMaxLength);
    if (trimmed.isEmpty()) {
        return;
    }
    const QString declared = m_workspaces->reconciler().map().entryFor(desktopId).name;
    if (!declared.isEmpty()) {
        // Named: the declaration is the source of truth; renaming it there
        // re-applies through the ordinary declaration path (which pushes the
        // KWin name itself).
        if (declared == trimmed) {
            return;
        }
        rewriteDeclaration(declared, [trimmed](QVariantMap& entry) {
            entry.insert(ConfigDefaults::namedEntryNameField(), trimmed);
        });
        return;
    }
    if (!m_workspaces->renameDynamicWorkspace(desktopId, trimmed)) {
        qCDebug(lcDaemon) << "overview: rename refused for" << desktopId;
    }
}

void OverviewController::pinWorkspace(const QString& desktopId, bool pinned)
{
    if (!m_workspaces || !m_namedEntries.get || !m_namedEntries.set) {
        qCDebug(lcDaemon) << "overview: pin ignored, no declaration access";
        return;
    }
    const PhosphorWorkspaces::WorkspaceEntry entry = m_workspaces->reconciler().map().entryFor(desktopId);
    if (entry.desktopId.isEmpty()) {
        return;
    }
    QVariantList entries = m_namedEntries.get();
    if (!pinned) {
        if (entry.name.isEmpty()) {
            return;
        }
        for (int i = 0; i < entries.size(); ++i) {
            if (entries.at(i).toMap().value(ConfigDefaults::namedEntryNameField()).toString().trimmed() == entry.name) {
                entries.removeAt(i);
                m_namedEntries.set(entries);
                return;
            }
        }
        return;
    }
    if (!entry.name.isEmpty()) {
        return; // already a named workspace
    }
    // The declared name is the workspace's current KWin name, or a generated
    // one; declared names are unique, so a clash gets a numeric suffix.
    QString name;
    if (m_vdm) {
        const int index = m_vdm->desktopIndexOf(desktopId);
        const QStringList raw = m_vdm->rawDesktopNames();
        if (index > 0 && index <= raw.size()) {
            name = raw.at(index - 1).trimmed();
        }
        if (name.isEmpty()) {
            name = QStringLiteral("Workspace %1").arg(index);
        }
    } else {
        name = QStringLiteral("Workspace");
    }
    name = name.left(ConfigDefaults::WorkspaceNameMaxLength);
    QSet<QString> taken;
    for (const QVariant& value : std::as_const(entries)) {
        taken.insert(value.toMap().value(ConfigDefaults::namedEntryNameField()).toString().trimmed());
    }
    QString unique = name;
    for (int n = 2; taken.contains(unique); ++n) {
        unique = QStringLiteral("%1 %2").arg(name).arg(n);
    }
    QVariantMap declaration;
    declaration.insert(ConfigDefaults::namedEntryNameField(), unique);
    declaration.insert(ConfigDefaults::namedEntryOutputField(), m_workspaces->reconciler().map().ownerOf(desktopId));
    declaration.insert(ConfigDefaults::namedEntryPositionField(),
                       m_workspaces->reconciler().map().sliceIndexOf(desktopId));
    declaration.insert(ConfigDefaults::namedEntryFocusShortcutField(), QString());
    declaration.insert(ConfigDefaults::namedEntryMoveShortcutField(), QString());
    entries.append(declaration);
    m_namedEntries.set(entries);
}

void OverviewController::panStrip(const QString& screenId, const QString& desktopId, int deltaPx)
{
    if (!m_scrollEngine || !m_vdm || deltaPx == 0) {
        return;
    }
    const int desktop = m_vdm->desktopIndexOf(desktopId);
    if (desktop <= 0) {
        return;
    }
    const PhosphorEngine::PlacementStateKey key{screenId, desktop, currentActivity()};
    if (!m_scrollEngine->panStoredView(key, deltaPx)) {
        return;
    }
    if (m_stripDirtyMarker) {
        m_stripDirtyMarker();
    }
    // The current context shows the pan now; any other context shows it
    // when next focused, through its ordinary relayout.
    if (m_vdm->currentDesktopForScreen(screenId) == desktop) {
        m_scrollEngine->retile(screenId);
    }
    scheduleRebuild();
}

void OverviewController::rewriteDeclaration(const QString& name, const std::function<void(QVariantMap&)>& mutate)
{
    if (!m_namedEntries.get || !m_namedEntries.set) {
        return;
    }
    QVariantList entries = m_namedEntries.get();
    for (int i = 0; i < entries.size(); ++i) {
        QVariantMap entry = entries.at(i).toMap();
        if (entry.value(ConfigDefaults::namedEntryNameField()).toString().trimmed() != name) {
            continue;
        }
        mutate(entry);
        entries[i] = entry;
        m_namedEntries.set(entries);
        return;
    }
}

} // namespace PlasmaZones
