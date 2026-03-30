// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "layershellintegration.h"
#include <QtWaylandClient/private/qwaylandshellintegrationplugin_p.h>

class PzLayerShellPlugin : public QtWaylandClient::QWaylandShellIntegrationPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QWaylandShellIntegrationFactoryInterface_iid FILE "pz-layer-shell.json")

public:
    QtWaylandClient::QWaylandShellIntegration* create(const QString& key, const QStringList& paramList) override
    {
        Q_UNUSED(paramList)
        if (key == QLatin1String("pz-layer-shell")) {
            return new PlasmaZones::LayerShellIntegration();
        }
        return nullptr;
    }
};

#include "pzlayershellplugin.moc"
