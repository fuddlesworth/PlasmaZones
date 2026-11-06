// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/FacetTestHost.h"

#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/FacetHost.h"
#include "PhosphorControl/ISearchProvider.h"
#include "PhosphorControl/PageController.h"
#include "PhosphorControl/PageRegistry.h"
#include "PhosphorControl/StagingDomain.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPluginLoader>
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

// The search entries of the module's providers that do not resolve.
QStringList checkSearchProviders(const FacetHost& host, const QString& moduleId, const FacetModuleMetadata& metadata)
{
    QStringList problems;
    QSet<QString> extended;
    for (const FacetExtensionMetadata& extension : metadata.extensions) {
        extended.insert(extension.target);
    }
    for (const ISearchProvider* provider : host.searchProviders(moduleId)) {
        const QVector<SearchEntry> entries = provider->searchEntries();
        for (const SearchEntry& entry : entries) {
            if (entry.kind == SearchEntry::Kind::Action) {
                continue;
            }
            const QString where =
                entry.anchor.isEmpty() ? entry.pageId : entry.pageId + QLatin1Char('#') + entry.anchor;
            const bool ownPage = entry.pageId.startsWith(moduleId + QLatin1Char(':'))
                && metadata.page(entry.pageId.mid(moduleId.size() + 1));
            if (!ownPage && !extended.contains(entry.pageId)) {
                problems.append(QStringLiteral("search entry %1 (%2) is not on one of the module's pages or a page "
                                               "it extends")
                                    .arg(entry.title, where));
                continue;
            }
            const bool anchored = entry.kind == SearchEntry::Kind::Setting || entry.kind == SearchEntry::Kind::Section;
            if (ownPage && anchored && !entry.anchor.isEmpty()
                && !metadata.page(entry.pageId.mid(moduleId.size() + 1))->anchors.contains(entry.anchor)) {
                problems.append(QStringLiteral("search entry %1 (%2) names an anchor its page does not list in "
                                               "\"Anchors\"")
                                    .arg(entry.title, where));
            }
        }
    }
    return problems;
}

// Every page and anchor of the previous release that is gone without a bump
// of its page's anchors version.
QStringList checkAnchors(const QString& path, const FacetModuleMetadata& metadata)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {QStringLiteral("cannot read the previous anchors %1: %2").arg(path, file.errorString())};
    }
    const QJsonObject previous = QJsonDocument::fromJson(file.readAll()).object();
    if (previous.value(QLatin1String("Module")).toString() != metadata.id) {
        return {QStringLiteral("the previous anchors %1 are for module %2, not %3")
                    .arg(path, previous.value(QLatin1String("Module")).toString(), metadata.id)};
    }
    QStringList problems;
    const QJsonObject pages = previous.value(QLatin1String("Pages")).toObject();
    for (auto it = pages.constBegin(); it != pages.constEnd(); ++it) {
        const FacetPageMetadata* const page = metadata.page(it.key());
        if (!page) {
            problems.append(
                QStringLiteral("page %1 of the previous release is gone; other modules may extend it").arg(it.key()));
            continue;
        }
        const int was = it.value().toObject().value(QLatin1String("Version")).toInt(1);
        const int now = metadata.slotVersion(page->id, QStringLiteral("top"));
        if (now < was) {
            problems.append(QStringLiteral("page %1's anchors version went down from %2 to %3")
                                .arg(page->id, QString::number(was), QString::number(now)));
            continue;
        }
        if (now > was) {
            continue; // extensions built for the old anchors are refused, as they should be
        }
        const QJsonArray anchors = it.value().toObject().value(QLatin1String("Anchors")).toArray();
        for (const QJsonValue& anchor : anchors) {
            if (!page->anchors.contains(anchor.toString())) {
                problems.append(QStringLiteral("anchor %1 of page %2 is gone, but the page's anchors version is "
                                               "still %3; raise the \"anchors\" extension point's version")
                                    .arg(anchor.toString(), page->id, QString::number(now)));
            }
        }
    }
    return problems;
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

void FacetTestHost::addTargetModule(const QString& modulePath)
{
    m_targetPaths.append(modulePath);
}

void FacetTestHost::setPreviousAnchors(const QString& anchorsPath)
{
    m_previousAnchorsPath = anchorsPath;
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
    for (const QString& path : QStringList{m_modulePath} + m_targetPaths) {
        const QString staged = searchPath + QLatin1Char('/') + QFileInfo(path).fileName();
        if (!QFile::copy(path, staged)) {
            return {QStringLiteral("cannot copy %1 into the test search path").arg(path)};
        }
        QFile::setPermissions(staged, mode);
    }
    // The module under test, by the id in its embedded metadata.
    const QString moduleId = QPluginLoader(m_modulePath)
                                 .metaData()
                                 .value(QLatin1String("MetaData"))
                                 .toObject()
                                 .value(QLatin1String("Id"))
                                 .toString();

    ApplicationController controller;
    FacetHost host(&controller);
    host.setSearchPaths({{searchPath, PhosphorRegistry::PluginOrigin::Development}});
    host.setSentinelPath(root.filePath(QStringLiteral("facet-loading")));
    host.setLiveReload(false);
    host.start();

    for (const PhosphorRegistry::RefusedPlugin& refused : host.refusedModules()) {
        problems.append(QStringLiteral("refused: %1").arg(refused.reason));
    }
    if (!host.moduleIds().contains(moduleId)) {
        if (problems.isEmpty()) {
            problems.append(QStringLiteral("no Facet module found in %1").arg(m_modulePath));
        }
        return problems;
    }

    QString error;
    if (!host.loadModule(moduleId, &error)) {
        problems.append(QStringLiteral("module %1 did not load: %2").arg(moduleId, error));
        return problems;
    }

    const std::optional<FacetModuleMetadata> metadata = host.metadata(moduleId);
    problems.append(checkSearchProviders(host, moduleId, *metadata));
    const QString ownPrefix = moduleId + QLatin1Char(':');
    for (const FacetHost::RefusedExtension& refused : host.refusedExtensions()) {
        if (refused.id.startsWith(ownPrefix)) {
            problems.append(QStringLiteral("extension %1 is refused: %2").arg(refused.id, refused.reason));
        }
    }
    if (!m_previousAnchorsPath.isEmpty()) {
        problems.append(checkAnchors(m_previousAnchorsPath, *metadata));
    }
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
