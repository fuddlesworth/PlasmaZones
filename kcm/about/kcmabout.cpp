// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcmabout.h"
#include <QProcess>
#include <KPluginFactory>
#include "../../src/config/updatechecker.h"

K_PLUGIN_FACTORY_WITH_JSON(KCMAboutFactory, "kcm_plasmazones_about.json", registerPlugin<PlasmaZones::KCMAbout>();)

namespace PlasmaZones {

KCMAbout::KCMAbout(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    setButtons({});
}

KCMAbout::~KCMAbout() = default;

QString KCMAbout::currentVersion() const
{
    return UpdateChecker::currentVersion();
}

void KCMAbout::openSettings()
{
    QProcess::startDetached(QStringLiteral("plasmazones-settings"), {});
}

} // namespace PlasmaZones

#include "kcmabout.moc"
