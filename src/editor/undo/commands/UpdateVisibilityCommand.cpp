// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UpdateVisibilityCommand.h"

#include <KLocalizedString>

#include "../../EditorController.h"

using namespace PlasmaZones;

UpdateVisibilityCommand::UpdateVisibilityCommand(QPointer<EditorController> editorController,
                                                 const QStringList& oldScreens, const QStringList& newScreens,
                                                 const QList<int>& oldDesktops, const QList<int>& newDesktops,
                                                 const QStringList& oldActivities, const QStringList& newActivities,
                                                 const QString& text, QUndoCommand* parent)
    : QUndoCommand(text.isEmpty() ? i18nc("@action", "Change Visibility") : text, parent)
    , m_editorController(editorController)
    , m_oldScreens(oldScreens)
    , m_newScreens(newScreens)
    , m_oldDesktops(oldDesktops)
    , m_newDesktops(newDesktops)
    , m_oldActivities(oldActivities)
    , m_newActivities(newActivities)
{
}

void UpdateVisibilityCommand::undo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->setAllowedScreensDirect(m_oldScreens);
    m_editorController->setAllowedDesktopsDirect(m_oldDesktops);
    m_editorController->setAllowedActivitiesDirect(m_oldActivities);
}

void UpdateVisibilityCommand::redo()
{
    if (!m_editorController) {
        return;
    }
    m_editorController->setAllowedScreensDirect(m_newScreens);
    m_editorController->setAllowedDesktopsDirect(m_newDesktops);
    m_editorController->setAllowedActivitiesDirect(m_newActivities);
}
