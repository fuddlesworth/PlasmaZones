// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "PhosphorControl/FacetHost.h"

#include "PhosphorControl/ApplicationController.h"
#include "PhosphorControl/FacetContext.h"
#include "PhosphorControl/FacetModule.h"
#include "PhosphorControl/PageController.h"
#include "PhosphorControl/PageRegistry.h"
#include "facetplaceholderpage_p.h"

#include <PhosphorConfig/IBackend.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QStandardPaths>
#include <QVariantMap>

#include <algorithm>

namespace PhosphorControl {

namespace {
Q_LOGGING_CATEGORY(lcFacetHost, "phosphorcontrol.facethost")

const QString CategoryPrefix = QStringLiteral("@category/");
const QString DefaultCategory = QStringLiteral("apps");

// The page a module page shows until its module loads, and for good when the
// module cannot load. Navigating to the page loads the module first, so it is
// only ever seen with a reason.
const QUrl UnavailablePageUrl(QStringLiteral("qrc:/qt/qml/org/phosphor/control/qml/FacetUnavailablePage.qml"));

// The file may not exist yet, so the key is its cleaned absolute path with
// the directory's symlinks resolved when the directory exists, rather than
// the file's canonical path.
QString configFileKey(const QString& path)
{
    const QFileInfo info(QDir::cleanPath(path));
    const QString directory = QFileInfo(info.absolutePath()).canonicalFilePath();
    return directory.isEmpty() ? info.absoluteFilePath() : directory + QLatin1Char('/') + info.fileName();
}

QList<FacetCategoryMetadata> baseCategories()
{
    const auto name = [](const char* text) {
        return QCoreApplication::translate("PhosphorControl::FacetHost", text);
    };
    return {
        {QStringLiteral("window-management"), name("Window Management"), QStringLiteral("preferences-system-windows"),
         10},
        {QStringLiteral("appearance"), name("Appearance"), QStringLiteral("preferences-desktop-theme-global"), 20},
        {QStringLiteral("desktop"), name("Desktop"), QStringLiteral("preferences-desktop"), 30},
        {QStringLiteral("input"), name("Input"), QStringLiteral("preferences-desktop-peripherals"), 40},
        {QStringLiteral("apps"), name("Apps"), QStringLiteral("preferences-desktop-default-applications"), 50},
        {QStringLiteral("system"), name("System"), QStringLiteral("preferences-system"), 60},
    };
}
} // namespace

struct FacetHost::Module
{
    FacetModuleMetadata metadata;
    ModuleState state = ModuleState::Available;
    QString reason;
    bool nodeRegistered = false;
    QStringList boundPages;
    QList<QPointer<StagingDomain>> domains;
    struct Extension
    {
        QPointer<StagingDomain> domain;
        QUrl qmlSource;
    };
    QHash<QString, Extension> extensions;
};

struct FacetHost::ActiveExtension
{
    QString moduleId;
    QString localId;
    QString targetPage;
    QString slot;
    QString name;
    int order = 0;
};

PhosphorRegistry::PluginSeam FacetHost::seam()
{
    return PhosphorRegistry::PluginSeam{QStringLiteral("facet"), QStringLiteral(PhosphorFacetModule_iid), 1};
}

QString FacetHost::pageId(const QString& moduleId, const QString& localPageId)
{
    return moduleId + QLatin1Char(':') + localPageId;
}

QString FacetHost::categoryNodeId(const QString& categoryId)
{
    return CategoryPrefix + categoryId;
}

FacetHost::FacetHost(ApplicationController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_catalog(std::make_unique<PhosphorRegistry::PluginCatalog>(seam()))
    , m_sentinelPath(QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation)
                     + QStringLiteral("/phosphor-works/facet-loading"))
{
    if (!m_controller) {
        qFatal("FacetHost: controller must not be null");
    }
    for (const FacetCategoryMetadata& category : baseCategories()) {
        m_categories.insert(category.id, category);
    }
}

FacetHost::~FacetHost()
{
    if (m_controller) {
        m_controller->setNavigationHook({});
        m_controller->setExtensionProvider({});
    }
}

void FacetHost::setSearchPaths(const QList<PhosphorRegistry::PluginSearchPath>& paths)
{
    m_catalog->setSearchPaths(paths);
}

void FacetHost::setSentinelPath(const QString& path)
{
    m_sentinelPath = path;
}

void FacetHost::setLocale(const QLocale& locale)
{
    m_locale = locale;
}

void FacetHost::setLiveReload(bool enabled)
{
    m_catalog->setLiveReload(enabled);
}

void FacetHost::start()
{
    if (m_started) {
        qCWarning(lcFacetHost) << "FacetHost: start() called twice";
        return;
    }

    m_crashedModule = readSentinel();
    clearSentinel();
    if (!m_crashedModule.isEmpty()) {
        qCWarning(lcFacetHost).noquote() << "FacetHost: module" << m_crashedModule
                                         << "was loading when Facet last stopped unexpectedly; it stays disabled until "
                                            "it is retried";
    }

    QStringList found;
    const QMetaObject::Connection collect =
        connect(m_catalog.get(), &PhosphorRegistry::PluginCatalog::pluginAdded, this, [&found](const QString& id) {
            found.append(id);
        });
    m_catalog->scan();
    disconnect(collect);
    m_started = true;

    // Parse every module, then register categories, module nodes and pages in
    // an order where each parent exists before its children.
    QList<std::shared_ptr<Module>> parsed;
    for (const QString& id : std::as_const(found)) {
        const std::optional<PhosphorRegistry::PluginMetadata> plugin = m_catalog->plugin(id);
        if (!plugin) {
            continue;
        }
        auto module = std::make_shared<Module>();
        QStringList errors;
        module->metadata = FacetModuleMetadata::parse(plugin->metaData, m_locale, &errors);
        module->metadata.id = id;
        for (const FacetCategoryMetadata& category : std::as_const(module->metadata.categories)) {
            if (!m_categories.contains(category.id)) {
                m_categories.insert(category.id, category);
            }
        }
        m_modules.insert(id, module);
        if (!errors.isEmpty()) {
            setFailed(*module, QStringLiteral("invalid metadata: %1").arg(errors.join(QStringLiteral("; "))));
            continue;
        }
        if (id == m_crashedModule) {
            module->state = ModuleState::DisabledAfterCrash;
            module->reason = QStringLiteral("it was loading when Facet last stopped unexpectedly");
        }
        parsed.append(module);
    }

    std::sort(parsed.begin(), parsed.end(), [](const auto& a, const auto& b) {
        if (a->metadata.order != b->metadata.order) {
            return a->metadata.order < b->metadata.order;
        }
        return a->metadata.name.localeAwareCompare(b->metadata.name) < 0;
    });

    // Categories in use, in category order.
    QList<FacetCategoryMetadata> used;
    QSet<QString> usedIds;
    for (const auto& module : std::as_const(parsed)) {
        QString category = module->metadata.category.isEmpty() ? DefaultCategory : module->metadata.category;
        if (!m_categories.contains(category)) {
            qCInfo(lcFacetHost).noquote() << "FacetHost: module" << module->metadata.id << "names unknown category"
                                          << category << "- it goes under" << DefaultCategory;
            category = DefaultCategory;
        }
        module->metadata.category = category;
        if (!usedIds.contains(category)) {
            usedIds.insert(category);
            used.append(m_categories.value(category));
        }
    }
    std::sort(used.begin(), used.end(), [](const FacetCategoryMetadata& a, const FacetCategoryMetadata& b) {
        if (a.order != b.order) {
            return a.order < b.order;
        }
        return a.name.localeAwareCompare(b.name) < 0;
    });
    for (const FacetCategoryMetadata& category : std::as_const(used)) {
        ensureCategoryNode(category.id);
    }

    // Module nodes and their pages. A module nested under another waits for
    // that module's node. A pass that places nothing ends the loop, and what
    // is left goes under its category.
    QList<std::shared_ptr<Module>> pending = parsed;
    while (!pending.isEmpty()) {
        QList<std::shared_ptr<Module>> next;
        for (const auto& module : std::as_const(pending)) {
            const QString& parent = module->metadata.parent;
            const bool parentReady = parent.isEmpty() || !m_modules.contains(parent)
                || m_modules.value(parent)->nodeRegistered || m_modules.value(parent)->state == ModuleState::Failed;
            if (parentReady) {
                registerPlaceholders(*module);
            } else {
                next.append(module);
            }
        }
        if (next.size() == pending.size()) {
            for (const auto& module : std::as_const(next)) {
                qCInfo(lcFacetHost).noquote() << "FacetHost: module" << module->metadata.id
                                              << "is in a nesting cycle; it goes under its category";
                module->metadata.parent.clear();
                registerPlaceholders(*module);
            }
            break;
        }
        pending = next;
    }

    resolveExtensions();
    m_controller->setExtensionProvider([this](const QString& pageId, const QString& slot) {
        return extensionsFor(pageId, slot);
    });

    connect(m_catalog.get(), &PhosphorRegistry::PluginCatalog::pluginAdded, this, &FacetHost::addModule);
    connect(m_catalog.get(), &PhosphorRegistry::PluginCatalog::pluginRemoved, this, [](const QString& id) {
        qCInfo(lcFacetHost).noquote() << "FacetHost: module" << id << "was removed; its pages go at the next start";
    });
    m_controller->setNavigationHook([this](const QString& pageId) {
        onNavigation(pageId);
    });
}

void FacetHost::addModule(const QString& moduleId)
{
    if (m_modules.contains(moduleId)) {
        return;
    }
    const std::optional<PhosphorRegistry::PluginMetadata> plugin = m_catalog->plugin(moduleId);
    if (!plugin) {
        return;
    }
    auto module = std::make_shared<Module>();
    QStringList errors;
    module->metadata = FacetModuleMetadata::parse(plugin->metaData, m_locale, &errors);
    module->metadata.id = moduleId;
    m_modules.insert(moduleId, module);
    if (!errors.isEmpty()) {
        setFailed(*module, QStringLiteral("invalid metadata: %1").arg(errors.join(QStringLiteral("; "))));
        return;
    }
    for (const FacetCategoryMetadata& category : std::as_const(module->metadata.categories)) {
        if (!m_categories.contains(category.id)) {
            m_categories.insert(category.id, category);
        }
    }
    if (module->metadata.category.isEmpty() || !m_categories.contains(module->metadata.category)) {
        module->metadata.category = DefaultCategory;
    }
    ensureCategoryNode(module->metadata.category);
    registerPlaceholders(*module);
    const QStringList before = m_extensionsByPage.keys();
    resolveExtensions();
    QSet<QString> touched(before.cbegin(), before.cend());
    for (auto it = m_extensionsByPage.cbegin(); it != m_extensionsByPage.cend(); ++it) {
        touched.insert(it.key());
    }
    for (const QString& page : std::as_const(touched)) {
        m_controller->notifyExtensionsChanged(page);
    }
}

void FacetHost::ensureCategoryNode(const QString& categoryId)
{
    if (m_registeredCategoryNodes.contains(categoryId)) {
        return;
    }
    const FacetCategoryMetadata category = m_categories.value(categoryId);
    const QString id = categoryNodeId(categoryId);
    m_controller->registerPage(placeholder(id), QString(), category.name, QUrl(), category.icon, true);
    m_registeredCategoryNodes.insert(categoryId);
}

FacetPlaceholderPage* FacetHost::placeholder(const QString& id)
{
    auto* page = new FacetPlaceholderPage(id, this);
    m_placeholders.insert(id, page);
    return page;
}

bool FacetHost::canRetry(const Module& module)
{
    // A module that failed before binding anything can be tried again. One
    // with invalid metadata has no placeholder pages to bind into, and one
    // that bound pages keeps them: loading it twice would bind controllers
    // into pages that already have them.
    return module.state == ModuleState::DisabledAfterCrash
        || (module.state == ModuleState::Failed && module.nodeRegistered && module.boundPages.isEmpty());
}

void FacetHost::updateUnavailablePages(const Module& module)
{
    const bool retryable = canRetry(module);
    for (const FacetPageMetadata& page : module.metadata.pages) {
        FacetPlaceholderPage* const placeholder = m_placeholders.value(pageId(module.metadata.id, page.id));
        if (placeholder) {
            placeholder->setUnavailable(module.reason, retryable);
        }
    }
}

void FacetHost::registerPlaceholders(Module& module)
{
    PageRegistry* const registry = m_controller->registry();
    const FacetModuleMetadata& metadata = module.metadata;

    QString nodeParent = categoryNodeId(metadata.category);
    if (!metadata.parent.isEmpty()) {
        if (registry->hasPage(metadata.parent)) {
            nodeParent = metadata.parent;
        } else {
            qCInfo(lcFacetHost).noquote() << "FacetHost: module" << metadata.id << "nests under" << metadata.parent
                                          << "which is not installed; it goes under its category";
        }
    }
    m_controller->registerPage(placeholder(metadata.id), nodeParent, metadata.name, QUrl(), metadata.icon, true);
    module.nodeRegistered = true;

    QSet<QString> hasChildren;
    for (const FacetPageMetadata& page : metadata.pages) {
        if (!page.parent.isEmpty() && !page.parent.contains(QLatin1Char(':'))) {
            hasChildren.insert(page.parent);
        }
    }
    // Pages arrive parents-first within the module (FacetModuleMetadata::parse).
    for (const FacetPageMetadata& page : metadata.pages) {
        QString parent = metadata.id;
        if (page.parent.contains(QLatin1Char(':'))) {
            const QString target = page.parent;
            if (registry->hasPage(target)) {
                parent = target;
            } else {
                qCInfo(lcFacetHost).noquote() << "FacetHost: page" << pageId(metadata.id, page.id) << "goes under"
                                              << target << "which is not installed; it stays under its module";
            }
        } else if (!page.parent.isEmpty()) {
            parent = pageId(metadata.id, page.parent);
        }
        const QString scoped = pageId(metadata.id, page.id);
        FacetPlaceholderPage* const stand = placeholder(scoped);
        stand->setRetry([this, moduleId = metadata.id]() {
            QString error;
            if (!retryModule(moduleId, &error)) {
                qCWarning(lcFacetHost).noquote() << "FacetHost: retrying module" << moduleId << "failed -" << error;
            }
        });
        m_controller->registerPage(stand, parent, page.name, UnavailablePageUrl, page.icon,
                                   hasChildren.contains(page.id));
    }
    updateUnavailablePages(module);
}

QString FacetHost::owningModule(const QString& pageId) const
{
    if (pageId.startsWith(CategoryPrefix)) {
        return QString();
    }
    const qsizetype colon = pageId.indexOf(QLatin1Char(':'));
    const QString candidate = colon > 0 ? pageId.left(colon) : pageId;
    return m_modules.contains(candidate) ? candidate : QString();
}

void FacetHost::onNavigation(const QString& pageId)
{
    // The page's own module, then every module that adds a section to it, so
    // the page shows complete on its first paint.
    QStringList toLoad;
    const QString owner = owningModule(pageId);
    if (!owner.isEmpty()) {
        toLoad.append(owner);
    }
    const QList<std::shared_ptr<ActiveExtension>> extensions = m_extensionsByPage.value(pageId);
    for (const auto& extension : extensions) {
        if (!toLoad.contains(extension->moduleId)) {
            toLoad.append(extension->moduleId);
        }
    }
    for (const QString& moduleId : std::as_const(toLoad)) {
        const std::shared_ptr<Module> module = m_modules.value(moduleId);
        if (!module || module->state != ModuleState::Available) {
            continue;
        }
        QString error;
        if (!loadModule(moduleId, &error)) {
            qCWarning(lcFacetHost).noquote()
                << "FacetHost: opening" << pageId << "could not load module" << moduleId << "-" << error;
        }
    }
}

bool FacetHost::loadModule(const QString& moduleId, QString* errorString)
{
    const auto fail = [errorString](const QString& message) {
        if (errorString) {
            *errorString = message;
        }
        return false;
    };
    const std::shared_ptr<Module> module = m_modules.value(moduleId);
    if (!module) {
        return fail(QStringLiteral("no module %1").arg(moduleId));
    }
    switch (module->state) {
    case ModuleState::Loaded:
        return true;
    case ModuleState::Failed:
    case ModuleState::DisabledAfterCrash:
        return fail(module->reason);
    case ModuleState::Available:
        break;
    }

    writeSentinel(moduleId);
    QString loadError;
    QObject* const root = m_catalog->load(moduleId, &loadError);
    if (!root) {
        clearSentinel();
        setFailed(*module, loadError);
        return fail(loadError);
    }
    auto* const facetModule = qobject_cast<IFacetModule*>(root);
    if (!facetModule) {
        clearSentinel();
        const QString reason = QStringLiteral("its root object does not implement IFacetModule");
        setFailed(*module, reason);
        return fail(reason);
    }

    FacetContext context(this, module->metadata);
    facetModule->registerInto(context);
    clearSentinel();

    for (const FacetPageMetadata& page : std::as_const(module->metadata.pages)) {
        if (!context.m_registeredPages.contains(page.id)) {
            context.m_errors.append(QStringLiteral("did not register page %1").arg(page.id));
        }
    }
    for (const FacetExtensionMetadata& extension : std::as_const(module->metadata.extensions)) {
        if (!context.m_registeredExtensions.contains(extension.id)) {
            context.m_errors.append(QStringLiteral("did not register extension %1").arg(extension.id));
        }
    }
    module->domains = context.m_domains;
    if (!context.m_errors.isEmpty()) {
        const QString reason = context.m_errors.join(QStringLiteral("; "));
        setFailed(*module, reason);
        return fail(reason);
    }
    module->state = ModuleState::Loaded;
    module->reason.clear();

    // Its sections join the pages they are added to: dirty mark, reset, and
    // the pages' FacetSlots. Notified after the walk, since a slot handler
    // may navigate and so change the extension map.
    QStringList touched;
    for (auto it = m_extensionsByPage.cbegin(); it != m_extensionsByPage.cend(); ++it) {
        for (const auto& extension : it.value()) {
            if (extension->moduleId != moduleId) {
                continue;
            }
            const auto registered = module->extensions.constFind(extension->localId);
            if (registered != module->extensions.cend() && registered->domain) {
                m_controller->attachDomainToPage(registered->domain.data(), it.key());
                if (!touched.contains(it.key())) {
                    touched.append(it.key());
                }
            }
        }
    }
    for (const QString& page : std::as_const(touched)) {
        m_controller->notifyExtensionsChanged(page);
    }

    qCInfo(lcFacetHost).noquote() << "FacetHost: loaded module" << moduleId;
    Q_EMIT moduleLoaded(moduleId);
    return true;
}

bool FacetHost::retryModule(const QString& moduleId, QString* errorString)
{
    const std::shared_ptr<Module> module = m_modules.value(moduleId);
    if (module && canRetry(*module)) {
        module->state = ModuleState::Available;
        module->reason.clear();
    }
    return loadModule(moduleId, errorString);
}

bool FacetHost::bindPage(Module& module, const QString& scopedId, PageController* controller, const QUrl& qmlSource)
{
    if (!m_controller->bindPage(scopedId, controller, qmlSource)) {
        return false;
    }
    module.boundPages.append(scopedId);
    return true;
}

void FacetHost::addDomain(StagingDomain* domain)
{
    m_controller->registerDomain(domain);
}

void FacetHost::addExtension(Module& module, const QString& localId, StagingDomain* domain, const QUrl& qmlSource)
{
    module.extensions.insert(localId, Module::Extension{domain, qmlSource});
}

void FacetHost::resolveExtensions()
{
    m_extensionsByPage.clear();
    m_refusedExtensions.clear();
    for (auto moduleIt = m_modules.cbegin(); moduleIt != m_modules.cend(); ++moduleIt) {
        const Module& module = *moduleIt.value();
        if (!module.nodeRegistered) {
            continue; // invalid metadata: nothing of it is shown
        }
        for (const FacetExtensionMetadata& extension : module.metadata.extensions) {
            const QString scoped = pageId(module.metadata.id, extension.id);
            const QString targetModuleId = extension.target.section(QLatin1Char(':'), 0, 0);
            const QString targetLocalPage = extension.target.section(QLatin1Char(':'), 1);
            const std::shared_ptr<Module> target = m_modules.value(targetModuleId);
            if (!target || !target->nodeRegistered) {
                qCInfo(lcFacetHost).noquote() << "FacetHost: extension" << scoped << "targets" << extension.target
                                              << "whose module is not installed; it stays inactive";
                continue;
            }
            QString reason;
            if (!target->metadata.page(targetLocalPage)) {
                reason = QStringLiteral("its target page %1 does not exist").arg(extension.target);
            } else if (const int offered = target->metadata.slotVersion(targetLocalPage, extension.slot);
                       offered == 0) {
                reason = QStringLiteral("page %1 has no slot %2").arg(extension.target, extension.slot);
            } else if (offered != extension.targetVersion) {
                reason = QStringLiteral("it is built for version %1 of slot %2 on %3, which is at version %4")
                             .arg(QString::number(extension.targetVersion), extension.slot, extension.target,
                                  QString::number(offered));
            }
            if (!reason.isEmpty()) {
                m_refusedExtensions.append({scoped, reason});
                continue;
            }
            auto active = std::make_shared<ActiveExtension>();
            active->moduleId = module.metadata.id;
            active->localId = extension.id;
            active->targetPage = extension.target;
            active->slot = extension.slot;
            active->name = extension.name;
            active->order = extension.order;
            m_extensionsByPage[extension.target].append(active);
        }
    }
    for (auto it = m_extensionsByPage.begin(); it != m_extensionsByPage.end(); ++it) {
        std::sort(it->begin(), it->end(), [](const auto& a, const auto& b) {
            if (a->order != b->order) {
                return a->order < b->order;
            }
            if (a->moduleId != b->moduleId) {
                return a->moduleId < b->moduleId;
            }
            return a->localId < b->localId;
        });
    }
    std::sort(m_refusedExtensions.begin(), m_refusedExtensions.end(),
              [](const RefusedExtension& a, const RefusedExtension& b) {
                  return a.id < b.id;
              });
    // Resolution runs again whenever a module arrives, so warn once per
    // refused extension, not once per pass.
    for (const RefusedExtension& refused : std::as_const(m_refusedExtensions)) {
        if (!m_warnedExtensions.contains(refused.id)) {
            m_warnedExtensions.insert(refused.id);
            qCWarning(lcFacetHost).noquote() << "FacetHost: skipping extension" << refused.id << "-" << refused.reason;
        }
    }
}

QVariantList FacetHost::extensionsFor(const QString& pageId, const QString& slot) const
{
    QVariantList list;
    const QList<std::shared_ptr<ActiveExtension>> extensions = m_extensionsByPage.value(pageId);
    for (const auto& extension : extensions) {
        if (extension->slot != slot) {
            continue;
        }
        const std::shared_ptr<Module> module = m_modules.value(extension->moduleId);
        QVariantMap entry;
        entry.insert(QStringLiteral("id"), FacetHost::pageId(extension->moduleId, extension->localId));
        entry.insert(QStringLiteral("moduleId"), extension->moduleId);
        entry.insert(QStringLiteral("moduleName"), module->metadata.name);
        entry.insert(QStringLiteral("name"), extension->name);
        switch (module->state) {
        case ModuleState::Available:
            continue; // not loaded yet: opening the page loads it first
        case ModuleState::Loaded: {
            const auto registered = module->extensions.constFind(extension->localId);
            if (registered == module->extensions.cend()) {
                continue;
            }
            entry.insert(QStringLiteral("qmlSource"), registered->qmlSource);
            entry.insert(QStringLiteral("domain"), QVariant::fromValue<QObject*>(registered->domain.data()));
            break;
        }
        case ModuleState::Failed:
        case ModuleState::DisabledAfterCrash:
            entry.insert(QStringLiteral("error"), module->reason);
            break;
        }
        list.append(entry);
    }
    return list;
}

QList<FacetHost::RefusedExtension> FacetHost::refusedExtensions() const
{
    return m_refusedExtensions;
}

QStringList FacetHost::extensionsOn(const QString& pageId) const
{
    QStringList ids;
    const QList<std::shared_ptr<ActiveExtension>> extensions = m_extensionsByPage.value(pageId);
    for (const auto& extension : extensions) {
        ids.append(FacetHost::pageId(extension->moduleId, extension->localId));
    }
    return ids;
}

void FacetHost::setFailed(Module& module, const QString& reason)
{
    module.state = ModuleState::Failed;
    module.reason = reason;
    updateUnavailablePages(module);
    qCWarning(lcFacetHost).noquote() << "FacetHost: module" << module.metadata.id << "failed:" << reason;
    Q_EMIT moduleFailed(module.metadata.id, reason);
}

QStringList FacetHost::moduleIds() const
{
    QStringList ids = m_modules.keys();
    std::sort(ids.begin(), ids.end());
    return ids;
}

FacetHost::ModuleState FacetHost::state(const QString& moduleId) const
{
    const std::shared_ptr<Module> module = m_modules.value(moduleId);
    return module ? module->state : ModuleState::Failed;
}

QString FacetHost::stateReason(const QString& moduleId) const
{
    const std::shared_ptr<Module> module = m_modules.value(moduleId);
    return module ? module->reason : QStringLiteral("no module %1").arg(moduleId);
}

std::optional<FacetModuleMetadata> FacetHost::metadata(const QString& moduleId) const
{
    const std::shared_ptr<Module> module = m_modules.value(moduleId);
    if (!module) {
        return std::nullopt;
    }
    return module->metadata;
}

QList<PhosphorRegistry::RefusedPlugin> FacetHost::refusedModules() const
{
    return m_catalog->refused();
}

QList<StagingDomain*> FacetHost::moduleDomains(const QString& moduleId) const
{
    QList<StagingDomain*> domains;
    const std::shared_ptr<Module> module = m_modules.value(moduleId);
    if (module) {
        for (const QPointer<StagingDomain>& domain : module->domains) {
            if (domain) {
                domains.append(domain.data());
            }
        }
    }
    return domains;
}

PhosphorConfig::IBackend*
FacetHost::sharedConfigBackend(const QString& path,
                               const std::function<std::unique_ptr<PhosphorConfig::IBackend>(const QString&)>& create)
{
    const QString key = configFileKey(path);
    const auto existing = m_configBackends.constFind(key);
    if (existing != m_configBackends.cend()) {
        return existing->get();
    }
    std::unique_ptr<PhosphorConfig::IBackend> created = create ? create(key) : nullptr;
    if (!created) {
        return nullptr;
    }
    PhosphorConfig::IBackend* const backend = created.get();
    m_configBackends.insert(key, std::shared_ptr<PhosphorConfig::IBackend>(std::move(created)));
    return backend;
}

PhosphorConfig::IBackend* FacetHost::configBackend(const QString& path) const
{
    const QString key = configFileKey(path);
    const auto existing = m_configBackends.constFind(key);
    return existing != m_configBackends.cend() ? existing->get() : nullptr;
}

QStringList FacetHost::boundPages(const QString& moduleId) const
{
    const std::shared_ptr<Module> module = m_modules.value(moduleId);
    return module ? module->boundPages : QStringList();
}

QString FacetHost::readSentinel() const
{
    QFile file(m_sentinelPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

void FacetHost::writeSentinel(const QString& moduleId) const
{
    QDir().mkpath(QFileInfo(m_sentinelPath).absolutePath());
    QSaveFile file(m_sentinelPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(moduleId.toUtf8()) < 0 || !file.commit()) {
        qCWarning(lcFacetHost).noquote() << "FacetHost: could not write the loading sentinel" << m_sentinelPath;
    }
}

void FacetHost::clearSentinel() const
{
    QFile::remove(m_sentinelPath);
}

} // namespace PhosphorControl
