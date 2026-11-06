// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "PhosphorControl/FacetModuleMetadata.h"
#include "phosphorcontrol_export.h"

#include <QList>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtCore/qtclasshelpermacros.h>

#include <functional>
#include <memory>

namespace PhosphorConfig {
class IBackend;
}

namespace PhosphorControl {

class FacetHost;
class PageController;
class StagingDomain;

/// What Facet hands a module while it loads (IFacetModule::registerInto).
///
/// Page ids are scoped: a module declares `snapping/behavior` in its metadata,
/// and Facet knows the page as `placement:snapping/behavior`, so two modules
/// can never collide. A module builds each PageController with
/// pageId(localId) and registers it under its local id.
class PHOSPHORCONTROL_EXPORT FacetContext
{
public:
    Q_DISABLE_COPY_MOVE(FacetContext)

    [[nodiscard]] QString moduleId() const;
    [[nodiscard]] const FacetModuleMetadata& metadata() const;

    /// The scoped id of the module's page @p localPageId.
    [[nodiscard]] QString pageId(const QString& localPageId) const;

    /// Register the controller and QML page for the metadata page
    /// @p localPageId. The controller's id() must be pageId(localPageId).
    /// Refused, and recorded as a module error, for a page the metadata does
    /// not declare, a page already registered, or a mismatched id.
    bool registerPage(const QString& localPageId, PageController* controller, const QUrl& qmlSource);

    /// Register a staging domain that has no page of its own, so it joins
    /// Apply, Discard and the dirty flag.
    void registerDomain(StagingDomain* domain);

    /// Register the section for the extension @p localExtensionId the
    /// metadata declares: its staging domain, which joins Apply, Discard and
    /// the dirty flag and counts toward the page it is added to, and the QML
    /// the page's FacetSlot shows. Every declared extension must be
    /// registered, whether or not its target is installed.
    bool registerExtension(const QString& localExtensionId, StagingDomain* domain, const QUrl& qmlSource);

    using ConfigBackendFactory = std::function<std::unique_ptr<PhosphorConfig::IBackend>(const QString& path)>;
    /// The config backend for the file at @p path, shared by every module
    /// that opens the same file, so one Apply writes each module's keys into
    /// one in-memory document and no module overwrites another's. The first
    /// module to open a file creates its backend with @p create; later ones
    /// get that backend and @p create is not called, so modules sharing a
    /// file must set its backend up alike. Each module wraps the backend in
    /// its own PhosphorConfig::Store or settings object.
    ///
    /// The backend lives as long as the FacetHost. Do not use it from a
    /// destructor. Returns null, and records a module error, when @p path is
    /// not absolute or @p create returns null.
    PhosphorConfig::IBackend* configBackend(const QString& path, const ConfigBackendFactory& create);

private:
    friend class FacetHost;
    FacetContext(FacetHost* host, const FacetModuleMetadata& metadata);

    FacetHost* m_host = nullptr;
    const FacetModuleMetadata& m_metadata;
    QSet<QString> m_registeredPages;
    QSet<QString> m_registeredExtensions;
    QList<QPointer<StagingDomain>> m_domains;
    QStringList m_errors;
};

} // namespace PhosphorControl
