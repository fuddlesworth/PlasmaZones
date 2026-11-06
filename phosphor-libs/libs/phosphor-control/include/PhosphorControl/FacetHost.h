// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once

#include "PhosphorControl/FacetModuleMetadata.h"
#include "phosphorcontrol_export.h"

#include <PhosphorRegistry/PluginCatalog.h>

#include <QHash>
#include <QList>
#include <QLocale>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <functional>
#include <memory>
#include <optional>

namespace PhosphorConfig {
class IBackend;
}

namespace PhosphorControl {

class ApplicationController;
class FacetContext;
class FacetPlaceholderPage;
class ISearchProvider;
class SearchController;
class PageController;
class StagingDomain;

/// Hosts Facet modules in an ApplicationController.
///
/// start() reads every module's metadata through a PluginCatalog over the
/// `facet` seam, without loading any module, and fills the page registry with
/// placeholder pages: one node per category in use, one per module, and one
/// per module page, all with their real titles and icons. Navigating to a
/// module's page or node loads the module (synchronously, through
/// ApplicationController's navigation hook), and the module's registerInto
/// binds its real controllers into the placeholders.
///
/// Page ids: a category node is `@category/<id>`, a module node is the module
/// id, and a module page is `<module>:<page>` (FacetContext::pageId). Neither
/// form can collide with another, because module ids contain neither `@`
/// nor `:`.
///
/// Extensions: a module's metadata can add sections to another module's
/// page. start() resolves each against its target page's slots and their
/// versions, and lists the ones that do not fit in refusedExtensions(). An
/// extension aimed at a module that is not installed is skipped quietly.
/// Opening a page also loads every module that extends it, and the host
/// answers ApplicationController::extensionsFor() for the page's FacetSlots.
///
/// A crash while a module loads is remembered: the module's id is written to a
/// sentinel file before registerInto and removed after. A sentinel left at the
/// next start marks that module DisabledAfterCrash until retryModule().
///
/// GUI thread only. The ApplicationController must outlive the host.
class PHOSPHORCONTROL_EXPORT FacetHost : public QObject
{
    Q_OBJECT
public:
    enum class ModuleState : quint8 {
        /// Found and on the sidebar, not loaded yet.
        Available,
        Loaded,
        /// Its metadata is invalid, its library did not load, or its
        /// registerInto did not match its metadata. See stateReason().
        Failed,
        /// It was loading when Facet last crashed.
        DisabledAfterCrash,
    };
    Q_ENUM(ModuleState)

    /// The `facet` seam: `<root>/facet/`, PhosphorFacetModule_iid, ApiVersion 1.
    [[nodiscard]] static PhosphorRegistry::PluginSeam seam();

    /// The scoped page id of @p localPageId in @p moduleId.
    [[nodiscard]] static QString pageId(const QString& moduleId, const QString& localPageId);
    /// The page id of category @p categoryId's node.
    [[nodiscard]] static QString categoryNodeId(const QString& categoryId);

    explicit FacetHost(ApplicationController* controller, QObject* parent = nullptr);
    ~FacetHost() override;

    /// Directories to search for modules, highest priority first. Must be set
    /// before start(). Default: PluginCatalog::defaultSearchPaths("facet").
    void setSearchPaths(const QList<PhosphorRegistry::PluginSearchPath>& paths);
    /// The file that records a module being loaded. Must be set before
    /// start(). Default: `$XDG_STATE_HOME/phosphor-works/facet-loading`.
    void setSentinelPath(const QString& path);
    /// The locale module metadata is read in. Default: QLocale().
    void setLocale(const QLocale& locale);
    /// Turn the catalog's live reload off, for tests. Before start().
    void setLiveReload(bool enabled);
    /// Index every module's pages, search entries and extensions in
    /// @p search, which must outlive the host. Set before start(). A module
    /// whose metadata says `SearchProviders` loads the first time the query
    /// is not empty, so its dynamic providers join the results.
    void setSearchController(SearchController* search);

    /// Find the modules and build the placeholder sidebar. Call once.
    void start();

    /// Load @p moduleId now. True when it is loaded, at once or already.
    bool loadModule(const QString& moduleId, QString* errorString = nullptr);
    /// Clear DisabledAfterCrash or Failed for @p moduleId and load it again.
    bool retryModule(const QString& moduleId, QString* errorString = nullptr);

    /// The modules on offer, sorted.
    [[nodiscard]] QStringList moduleIds() const;
    [[nodiscard]] ModuleState state(const QString& moduleId) const;
    [[nodiscard]] QString stateReason(const QString& moduleId) const;
    [[nodiscard]] std::optional<FacetModuleMetadata> metadata(const QString& moduleId) const;
    /// Module files the catalog refused, with the reason.
    [[nodiscard]] QList<PhosphorRegistry::RefusedPlugin> refusedModules() const;

    /// An extension that will not be shown, and why: its target page does not
    /// exist, has no such slot, or offers the slot at another version. An
    /// extension whose target module is not installed is not refused, only
    /// inactive.
    struct RefusedExtension
    {
        /// `<module>:<extension>`.
        QString id;
        QString reason;
    };
    [[nodiscard]] QList<RefusedExtension> refusedExtensions() const;
    /// The scoped ids of the extensions that will be shown on @p pageId, in
    /// slot order.
    [[nodiscard]] QStringList extensionsOn(const QString& pageId) const;

    /// The domains @p moduleId registered while loading: headless ones and its
    /// extensions'.
    [[nodiscard]] QList<StagingDomain*> moduleDomains(const QString& moduleId) const;
    /// The page ids the module of @p moduleId registered while loading.
    [[nodiscard]] QStringList boundPages(const QString& moduleId) const;
    /// The search providers the module of @p moduleId registered while loading.
    [[nodiscard]] QList<ISearchProvider*> searchProviders(const QString& moduleId) const;

    /// The config backend modules share for the file at @p path, or null
    /// when no module opened it (FacetContext::configBackend).
    [[nodiscard]] PhosphorConfig::IBackend* configBackend(const QString& path) const;

Q_SIGNALS:
    void moduleLoaded(const QString& moduleId);
    void moduleFailed(const QString& moduleId, const QString& reason);

private:
    friend class FacetContext;
    struct Module;

    void addModule(const QString& moduleId);
    void registerPlaceholders(Module& module);
    void onNavigation(const QString& pageId);
    [[nodiscard]] QString owningModule(const QString& pageId) const;
    bool bindPage(Module& module, const QString& scopedId, PageController* controller, const QUrl& qmlSource);
    void addDomain(StagingDomain* domain);
    void addExtension(Module& module, const QString& localId, StagingDomain* domain, const QUrl& qmlSource);
    void addSearchProvider(ISearchProvider* provider);
    void indexModule(const Module& module);
    void indexExtensions();
    void loadSearchProviders();
    PhosphorConfig::IBackend*
    sharedConfigBackend(const QString& path,
                        const std::function<std::unique_ptr<PhosphorConfig::IBackend>(const QString&)>& create);
    void resolveExtensions();
    [[nodiscard]] QVariantList extensionsFor(const QString& pageId, const QString& slot) const;
    FacetPlaceholderPage* placeholder(const QString& id);
    [[nodiscard]] static bool canRetry(const Module& module);
    void updateUnavailablePages(const Module& module);
    void ensureCategoryNode(const QString& categoryId);
    void setFailed(Module& module, const QString& reason);
    [[nodiscard]] QString readSentinel() const;
    void writeSentinel(const QString& moduleId) const;
    void clearSentinel() const;

    QPointer<ApplicationController> m_controller;
    QPointer<SearchController> m_search;
    QSet<QString> m_indexedExtensions;
    bool m_searchProvidersLoaded = false;
    std::unique_ptr<PhosphorRegistry::PluginCatalog> m_catalog;
    QString m_sentinelPath;
    QLocale m_locale;
    bool m_started = false;
    QString m_crashedModule;
    QHash<QString, std::shared_ptr<Module>> m_modules;
    QHash<QString, FacetCategoryMetadata> m_categories;
    QSet<QString> m_registeredCategoryNodes;
    QHash<QString, FacetPlaceholderPage*> m_placeholders;
    struct ActiveExtension;
    QHash<QString, QList<std::shared_ptr<ActiveExtension>>> m_extensionsByPage;
    QList<RefusedExtension> m_refusedExtensions;
    QSet<QString> m_warnedExtensions;
    // Shared, not unique: QHash values must be copyable. Each backend writes
    // its unsaved changes when it is destroyed.
    QHash<QString, std::shared_ptr<PhosphorConfig::IBackend>> m_configBackends;
};

} // namespace PhosphorControl
