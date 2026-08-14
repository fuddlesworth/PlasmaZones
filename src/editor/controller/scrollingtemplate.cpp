// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../EditorTemplateModel.h"
#include "../undo/UndoController.h"
#include "core/platform/logging.h"
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>

#include "phosphor_i18n.h"
#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace PlasmaZones {

// ---------------------------------------------------------------------------
// Editing mode
// ---------------------------------------------------------------------------

void EditorController::setEditorModeInternal(int mode)
{
    if (m_editorMode == mode) {
        return;
    }
    m_editorMode = mode;
    // Zone selection is meaningless outside layout mode, and stale selection
    // state would leak into shortcut enablement gates.
    setSelectedZoneIdsDirect({});
    Q_EMIT editorModeChanged();
}

void EditorController::reloadLocalTemplates()
{
    if (m_templateStore) {
        m_templateStore->loadTemplates();
    }
}

// ---------------------------------------------------------------------------
// Template lifecycle
// ---------------------------------------------------------------------------

void EditorController::createNewScrollingTemplate()
{
    setEditorModeInternal(ModeScrollingTemplate);

    m_layoutId = QUuid::createUuid().toString();
    m_layoutName = PhosphorI18n::tr("New Template");
    m_isNewLayout = true;
    m_hasUnsavedChanges = true;

    // Same starting shape the settings dialog seeded: no blueprint columns,
    // width-preset default pointing at the middle stop of a thirds/half/two-
    // thirds vocabulary.
    PhosphorZones::ScrollingTemplate templ;
    templ.presetColumnWidths = {1.0 / 3.0, 0.5, 2.0 / 3.0};
    templ.presetWindowHeights = {1.0 / 3.0, 0.5, 2.0 / 3.0};
    m_scrollingTemplate->resetState(EditorTemplateModel::stateFromTemplate(templ), /*isSystem*/ false);

    if (m_undoController) {
        m_undoController->clear();
    }

    Q_EMIT layoutIdChanged();
    Q_EMIT layoutNameChanged();
    Q_EMIT hasUnsavedChangesChanged();
}

void EditorController::loadScrollingTemplate(const QString& templateId)
{
    const QUuid id = QUuid::fromString(templateId);
    if (id.isNull()) {
        Q_EMIT layoutLoadFailed(PhosphorI18n::tr("That template is no longer available."));
        return;
    }

    PhosphorZones::ScrollingTemplate templ;
    if (m_templateStore) {
        templ = m_templateStore->templateById(id);
    }
    if (!templ.isValid()) {
        // Local read view can be behind a just-saved daemon write; ask the
        // daemon for the full list and pick the id out of it.
        const QDBusMessage reply = PhosphorProtocol::ClientHelpers::syncCall(
            PhosphorProtocol::Service::Interface::LayoutRegistry, QStringLiteral("getScrollingTemplates"), {});
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            const QJsonArray array = QJsonDocument::fromJson(reply.arguments().first().toString().toUtf8()).array();
            for (const QJsonValue& value : array) {
                const QJsonObject obj = value.toObject();
                if (QUuid::fromString(obj.value(PhosphorZones::TemplateJsonKeys::Id).toString()) == id) {
                    templ = PhosphorZones::ScrollingTemplate::fromJson(obj);
                    templ.isSystem = obj.value(PhosphorZones::TemplateJsonKeys::IsSystem).toBool();
                    break;
                }
            }
        }
    }
    if (!templ.isValid()) {
        qCWarning(lcEditor) << "loadScrollingTemplate: unknown template" << templateId;
        Q_EMIT layoutLoadFailed(PhosphorI18n::tr("That template is no longer available."));
        return;
    }

    setEditorModeInternal(ModeScrollingTemplate);

    m_layoutId = templ.id.toString();
    m_layoutName = templ.name;
    m_isNewLayout = false;
    m_hasUnsavedChanges = false;
    m_scrollingTemplate->resetState(EditorTemplateModel::stateFromTemplate(templ), templ.isSystem);

    if (m_undoController) {
        m_undoController->clear();
    }

    Q_EMIT layoutIdChanged();
    Q_EMIT layoutNameChanged();
    Q_EMIT hasUnsavedChangesChanged();
}

bool EditorController::saveScrollingTemplateNow()
{
    // The daemon clamps name/description and normalizes fractions at its
    // boundary; the local-store fallback normalizes too.
    const QJsonObject payload = m_scrollingTemplate->toWirePayload(m_layoutId, m_layoutName);

    QString savedId;
    const QString payloadStr = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    const QDBusMessage reply = PhosphorProtocol::ClientHelpers::syncCall(
        PhosphorProtocol::Service::Interface::LayoutRegistry, QStringLiteral("saveScrollingTemplate"), {payloadStr});
    if (reply.type() == QDBusMessage::ReplyMessage) {
        savedId = reply.arguments().value(0).toString();
        if (savedId.isEmpty()) {
            // The daemon answered and refused (invalid after normalization —
            // in practice an empty name, which the UI already gates).
            Q_EMIT layoutSaveFailed(PhosphorI18n::tr("The daemon refused the template. Check that it has a name."));
            return false;
        }
    } else {
        // Daemon unreachable — fall back to the local store so the editor
        // works standalone, mirroring loadLayout's local-registry fast path.
        PhosphorZones::ScrollingTemplate templ = PhosphorZones::ScrollingTemplate::fromJson(payload);
        const QUuid storedId = m_templateStore ? m_templateStore->saveTemplate(templ) : QUuid();
        if (storedId.isNull()) {
            Q_EMIT layoutSaveFailed(PhosphorI18n::tr("Could not save the template."));
            return false;
        }
        savedId = storedId.toString();
    }

    if (m_layoutId != savedId) {
        m_layoutId = savedId;
        Q_EMIT layoutIdChanged();
    }
    m_isNewLayout = false;
    m_hasUnsavedChanges = false;
    // The save wrote a user copy that shadows the bundled file, so the loaded
    // template is no longer the read-only system entry.
    m_scrollingTemplate->clearSystemFlag();
    if (m_undoController) {
        m_undoController->setClean();
    }
    Q_EMIT hasUnsavedChangesChanged();
    Q_EMIT layoutSaved();
    return true;
}

} // namespace PlasmaZones
