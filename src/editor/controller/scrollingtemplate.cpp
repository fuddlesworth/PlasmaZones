// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../EditorController.h"
#include "../EditorTemplateModel.h"
#include "../undo/UndoController.h"
#include "core/platform/logging.h"
#include "core/types/constants.h"
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>

#include "phosphor_i18n.h"
#include <QDBusError>
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
    // state would leak into shortcut enablement gates. A multi-zone drag
    // interrupted by a forwarded launch must end too, or its active flag and
    // captured positions outlive the canvas that owned them (the primary
    // zone's own restore is the QML handler's half and is covered by the
    // zone reload every route back into layout mode performs).
    setSelectedZoneIdsDirect({});
    if (m_multiZoneDragActive) {
        endMultiZoneDrag(/*commit*/ false);
    }
    // A parked screen switch answers a question about the PREVIOUS edited
    // object; once the mode changed, applying it later could save or discard
    // the wrong one. Drop it — the prompt's dialog acts on a cleared park as
    // a no-op.
    m_pendingTargetScreen.reset();
    m_pendingTargetEditsLayout = false;
    // m_pendingLaunch is deliberately NOT dropped here, unlike the park
    // above: a parked launch has its own confirmation dialog whose Confirm
    // applies the launch wholesale (mode included), so it stays meaningful
    // across a mode change — dropping it would strand that open dialog
    // acting on nothing.
    // The rest of the layout session (zones, gap overrides, bounds)
    // deliberately stays loaded when entering template mode: QML gates every
    // reader on the mode, and each route back into layout mode
    // (createNewLayout, loadLayout, the forced plain-screen launch) rebuilds
    // it before anything renders or saves.
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

// Shared session-begin tail for the two template entry points, so the reset
// work and the signal contract are written once.
//
// Signal order is load-bearing: layoutIdChanged fires BEFORE the model reset
// because the strip canvas latches its reload handling (contentX rewind,
// selection clear) on it, and must observe the new identity before the
// columnsChanged the reset emits — otherwise a template loaded over an open
// canvas is mistaken for a column append. The id/name/dirty emissions are
// deliberately unconditional: layoutIdChanged doubles as the reload beacon,
// and a discard that re-loads the SAME id still needs the canvas to rewind.
void EditorController::beginTemplateSession(const QString& id, const QString& name, bool isNew,
                                            const QVariantMap& state, bool isSystem)
{
    setEditorModeInternal(ModeScrollingTemplate);
    // The mode setter drops the parked screen switch only on a genuine mode
    // CHANGE (it early-returns otherwise), so a template loaded over an open
    // template session — the same-mode swap — would keep a park answering
    // for the PREVIOUS template and later save or discard the wrong one.
    // Same rationale as the setter's own clear, applied per session.
    m_pendingTargetScreen.reset();
    m_pendingTargetEditsLayout = false;

    // A template has no fixed-zone reference, so the layout bounding box a
    // previous FIXED-geometry layout left behind is never the right basis for
    // it. targetScreenSize() prefers that override, and templatePreviewVertical
    // resolves the Auto axis from whatever it returns, so a template launched
    // for the SAME screen an editing session already targeted would take its
    // strip direction from the old layout's bounding box instead of the
    // monitor. Neither clearer on the launch path runs in that case, because
    // applyLaunch's screen switch early-returns when the target is unchanged.
    if (m_layoutBoundsOverride.isValid()) {
        m_layoutBoundsOverride = QSize();
        Q_EMIT targetScreenSizeChanged();
    }

    m_layoutId = id;
    m_layoutName = name;
    m_isNewLayout = isNew;
    m_hasUnsavedChanges = isNew;

    Q_EMIT layoutIdChanged();
    Q_EMIT layoutNameChanged();
    Q_EMIT hasUnsavedChangesChanged();

    m_scrollingTemplate->resetState(state, isSystem);

    if (m_undoController) {
        m_undoController->clear();
    }
}

void EditorController::createNewScrollingTemplate()
{
    // Same starting shape the deleted settings-app form seeded: no blueprint
    // columns, width-preset default pointing at the middle stop of a thirds/
    // half/two-thirds vocabulary.
    PhosphorZones::ScrollingTemplate templ;
    templ.presetColumnWidths = {1.0 / 3.0, 0.5, 2.0 / 3.0};
    templ.presetWindowHeights = {1.0 / 3.0, 0.5, 2.0 / 3.0};
    beginTemplateSession(QUuid::createUuid().toString(), PhosphorI18n::tr("New Template"), /*isNew*/ true,
                         EditorTemplateModel::stateFromTemplate(templ), /*isSystem*/ false);
}

bool EditorController::loadScrollingTemplate(const QString& templateId)
{
    const QUuid id = QUuid::fromString(templateId);
    if (id.isNull()) {
        Q_EMIT layoutLoadFailed(PhosphorI18n::tr("That template is no longer available."));
        return false;
    }

    PhosphorZones::ScrollingTemplate templ;
    if (m_templateStore) {
        templ = m_templateStore->templateById(id);
    }
    if (!templ.isValid()) {
        // Absent from the local read view (a template created in another
        // process since our last rescan); ask the daemon for the full list
        // and pick the id out of it. A stale-but-present local entry wins
        // without a round-trip — the scrollingTemplatesChanged reload keeps
        // that window small.
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
        return false;
    }

    beginTemplateSession(templ.id.toString(), templ.name, /*isNew*/ false,
                         EditorTemplateModel::stateFromTemplate(templ), templ.isSystem);
    return true;
}

bool EditorController::saveScrollingTemplateNow()
{
    // Clamp the name before building the payload so BOTH persistence paths
    // (daemon boundary and the offline local store) write the same clamped
    // spelling; the daemon re-applies the identical clamp at its boundary.
    // Saving also deliberately recreates a template another process deleted
    // while this editor held it — the same semantics as saving a file whose
    // on-disk copy was removed underneath the editor.
    const QString saveName = clampName(m_layoutName);
    const QJsonObject payload = m_scrollingTemplate->toWirePayload(m_layoutId, saveName);

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
        // Only genuinely-unreachable errors take the offline fallback: a
        // reachable daemon that ERRORED must surface as a failure, not be
        // papered over with a local write the daemon's registry never learns
        // about (nothing watches the template directory).
        const QDBusError error(reply);
        const bool unreachable = error.type() == QDBusError::ServiceUnknown || error.type() == QDBusError::NoReply
            || error.type() == QDBusError::Timeout || error.type() == QDBusError::Disconnected;
        if (!unreachable) {
            qCWarning(lcEditor) << "saveScrollingTemplate: daemon error" << error.name() << error.message();
            Q_EMIT layoutSaveFailed(PhosphorI18n::tr("Could not save the template."));
            return false;
        }
        // Daemon unreachable — write the local store so the editor works
        // standalone (daemon first, local store as the offline fallback).
        // saveTemplate normalizes the same way the daemon does. A settings
        // app open at this moment does not learn of the write (no broadcast
        // without the daemon, and nothing watches the directory); its read
        // view catches up on the daemon's next start-up rescan. Accepted for
        // this deliberately narrow offline case.
        PhosphorZones::ScrollingTemplate templ = PhosphorZones::ScrollingTemplate::fromJson(payload);
        const QUuid storedId = m_templateStore ? m_templateStore->saveTemplate(templ) : QUuid();
        if (storedId.isNull()) {
            Q_EMIT layoutSaveFailed(PhosphorI18n::tr("Could not save the template. Check that it has a name."));
            return false;
        }
        savedId = storedId.toString();
    }

    if (m_layoutId != savedId) {
        m_layoutId = savedId;
        Q_EMIT layoutIdChanged();
    }
    if (m_layoutName != saveName) {
        m_layoutName = saveName;
        Q_EMIT layoutNameChanged();
    }
    m_isNewLayout = false;
    const bool unsavedCleared = m_hasUnsavedChanges;
    m_hasUnsavedChanges = false;
    // Reconcile the model with what actually persisted: both paths normalize
    // (drop degenerate columns, sort/dedupe/cap presets, demote an
    // unusable Preset default), and fromJson applies the identical
    // normalization, so parsing our own payload yields the stored shape
    // without a re-read round-trip. resetState also drops the system flag —
    // the save wrote a user copy that shadows any bundled file.
    PhosphorZones::ScrollingTemplate stored = PhosphorZones::ScrollingTemplate::fromJson(payload);
    m_scrollingTemplate->resetState(EditorTemplateModel::stateFromTemplate(stored), /*isSystem*/ false);
    if (m_undoController) {
        m_undoController->setClean();
    }
    if (unsavedCleared) {
        Q_EMIT hasUnsavedChangesChanged();
    }
    Q_EMIT layoutSaved();
    return true;
}

} // namespace PlasmaZones
