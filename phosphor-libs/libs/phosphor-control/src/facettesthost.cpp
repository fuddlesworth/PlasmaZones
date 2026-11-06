// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/FacetTestHost.h"

#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/FacetHost.h"
#include "PhosphorControl/PageController.h"
#include "PhosphorControl/PageRegistry.h"
#include "PhosphorControl/StagingDomain.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>

namespace PhosphorControl {

namespace {

// Call @p trigger on @p domain and count how often @p signal fires within the
// timeout, plus a short grace period to catch a second emission.
template<typename Signal>
int countResults(StagingDomain* domain, Signal signal, void (StagingDomain::*trigger)(), int timeoutMs)
{
    int count = 0;
    QEventLoop loop;
    const QMetaObject::Connection connection = QObject::connect(domain, signal, &loop, [&count, &loop] {
        ++count;
        loop.quit();
    });
    (domain->*trigger)();
    if (count == 0) {
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
        loop.exec();
    }
    // A second result for the same call is the bug this check exists for.
    QElapsedTimer grace;
    grace.start();
    while (grace.elapsed() < 50) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    QObject::disconnect(connection);
    return count;
}

} // namespace

FacetTestHost::FacetTestHost(QString modulePath)
    : m_modulePath(std::move(modulePath))
{
}

void FacetTestHost::setResultTimeoutMs(int ms)
{
    m_resultTimeoutMs = ms > 0 ? ms : 2000;
}

QStringList FacetTestHost::run()
{
    QStringList problems;
    if (!QFileInfo(m_modulePath).isFile()) {
        return {QStringLiteral("no module file at %1").arg(m_modulePath)};
    }

    QTemporaryDir root;
    if (!root.isValid()) {
        return {QStringLiteral("cannot create a temporary directory")};
    }
    const QString searchPath = root.filePath(QStringLiteral("facet"));
    QDir().mkpath(searchPath);
    const QFileDevice::Permissions mode = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
        | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::ExeOther;
    QFile::setPermissions(searchPath, mode);
    const QString staged = searchPath + QLatin1Char('/') + QFileInfo(m_modulePath).fileName();
    if (!QFile::copy(m_modulePath, staged)) {
        return {QStringLiteral("cannot copy %1 into the test search path").arg(m_modulePath)};
    }
    QFile::setPermissions(staged, mode);

    ApplicationController controller;
    FacetHost host(&controller);
    host.setSearchPaths({{searchPath, PhosphorRegistry::PluginOrigin::Development}});
    host.setSentinelPath(root.filePath(QStringLiteral("facet-loading")));
    host.setLiveReload(false);
    host.start();

    for (const PhosphorRegistry::RefusedPlugin& refused : host.refusedModules()) {
        problems.append(QStringLiteral("refused: %1").arg(refused.reason));
    }
    const QStringList modules = host.moduleIds();
    if (modules.isEmpty()) {
        if (problems.isEmpty()) {
            problems.append(QStringLiteral("no Facet module found in %1").arg(m_modulePath));
        }
        return problems;
    }
    const QString moduleId = modules.first();

    QString error;
    if (!host.loadModule(moduleId, &error)) {
        problems.append(QStringLiteral("module %1 did not load: %2").arg(moduleId, error));
        return problems;
    }

    const std::optional<FacetModuleMetadata> metadata = host.metadata(moduleId);
    QList<QPointer<StagingDomain>> domains;
    for (const FacetPageMetadata& page : metadata->pages) {
        PageController* const pageController = controller.registry()->controller(FacetHost::pageId(moduleId, page.id));
        if (pageController) {
            domains.append(pageController);
        }
    }
    for (StagingDomain* domain : host.moduleDomains(moduleId)) {
        domains.append(domain);
    }

    for (const QPointer<StagingDomain>& domain : std::as_const(domains)) {
        if (!domain) {
            continue;
        }
        const QString name = qobject_cast<PageController*>(domain.data())
            ? qobject_cast<PageController*>(domain.data())->id()
            : QString::fromLatin1(domain->metaObject()->className());
        const int applied =
            countResults(domain.data(), &StagingDomain::applyResult, &StagingDomain::apply, m_resultTimeoutMs);
        if (applied != 1) {
            problems.append(
                QStringLiteral("%1 emitted applyResult %2 times for one apply()").arg(name, QString::number(applied)));
        }
        if (!domain) {
            problems.append(QStringLiteral("%1 was deleted by apply()").arg(name));
            continue;
        }
        const int discarded =
            countResults(domain.data(), &StagingDomain::discardResult, &StagingDomain::discard, m_resultTimeoutMs);
        if (discarded != 1) {
            problems.append(QStringLiteral("%1 emitted discardResult %2 times for one discard()")
                                .arg(name, QString::number(discarded)));
        }
    }
    return problems;
}

} // namespace PhosphorControl
