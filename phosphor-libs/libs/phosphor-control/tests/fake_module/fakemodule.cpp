// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// One source for every Facet test module. tests/CMakeLists.txt builds it once
// per variant with its own fake_module_config.h and metadata.json, so the
// variants differ only in their metadata and in FAKE_MODE:
//   0  register every declared page and extension, and one headless domain
//   1  skip the last declared page
//   2  also register a page the metadata does not declare
//   3  register the first page with a controller whose id is wrong
//   4  every domain emits applyResult twice per apply()
//   5  skip the last declared extension
//   6  also register an extension the metadata does not declare, and the
//      first declared extension a second time
//   7  also open the config file $FAKE_CONFIG_PATH and register a domain
//      that writes ["Fake"][<module id>] = "applied" there on apply()
//   8  open a config file by a relative path
//   9  also register a search provider with an entity entry and a setting
//      entry on the first page, at its first anchor
//  10  also register a search provider with an entry on a page that is not
//      the module's
//
// A domain that is not a page is dirty while its dynamic property
// "fakeDirty" is true, so a test can dirty an extension's section with
// setProperty() without linking this file.

#include "fake_module_config.h"

#include <PhosphorControl/FacetContext.h>
#include <PhosphorControl/FacetModule.h>
#include <PhosphorControl/ISearchProvider.h>
#include <PhosphorControl/PageController.h>
#include <PhosphorControl/StagingDomain.h>

#include <PhosphorConfig/IBackend.h>
#include <PhosphorConfig/JsonBackend.h>

#include <QEvent>
#include <QObject>
#include <QUrl>

#include <memory>

namespace {

void emitApply(PhosphorControl::StagingDomain* domain)
{
    Q_EMIT domain->applyResult(true, QString());
    if (FAKE_MODE == 4) {
        Q_EMIT domain->applyResult(true, QString());
    }
}

class FakePage : public PhosphorControl::PageController
{
public:
    FakePage(const QString& id, QObject* parent)
        : PageController(id, parent)
    {
    }
    bool isDirty() const override
    {
        return m_dirty;
    }
    void setDirty(bool dirty)
    {
        if (m_dirty != dirty) {
            m_dirty = dirty;
            Q_EMIT dirtyChanged();
        }
    }
    void apply() override
    {
        setDirty(false);
        emitApply(this);
    }
    void discard() override
    {
        setDirty(false);
        Q_EMIT discardResult(true, QString());
    }

private:
    bool m_dirty = false;
};

class FakeDomain : public PhosphorControl::StagingDomain
{
public:
    using StagingDomain::StagingDomain;
    bool isDirty() const override
    {
        return property("fakeDirty").toBool();
    }
    void apply() override
    {
        setProperty("fakeDirty", false);
        emitApply(this);
    }
    void discard() override
    {
        setProperty("fakeDirty", false);
        Q_EMIT discardResult(true, QString());
    }
    void resetToDefaults() override
    {
        setProperty("fakeDirty", false);
    }

protected:
    bool event(QEvent* event) override
    {
        if (event->type() == QEvent::DynamicPropertyChange && isDirty() != m_wasDirty) {
            m_wasDirty = isDirty();
            Q_EMIT dirtyChanged();
        }
        return StagingDomain::event(event);
    }

private:
    bool m_wasDirty = false;
};

// Writes its module's key into a shared config file on apply().
class ConfigDomain : public FakeDomain
{
public:
    ConfigDomain(PhosphorConfig::IBackend* backend, QString key, QObject* parent)
        : FakeDomain(parent)
        , m_backend(backend)
        , m_key(std::move(key))
    {
    }
    void apply() override
    {
        m_backend->group(QStringLiteral("Fake"))->writeString(m_key, QStringLiteral("applied"));
        m_backend->commit();
        FakeDomain::apply();
    }

private:
    PhosphorConfig::IBackend* m_backend;
    QString m_key;
};

class FakeProvider : public PhosphorControl::ISearchProvider
{
public:
    explicit FakeProvider(QVector<PhosphorControl::SearchEntry> entries)
        : m_entries(std::move(entries))
    {
    }
    QVector<PhosphorControl::SearchEntry> searchEntries() const override
    {
        return m_entries;
    }

private:
    QVector<PhosphorControl::SearchEntry> m_entries;
};

} // namespace

class FakeFacetModule : public QObject, public PhosphorControl::IFacetModule
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID PhosphorFacetModule_iid FILE "facet_metadata.json")
    Q_INTERFACES(PhosphorControl::IFacetModule)

public:
    void registerInto(PhosphorControl::FacetContext& context) override
    {
        const auto& pages = context.metadata().pages;
        for (qsizetype i = 0; i < pages.size(); ++i) {
            if (FAKE_MODE == 1 && i == pages.size() - 1) {
                continue;
            }
            const QString local = pages.at(i).id;
            const QString id = (FAKE_MODE == 3 && i == 0) ? QStringLiteral("wrong:id") : context.pageId(local);
            context.registerPage(local, new FakePage(id, this),
                                 QUrl(QStringLiteral("qrc:/fake/") + local + QStringLiteral(".qml")));
        }
        if (FAKE_MODE == 2) {
            context.registerPage(QStringLiteral("undeclared"),
                                 new FakePage(context.pageId(QStringLiteral("undeclared")), this), QUrl());
        }
        context.registerDomain(new FakeDomain(this));

        const auto& extensions = context.metadata().extensions;
        for (qsizetype i = 0; i < extensions.size(); ++i) {
            if (FAKE_MODE == 5 && i == extensions.size() - 1) {
                continue;
            }
            const QString local = extensions.at(i).id;
            auto* domain = new FakeDomain(this);
            domain->setObjectName(local);
            context.registerExtension(local, domain,
                                      QUrl(QStringLiteral("qrc:/fake/extension/") + local + QStringLiteral(".qml")));
        }
        if (FAKE_MODE == 7 || FAKE_MODE == 8) {
            const QString path =
                FAKE_MODE == 7 ? qEnvironmentVariable("FAKE_CONFIG_PATH") : QStringLiteral("relative.json");
            PhosphorConfig::IBackend* const backend = context.configBackend(path, [](const QString& file) {
                return std::make_unique<PhosphorConfig::JsonBackend>(file);
            });
            if (backend) {
                auto* domain = new ConfigDomain(backend, context.moduleId(), this);
                domain->setObjectName(QStringLiteral("config"));
                context.registerDomain(domain);
            }
        }
        if (FAKE_MODE == 9 || FAKE_MODE == 10) {
            using PhosphorControl::SearchEntry;
            QVector<SearchEntry> entries;
            const auto& first = context.metadata().pages.first();
            SearchEntry entity;
            entity.kind = SearchEntry::Kind::Entity;
            entity.pageId = FAKE_MODE == 9 ? context.pageId(first.id) : QStringLiteral("elsewhere:page");
            entity.anchor = QStringLiteral("rule:42");
            entity.title = QStringLiteral("Dynamic entity");
            entries.append(entity);
            SearchEntry setting;
            setting.kind = SearchEntry::Kind::Setting;
            setting.pageId = context.pageId(first.id);
            setting.anchor = first.anchors.value(0);
            setting.title = QStringLiteral("Dynamic setting");
            entries.append(setting);
            m_provider = std::make_unique<FakeProvider>(entries);
            context.registerSearchProvider(m_provider.get());
        }
        if (FAKE_MODE == 6) {
            context.registerExtension(QStringLiteral("undeclared"), new FakeDomain(this), QUrl());
            context.registerExtension(extensions.first().id, new FakeDomain(this), QUrl());
        }
    }

private:
    std::unique_ptr<FakeProvider> m_provider;
};

#include "fakemodule.moc"
