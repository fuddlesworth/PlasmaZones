// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "EditorAppAdaptor.h"

#include "EditorLaunchController.h"

namespace PlasmaZones {

EditorAppAdaptor::EditorAppAdaptor(EditorLaunchController* launcher)
    : QDBusAbstractAdaptor(launcher)
    , m_launcher(launcher)
{
    setAutoRelaySignals(false);
}

EditorAppAdaptor::~EditorAppAdaptor() = default;

void EditorAppAdaptor::handleLaunchRequest(const QString& screenId, const QString& layoutId, bool createNew,
                                           bool preview)
{
    if (m_launcher) {
        m_launcher->handleLaunchRequest(screenId, layoutId, createNew, preview);
    }
}

} // namespace PlasmaZones
