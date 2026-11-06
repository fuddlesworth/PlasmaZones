// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/FacetContext.h"

#include "PhosphorControl/FacetHost.h"
#include "PhosphorControl/PageController.h"
#include "PhosphorControl/StagingDomain.h"

namespace PhosphorControl {

FacetContext::FacetContext(FacetHost* host, const FacetModuleMetadata& metadata)
    : m_host(host)
    , m_metadata(metadata)
{
}

QString FacetContext::moduleId() const
{
    return m_metadata.id;
}

const FacetModuleMetadata& FacetContext::metadata() const
{
    return m_metadata;
}

QString FacetContext::pageId(const QString& localPageId) const
{
    return FacetHost::pageId(m_metadata.id, localPageId);
}

bool FacetContext::registerPage(const QString& localPageId, PageController* controller, const QUrl& qmlSource)
{
    if (!m_metadata.page(localPageId)) {
        m_errors.append(QStringLiteral("registered page %1, which the metadata does not declare").arg(localPageId));
        return false;
    }
    if (m_registeredPages.contains(localPageId)) {
        m_errors.append(QStringLiteral("registered page %1 twice").arg(localPageId));
        return false;
    }
    if (!controller) {
        m_errors.append(QStringLiteral("registered page %1 with no controller").arg(localPageId));
        return false;
    }
    const QString scoped = pageId(localPageId);
    if (controller->id() != scoped) {
        m_errors.append(
            QStringLiteral("page %1's controller has id %2, expected %3").arg(localPageId, controller->id(), scoped));
        return false;
    }
    if (!m_host->bindPage(*m_host->m_modules.value(m_metadata.id), scoped, controller, qmlSource)) {
        m_errors.append(QStringLiteral("page %1 could not take its place in the sidebar").arg(localPageId));
        return false;
    }
    m_registeredPages.insert(localPageId);
    return true;
}

void FacetContext::registerDomain(StagingDomain* domain)
{
    if (!domain) {
        m_errors.append(QStringLiteral("registered a null domain"));
        return;
    }
    if (m_domains.contains(QPointer<StagingDomain>(domain))) {
        m_errors.append(QStringLiteral("registered a domain twice"));
        return;
    }
    m_host->addDomain(domain);
    m_domains.append(domain);
}

} // namespace PhosphorControl
