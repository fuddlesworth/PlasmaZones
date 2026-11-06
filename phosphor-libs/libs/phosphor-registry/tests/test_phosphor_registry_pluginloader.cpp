// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "test_plugin_helpers.h"

#include <PhosphorRegistry/IBarWidgetFactory.h>
#include <PhosphorRegistry/PluginLoader.h>
#include <PhosphorRegistry/Registry.h>

#include <QFile>
#include <QPointer>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorRegistry;
using namespace PhosphorRegistryTestHelpers;

namespace {

class BuiltinFactory : public IBarWidgetFactory
{
public:
    explicit BuiltinFactory(QString id)
        : m_id(std::move(id))
    {
    }
    [[nodiscard]] QString id() const override
    {
        return m_id;
    }
    [[nodiscard]] QString displayName() const override
    {
        return m_id;
    }
    [[nodiscard]] QQuickItem* createWidget(QQmlEngine*, QObject* parent) override
    {
        return new QQuickItem(qobject_cast<QQuickItem*>(parent));
    }

private:
    QString m_id;
};

} // namespace

class TestPluginLoader : public QObject
{
    Q_OBJECT

private:
    static std::unique_ptr<PluginLoader> makeLoader(Registry<IBarWidgetFactory>* registry, const QString& directory)
    {
        auto loader =
            std::make_unique<PluginLoader>(registry, QList<PluginSearchPath>{{directory, PluginOrigin::Development}});
        return loader;
    }

private Q_SLOTS:
    void barWidgetSeamNamesItsDirectoryIidAndVersion()
    {
        const PluginSeam seam = PluginLoader::barWidgetSeam();
        QCOMPARE(seam.name, QStringLiteral("bar-widget"));
        QCOMPARE(seam.iid, QStringLiteral("org.phosphor_works.Lambent.BarWidget/1"));
        QCOMPARE(seam.apiVersion, 1);
    }

    void registersTheFactoryOfEveryPluginItFinds()
    {
        QTemporaryDir root;
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin")).isEmpty());
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin-secondary")).isEmpty());
        Registry<IBarWidgetFactory> registry;
        auto loader = makeLoader(&registry, root.path());
        QSignalSpy loaded(loader.get(), &PluginLoader::pluginLoaded);
        QSignalSpy completed(loader.get(), &PluginLoader::rescanCompleted);

        loader->scanAndLoad();

        QCOMPARE(loaded.count(), 2);
        QCOMPARE(completed.count(), 1);
        QCOMPARE(loader->loadedPluginIds(),
                 (QStringList{QStringLiteral("fake-plugin"), QStringLiteral("fake-plugin-secondary")}));
        QVERIFY(registry.factory(QStringLiteral("fake-plugin")));
        QVERIFY(registry.factory(QStringLiteral("fake-plugin-secondary")));
    }

    void aPluginFactoryBuildsWidgets()
    {
        QTemporaryDir root;
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin")).isEmpty());
        Registry<IBarWidgetFactory> registry;
        auto loader = makeLoader(&registry, root.path());
        loader->scanAndLoad();

        QQmlEngine engine;
        QQuickItem parent;
        QQuickItem* const widget = registry.factory(QStringLiteral("fake-plugin"))->createWidget(&engine, &parent);
        QVERIFY(widget);
        QCOMPARE(widget->parentItem(), &parent);
    }

    void refusesAFactoryWhoseIdDiffersFromTheMetadata()
    {
        QTemporaryDir root;
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin-idmismatch")).isEmpty());
        Registry<IBarWidgetFactory> registry;
        auto loader = makeLoader(&registry, root.path());
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            loader->scanAndLoad();
        }

        QVERIFY(loader->loadedPluginIds().isEmpty());
        QVERIFY(registry.isEmpty());
        QCOMPARE(countContaining(warnings, QStringLiteral("fake-other")), 1);
    }

    void refusesAPluginThatReturnsNoFactory()
    {
        QTemporaryDir root;
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin-nullfactory")).isEmpty());
        Registry<IBarWidgetFactory> registry;
        auto loader = makeLoader(&registry, root.path());
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            loader->scanAndLoad();
        }

        QVERIFY(registry.isEmpty());
        QCOMPARE(countContaining(warnings, QStringLiteral("returned no factory")), 1);
    }

    void aBuiltinWithTheSameIdKeepsItsSlot()
    {
        QTemporaryDir root;
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin")).isEmpty());
        Registry<IBarWidgetFactory> registry;
        auto builtin = std::make_shared<BuiltinFactory>(QStringLiteral("fake-plugin"));
        QVERIFY(registry.registerFactory(builtin));
        auto loader = makeLoader(&registry, root.path());
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            loader->scanAndLoad();
        }

        QCOMPARE(registry.factory(QStringLiteral("fake-plugin")), builtin);
        QVERIFY(loader->loadedPluginIds().isEmpty());
    }

    void aRemovedPluginIsUnregisteredAndItsWidgetsSurvive()
    {
        QTemporaryDir root;
        const QString path = stagePlugin(root.path(), QStringLiteral("fake-plugin"));
        Registry<IBarWidgetFactory> registry;
        auto loader = makeLoader(&registry, root.path());
        loader->scanAndLoad();

        QQmlEngine engine;
        auto parent = std::make_unique<QQuickItem>();
        QPointer<QQuickItem> widget =
            registry.factory(QStringLiteral("fake-plugin"))->createWidget(&engine, parent.get());
        QVERIFY(widget);

        QSignalSpy unloaded(loader.get(), &PluginLoader::pluginUnloaded);
        QVERIFY(QFile::remove(path));
        loader->rescanNow();

        QCOMPARE(unloaded.count(), 1);
        QVERIFY(!registry.factory(QStringLiteral("fake-plugin")));
        QVERIFY(loader->loadedPluginIds().isEmpty());
        // The library stays mapped, so the widget is still a valid object and
        // its parent can delete it.
        QVERIFY(widget);
        QCOMPARE(widget->parentItem(), parent.get());
        parent.reset();
        QVERIFY(!widget);
    }

    void aPluginAddedLaterIsLoadedOnRescan()
    {
        QTemporaryDir root;
        Registry<IBarWidgetFactory> registry;
        auto loader = makeLoader(&registry, root.path());
        loader->scanAndLoad();
        QVERIFY(registry.isEmpty());

        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin")).isEmpty());
        QSignalSpy loaded(loader.get(), &PluginLoader::pluginLoaded);
        loader->rescanNow();
        QCOMPARE(loaded.count(), 1);
        QVERIFY(registry.factory(QStringLiteral("fake-plugin")));
    }

    void aRescanFromAnUnloadSlotIsSafe()
    {
        QTemporaryDir root;
        const QString first = stagePlugin(root.path(), QStringLiteral("fake-plugin"));
        const QString second = stagePlugin(root.path(), QStringLiteral("fake-plugin-secondary"));
        Registry<IBarWidgetFactory> registry;
        auto loader = makeLoader(&registry, root.path());
        loader->scanAndLoad();
        QCOMPARE(registry.size(), 2);

        int reentries = 0;
        connect(loader.get(), &PluginLoader::pluginUnloaded, this, [&loader, &reentries] {
            ++reentries;
            loader->rescanNow();
        });
        QVERIFY(QFile::remove(first));
        QVERIFY(QFile::remove(second));
        loader->rescanNow();

        QCOMPARE(reentries, 2);
        QVERIFY(registry.isEmpty());
    }

    void destroyingTheLoaderUnregistersItsFactoriesOnly()
    {
        QTemporaryDir root;
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin")).isEmpty());
        Registry<IBarWidgetFactory> registry;
        auto builtin = std::make_shared<BuiltinFactory>(QStringLiteral("clock"));
        QVERIFY(registry.registerFactory(builtin));
        auto loader = makeLoader(&registry, root.path());
        loader->scanAndLoad();
        QCOMPARE(registry.size(), 2);

        loader.reset();
        QCOMPARE(registry.size(), 1);
        QCOMPARE(registry.factory(QStringLiteral("clock")), builtin);
    }
};

QTEST_MAIN(TestPluginLoader)
#include "test_phosphor_registry_pluginloader.moc"
