// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorLaunchController.h"

#include "EditorAppAdaptor.h"
#include "EditorController.h"
#include "../core/logging.h"

#include <PhosphorProtocol/ServiceConstants.h>

namespace PlasmaZones {

EditorLaunchController::EditorLaunchController(EditorController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_singleInstance(std::make_unique<SingleInstanceService>(
          SingleInstanceIds{PhosphorProtocol::Service::Apps::Editor::ServiceName,
                            PhosphorProtocol::Service::Apps::Editor::ObjectPath,
                            PhosphorProtocol::Service::Apps::Editor::Interface},
          this))
{
    Q_ASSERT(m_controller);
    // Parent the adaptor to `this`. SingleInstanceService already has `this`
    // as its export object (set in the init list above), but it only
    // dereferences the export object later during claim() — by which point
    // the adaptor created below has been attached as a QObject child, so Qt
    // D-Bus's ExportAdaptors walk finds it. Destruction is automatic via
    // QObject parent/child.
    new EditorAppAdaptor(this);
}

EditorLaunchController::~EditorLaunchController() = default;

bool EditorLaunchController::registerDBusService()
{
    return m_singleInstance->claim();
}

void EditorLaunchController::applyLaunchArgs(const QString& screenId, const QString& layoutId, bool createNew,
                                             bool preview)
{
    // Translation only — the controller owns what the args MEAN and whether
    // they can be applied right now. A forwarded launch can land on an editor
    // holding unsaved edits, and `--new` / `--layout <id>` both replace the
    // loaded layout, so requestLaunch parks the request and has the UI ask
    // rather than destroying the work. An initial launch has a freshly
    // constructed controller with nothing unsaved, so it applies immediately.
    m_controller->requestLaunch(screenId, layoutId, createNew, preview);
}

void EditorLaunchController::handleLaunchRequest(const QString& screenId, const QString& layoutId, bool createNew,
                                                 bool preview)
{
    // Apply the forwarded CLI args to the running editor. Screen and layout
    // changes propagate through targetScreenChanged / layoutIdChanged signals,
    // which QML observes and reacts to (EditorWindow.qml calls
    // showFullScreenOnTargetScreen on screen changes, which handles the
    // destroy-and-remap dance for cross-monitor moves on Wayland).
    applyLaunchArgs(screenId, layoutId, createNew, preview);
}

} // namespace PlasmaZones
