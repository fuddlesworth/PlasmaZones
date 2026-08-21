// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcmabout.h"
#include <QCoreApplication>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QTranslator>
#include <KPluginFactory>

K_PLUGIN_FACTORY_WITH_JSON(KCMAboutFactory, "kcm_plasmazones_about.json", registerPlugin<PlasmaZones::KCMAbout>();)

namespace PlasmaZones {

KCMAbout::KCMAbout(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    setButtons({});

    // We are a plugin inside systemsettings, which loads no PlasmaZones
    // catalog of its own, so without this every string on this page renders
    // English no matter the locale. Same lookup as the KWin effect uses for
    // the same reason: search the shared data locations rather than paths
    // relative to the host binary, since the host is not ours.
    //
    // The QML side calls qsTr() rather than i18n() precisely so this is all
    // that is needed. i18n() in QML requires a PhosphorLocalizedContext on the
    // engine, which lives in plasmazones_core, and linking that whole library
    // into a minimal About plugin would pull the daemon's world into the
    // systemsettings process to translate nine strings.
    auto* translator = new QTranslator(this);
    const QLocale locale;
    const QStringList dataDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString& dir : dataDirs) {
        if (translator->load(locale, QStringLiteral("plasmazones"), QStringLiteral("_"),
                             dir + QStringLiteral("/plasmazones/translations"))) {
            QCoreApplication::installTranslator(translator);
            break;
        }
    }
}

KCMAbout::~KCMAbout() = default;

QString KCMAbout::currentVersion() const
{
    return QStringLiteral(PLASMAZONES_VERSION);
}

void KCMAbout::openSettings(const QString& page)
{
    QStringList args;
    if (!page.isEmpty()) {
        args << QStringLiteral("--page") << page;
    }
    QProcess::startDetached(QStringLiteral("plasmazones-settings"), args);
}

} // namespace PlasmaZones

#include "kcmabout.moc"
