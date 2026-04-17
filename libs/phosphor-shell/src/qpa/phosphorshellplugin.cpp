// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "layershellintegration.h"
#include <QtWaylandClient/private/qwaylandshellintegrationplugin_p.h>

class PhosphorShellPlugin : public QtWaylandClient::QWaylandShellIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QWaylandShellIntegrationFactoryInterface_iid FILE "phosphorshell.json")

public:
    QtWaylandClient::QWaylandShellIntegration* create(const QString& key, const QStringList& paramList) override
    {
        Q_UNUSED(paramList)
        if (key == QLatin1String("phosphorshell")) {
            return new PhosphorShell::LayerShellIntegration();
        }
        return nullptr;
    }
};

#include "phosphorshellplugin.moc"
