// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorLaunchController.h"

#include "EditorAppAdaptor.h"
#include "EditorController.h"
#include "../core/constants.h"
#include "../core/logging.h"

namespace PlasmaZones {

EditorLaunchController::EditorLaunchController(EditorController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_singleInstance(std::make_unique<SingleInstanceService>(
          SingleInstanceIds{DBus::EditorApp::ServiceName, DBus::EditorApp::ObjectPath, DBus::EditorApp::Interface},
          this))
{
    Q_ASSERT(m_controller);
    // The adaptor parents itself to `this`, so Qt D-Bus picks it up when
    // SingleInstanceService::claim() registers this object at /EditorApp.
    // The adaptor is destroyed automatically with this QObject.
    new EditorAppAdaptor(this);
}

EditorLaunchController::~EditorLaunchController() = default;

bool EditorLaunchController::registerDBusService()
{
    return m_singleInstance && m_singleInstance->claim();
}

void EditorLaunchController::applyLaunchArgs(const QString& screenId, const QString& layoutId, bool createNew,
                                             bool preview)
{
    // The loadAssignedLayout flag distinguishes the "no layout arg" path,
    // which should follow the screen's assigned layout, from the paths where
    // we already know what layout to load and must not trigger a second load.
    auto maybeSwitchScreen = [this](const QString& id, bool loadAssignedLayout) {
        if (id.isEmpty() || m_controller->targetScreen() == id) {
            return;
        }
        if (loadAssignedLayout) {
            m_controller->setTargetScreen(id);
        } else {
            m_controller->setTargetScreenDirect(id);
        }
    };

    if (createNew) {
        m_controller->setPreviewMode(false);
        maybeSwitchScreen(screenId, /*loadAssignedLayout*/ false);
        m_controller->createNewLayout();
    } else if (!layoutId.isEmpty()) {
        m_controller->setPreviewMode(preview || LayoutId::isAutotile(layoutId));
        m_controller->loadLayout(layoutId);
        maybeSwitchScreen(screenId, /*loadAssignedLayout*/ false);
    } else {
        m_controller->setPreviewMode(false);
        maybeSwitchScreen(screenId, /*loadAssignedLayout*/ true);
    }
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
