// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kcmabout.h"
#include "../common/daemoncontroller.h"
#include <QDesktopServices>
#include <QProcess>
#include <QUrl>
#include <KConfig>
#include <KConfigGroup>
#include <KPluginFactory>
#include <KSharedConfig>
#include "../../src/config/updatechecker.h"
#include "../../src/core/constants.h"
#include "../plasmazonesmoduledata.h"
#include "version.h"

K_PLUGIN_FACTORY_WITH_JSON(KCMAboutFactory, "kcm_plasmazones_about.json", registerPlugin<PlasmaZones::KCMAbout>();
                           registerPlugin<PlasmaZones::PlasmaZonesModuleData>();)

namespace PlasmaZones {

KCMAbout::KCMAbout(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
{
    // No Apply/Default buttons — daemon toggle is immediate
    setButtons({});

    // Daemon lifecycle management
    m_daemonController = std::make_unique<DaemonController>(nullptr);
    connect(m_daemonController.get(), &DaemonController::runningChanged, this, &KCMAbout::daemonRunningChanged);
    connect(m_daemonController.get(), &DaemonController::enabledChanged, this, &KCMAbout::daemonEnabledChanged);

    // Update checker
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::updateAvailableChanged, this, &KCMAbout::updateAvailableChanged);
    connect(m_updateChecker, &UpdateChecker::latestVersionChanged, this, &KCMAbout::latestVersionChanged);
    connect(m_updateChecker, &UpdateChecker::checkingChanged, this, &KCMAbout::checkingForUpdatesChanged);
    connect(m_updateChecker, &UpdateChecker::releaseUrlChanged, this, &KCMAbout::releaseUrlChanged);

    // Restore persisted dismissed version
    KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
    KConfigGroup updatesGroup = config->group(QStringLiteral("Updates"));
    m_dismissedUpdateVersion = updatesGroup.readEntry("DismissedUpdateVersion", QString());

    // Auto-check on load
    m_updateChecker->checkForUpdates();
}

KCMAbout::~KCMAbout() = default;

bool KCMAbout::isDaemonRunning() const
{
    return m_daemonController->isRunning();
}

bool KCMAbout::isDaemonEnabled() const
{
    return m_daemonController->isEnabled();
}

void KCMAbout::setDaemonEnabled(bool enabled)
{
    m_daemonController->setEnabled(enabled);
}

QString KCMAbout::currentVersion() const
{
    return UpdateChecker::currentVersion();
}

bool KCMAbout::checkingForUpdates() const
{
    return m_updateChecker ? m_updateChecker->isChecking() : false;
}

bool KCMAbout::updateAvailable() const
{
    return m_updateChecker ? m_updateChecker->updateAvailable() : false;
}

QString KCMAbout::latestVersion() const
{
    return m_updateChecker ? m_updateChecker->latestVersion() : QString();
}

QString KCMAbout::dismissedUpdateVersion() const
{
    return m_dismissedUpdateVersion;
}

void KCMAbout::setDismissedUpdateVersion(const QString& version)
{
    if (m_dismissedUpdateVersion != version) {
        m_dismissedUpdateVersion = version;

        // Persist to config
        KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("plasmazonesrc"));
        KConfigGroup group = config->group(QStringLiteral("Updates"));
        group.writeEntry("DismissedUpdateVersion", version);
        config->sync();

        Q_EMIT dismissedUpdateVersionChanged();
    }
}

void KCMAbout::checkForUpdates()
{
    if (m_updateChecker) {
        m_updateChecker->checkForUpdates();
    }
}

void KCMAbout::openReleaseUrl()
{
    QString url = m_updateChecker ? m_updateChecker->releaseUrl() : QString();
    if (!url.isEmpty()) {
        QDesktopServices::openUrl(QUrl(url));
    } else {
        QDesktopServices::openUrl(QUrl(GITHUB_RELEASES_URL));
    }
}

void KCMAbout::openSettings()
{
    QProcess::startDetached(QStringLiteral("plasmazones-settings"), {});
}

} // namespace PlasmaZones

#include "kcmabout.moc"
