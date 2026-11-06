// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "test_plugin_helpers.h"

#include <PhosphorRegistry/PluginCatalog.h>
#include <PhosphorRegistry/PluginLoader.h>

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorRegistry;
using namespace PhosphorRegistryTestHelpers;

class TestPluginCatalog : public QObject
{
    Q_OBJECT

private:
    // A catalog over the bar-widget seam with one development search path,
    // and live reload off so every scan is an explicit call.
    static std::unique_ptr<PluginCatalog> makeCatalog(const QList<PluginSearchPath>& paths)
    {
        auto catalog = std::make_unique<PluginCatalog>(PluginLoader::barWidgetSeam());
        catalog->setSearchPaths(paths);
        catalog->setLiveReload(false);
        return catalog;
    }

    static QString reasonFor(const PluginCatalog& catalog, const QString& filePath)
    {
        const QList<RefusedPlugin> refused = catalog.refused();
        for (const RefusedPlugin& entry : refused) {
            if (entry.filePath == filePath) {
                return entry.reason;
            }
        }
        return QString();
    }

private Q_SLOTS:
    void offersAPluginFromItsMetadataWithoutLoadingIt()
    {
        QTemporaryDir root;
        const QString path = stagePlugin(root.filePath(QStringLiteral("dev")), QStringLiteral("fake-plugin"));
        QVERIFY(!path.isEmpty());
        auto catalog = makeCatalog({{root.filePath(QStringLiteral("dev")), PluginOrigin::Development}});

        QSignalSpy added(catalog.get(), &PluginCatalog::pluginAdded);
        catalog->scan();

        QCOMPARE(added.count(), 1);
        QCOMPARE(added.first().first().toString(), QStringLiteral("fake-plugin"));
        const auto metadata = catalog->plugin(QStringLiteral("fake-plugin"));
        QVERIFY(metadata.has_value());
        QCOMPARE(metadata->filePath, path);
        QCOMPARE(metadata->apiVersion, 1);
        QCOMPARE(metadata->origin, PluginOrigin::Development);
        QCOMPARE(metadata->metaData.value(QLatin1String("Id")).toString(), QStringLiteral("fake-plugin"));
        QVERIFY(!catalog->isLoaded(QStringLiteral("fake-plugin")));
        QVERIFY(catalog->refused().isEmpty());
    }

    void loadReturnsTheRootObjectOnceAndCachesIt()
    {
        QTemporaryDir root;
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin")).isEmpty());
        auto catalog = makeCatalog({{root.path(), PluginOrigin::User}});
        catalog->scan();

        QString error;
        QObject* const first = catalog->load(QStringLiteral("fake-plugin"), &error);
        QVERIFY2(first, qPrintable(error));
        QVERIFY(catalog->isLoaded(QStringLiteral("fake-plugin")));
        QCOMPARE(catalog->load(QStringLiteral("fake-plugin")), first);
    }

    void loadOfAnUnknownIdFails()
    {
        QTemporaryDir root;
        auto catalog = makeCatalog({{root.path(), PluginOrigin::User}});
        catalog->scan();

        QString error;
        QCOMPARE(catalog->load(QStringLiteral("nothing-here"), &error), nullptr);
        QVERIFY(error.contains(QStringLiteral("nothing-here")));
    }

    void anEarlierSearchPathOverridesALaterOneAndSaysSo()
    {
        QTemporaryDir root;
        const QString dev = root.filePath(QStringLiteral("dev"));
        const QString user = root.filePath(QStringLiteral("user"));
        const QString devCopy = stagePlugin(dev, QStringLiteral("fake-plugin"));
        const QString userCopy = stagePlugin(user, QStringLiteral("fake-plugin"));
        QVERIFY(!devCopy.isEmpty() && !userCopy.isEmpty());
        auto catalog = makeCatalog({{dev, PluginOrigin::Development}, {user, PluginOrigin::User}});

        QStringList warnings;
        QStringList infos;
        {
            MessageCapture capture(warnings, &infos);
            catalog->scan();
        }

        QCOMPARE(catalog->plugins().size(), 1);
        QCOMPARE(catalog->plugin(QStringLiteral("fake-plugin"))->filePath, devCopy);
        QCOMPARE(catalog->plugin(QStringLiteral("fake-plugin"))->origin, PluginOrigin::Development);
        QCOMPARE(countContaining(infos, QStringLiteral("overrides")), 1);
        QCOMPARE(countContaining(infos, userCopy), 1);
        QVERIFY(catalog->refused().isEmpty());
    }

    void refusesAPluginBuiltForAnotherApiVersion()
    {
        QTemporaryDir root;
        const QString path = stagePlugin(root.path(), QStringLiteral("fake-plugin-wrongapi"));
        auto catalog = makeCatalog({{root.path(), PluginOrigin::User}});
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            catalog->scan();
        }

        QVERIFY(catalog->plugins().isEmpty());
        const QString reason = reasonFor(*catalog, path);
        QVERIFY2(reason.contains(QStringLiteral("ApiVersion 2")) && reason.contains(QStringLiteral("loads 1")),
                 qPrintable(reason));
        QCOMPARE(catalog->refused().first().id, QStringLiteral("fake-plugin-wrongapi"));
        QCOMPARE(countContaining(warnings, path), 1);
        // Refused from its metadata alone: it was never loaded.
        QVERIFY(!catalog->isLoaded(QStringLiteral("fake-plugin-wrongapi")));
    }

    void refusesAPluginOfAnotherSeam()
    {
        QTemporaryDir root;
        const QString path = stagePlugin(root.path(), QStringLiteral("fake-plugin-wrongiid"));
        auto catalog = makeCatalog({{root.path(), PluginOrigin::User}});
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            catalog->scan();
        }

        QVERIFY(catalog->plugins().isEmpty());
        QVERIFY(reasonFor(*catalog, path).contains(QStringLiteral("org.phosphor_works.Test.NotABarWidget/1")));
    }

    void refusesMetadataWithoutAValidId_data()
    {
        QTest::addColumn<QString>("variant");
        QTest::newRow("missing") << QStringLiteral("fake-plugin-noid");
        QTest::newRow("path characters") << QStringLiteral("fake-plugin-badid");
    }

    void refusesMetadataWithoutAValidId()
    {
        QFETCH(QString, variant);
        QTemporaryDir root;
        const QString path = stagePlugin(root.path(), variant);
        auto catalog = makeCatalog({{root.path(), PluginOrigin::User}});
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            catalog->scan();
        }

        QVERIFY(catalog->plugins().isEmpty());
        QVERIFY(reasonFor(*catalog, path).contains(QStringLiteral("no valid \"Id\"")));
    }

    void refusesALibraryThatIsNotAQtPlugin_data()
    {
        QTest::addColumn<bool>("garbage");
        QTest::newRow("plain shared library") << false;
        QTest::newRow("not a library at all") << true;
    }

    void refusesALibraryThatIsNotAQtPlugin()
    {
        QFETCH(bool, garbage);
        QTemporaryDir root;
        QString path;
        if (garbage) {
            path = root.filePath(QStringLiteral("garbage.so"));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArrayLiteral("this is not an ELF file"));
            file.close();
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        } else {
            path = stagePlugin(root.path(), QStringLiteral("plain"));
        }
        auto catalog = makeCatalog({{root.path(), PluginOrigin::User}});
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            catalog->scan();
        }

        QVERIFY(catalog->plugins().isEmpty());
        QCOMPARE(reasonFor(*catalog, path), QStringLiteral("not a Qt plugin"));
    }

    void refusesAGroupOrWorldWritablePlugin_data()
    {
        QTest::addColumn<bool>("loosenDirectory");
        QTest::addColumn<int>("bit");
        QTest::newRow("group-writable file") << false << int(QFileDevice::WriteGroup);
        QTest::newRow("world-writable file") << false << int(QFileDevice::WriteOther);
        QTest::newRow("group-writable directory") << true << int(QFileDevice::WriteGroup);
        QTest::newRow("world-writable directory") << true << int(QFileDevice::WriteOther);
    }

    void refusesAGroupOrWorldWritablePlugin()
    {
        QFETCH(bool, loosenDirectory);
        QFETCH(int, bit);
        QTemporaryDir root;
        const QString dir = root.filePath(QStringLiteral("plugins"));
        const QString path = stagePlugin(dir, QStringLiteral("fake-plugin"));
        QVERIFY(!path.isEmpty());
        const QString target = loosenDirectory ? dir : path;
        QVERIFY(QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::Permission(bit)));
        auto catalog = makeCatalog({{dir, PluginOrigin::User}});
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            catalog->scan();
        }

        QVERIFY(catalog->plugins().isEmpty());
        QVERIFY(reasonFor(*catalog, path).contains(QStringLiteral("writable")));
    }

    void ignoresASymlinkedPlugin()
    {
        QTemporaryDir root;
        const QString outside = stagePlugin(root.filePath(QStringLiteral("outside")), QStringLiteral("fake-plugin"));
        QVERIFY(!outside.isEmpty());
        const QString dir = root.filePath(QStringLiteral("plugins"));
        QVERIFY(QDir().mkpath(dir));
        QVERIFY(QFile::link(outside, dir + QStringLiteral("/linked.so")));
        auto catalog = makeCatalog({{dir, PluginOrigin::User}});
        catalog->scan();

        QVERIFY(catalog->plugins().isEmpty());
        QVERIFY(catalog->refused().isEmpty());
    }

    void refusesASecondPluginWithTheSameIdInOneDirectory()
    {
        QTemporaryDir root;
        const QString first = stagePlugin(root.path(), QStringLiteral("fake-plugin"), QStringLiteral("a.so"));
        const QString second = stagePlugin(root.path(), QStringLiteral("fake-plugin"), QStringLiteral("b.so"));
        auto catalog = makeCatalog({{root.path(), PluginOrigin::User}});
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            catalog->scan();
        }

        QCOMPARE(catalog->plugin(QStringLiteral("fake-plugin"))->filePath, first);
        QVERIFY(reasonFor(*catalog, second).contains(QStringLiteral("a.so")));
    }

    void aRescanOffersNewPluginsAndWithdrawsRemovedOnes()
    {
        QTemporaryDir root;
        auto catalog = makeCatalog({{root.path(), PluginOrigin::User}});
        QSignalSpy added(catalog.get(), &PluginCatalog::pluginAdded);
        QSignalSpy removed(catalog.get(), &PluginCatalog::pluginRemoved);
        QSignalSpy completed(catalog.get(), &PluginCatalog::scanCompleted);
        catalog->scan();
        QCOMPARE(completed.count(), 1);
        QVERIFY(catalog->plugins().isEmpty());

        const QString path = stagePlugin(root.path(), QStringLiteral("fake-plugin"));
        catalog->scan();
        QCOMPARE(added.count(), 1);
        QCOMPARE(completed.count(), 2);

        QVERIFY(QFile::remove(path));
        catalog->scan();
        QCOMPARE(removed.count(), 1);
        QCOMPARE(removed.first().first().toString(), QStringLiteral("fake-plugin"));
        QVERIFY(catalog->plugins().isEmpty());
        QCOMPARE(completed.count(), 3);
    }

    void aLoadedPluginKeepsOfferingTheCodeThatRuns()
    {
        QTemporaryDir root;
        const QString user = root.filePath(QStringLiteral("user"));
        const QString dev = root.filePath(QStringLiteral("dev"));
        const QString loadedFrom = stagePlugin(user, QStringLiteral("fake-plugin"));
        auto catalog = makeCatalog({{dev, PluginOrigin::Development}, {user, PluginOrigin::User}});
        catalog->scan();
        QVERIFY(catalog->load(QStringLiteral("fake-plugin")));

        // A higher-priority copy appears after the plugin was loaded. It
        // takes effect at the next start, so the offer still describes the
        // loaded file.
        QVERIFY(!stagePlugin(dev, QStringLiteral("fake-plugin")).isEmpty());
        QStringList warnings;
        QStringList infos;
        {
            MessageCapture capture(warnings, &infos);
            catalog->scan();
        }
        QCOMPARE(catalog->plugin(QStringLiteral("fake-plugin"))->filePath, loadedFrom);
        QCOMPARE(countContaining(infos, QStringLiteral("next start")), 1);
    }

    void aScanCutShortByTheCapKeepsPluginsItDidNotReach()
    {
        QTemporaryDir root;
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin")).isEmpty());
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin-secondary")).isEmpty());
        auto catalog = makeCatalog({{root.path(), PluginOrigin::User}});
        catalog->scan();
        QCOMPARE(catalog->plugins().size(), 2);

        catalog->setMaxFilesPerScanForTest(1);
        QSignalSpy removed(catalog.get(), &PluginCatalog::pluginRemoved);
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            catalog->scan();
        }
        QCOMPARE(removed.count(), 0);
        QCOMPARE(catalog->plugins().size(), 2);
        QCOMPARE(countContaining(warnings, QStringLiteral("stopped after")), 1);
    }

    void warnsOncePerProblemUntilItGoesAway()
    {
        QTemporaryDir root;
        const QString path = stagePlugin(root.path(), QStringLiteral("fake-plugin-wrongapi"));
        auto catalog = makeCatalog({{root.path(), PluginOrigin::User}});
        QStringList warnings;
        {
            MessageCapture capture(warnings);
            catalog->scan();
            catalog->scan();
            catalog->scan();
        }
        QCOMPARE(countContaining(warnings, path), 1);

        // Once the problem is gone, its return warns again.
        QVERIFY(QFile::remove(path));
        catalog->scan();
        QVERIFY(!stagePlugin(root.path(), QStringLiteral("fake-plugin-wrongapi")).isEmpty());
        warnings.clear();
        {
            MessageCapture capture(warnings);
            catalog->scan();
        }
        QCOMPARE(countContaining(warnings, path), 1);
    }

    void defaultSearchPathsPutDevelopmentFirstThenUserThenSystem()
    {
        const QByteArray saved = qgetenv(PluginCatalog::DevelopmentPathVariable);
        qputenv(PluginCatalog::DevelopmentPathVariable, QByteArrayLiteral("/build/one::/build/two"));
        const QList<PluginSearchPath> paths = PluginCatalog::defaultSearchPaths(QStringLiteral("facet"));
        if (saved.isNull()) {
            qunsetenv(PluginCatalog::DevelopmentPathVariable);
        } else {
            qputenv(PluginCatalog::DevelopmentPathVariable, saved);
        }

        QVERIFY(paths.size() >= 3);
        QCOMPARE(paths.at(0).directory, QStringLiteral("/build/one/facet"));
        QCOMPARE(paths.at(0).origin, PluginOrigin::Development);
        QCOMPARE(paths.at(1).directory, QStringLiteral("/build/two/facet"));
        QCOMPARE(paths.at(1).origin, PluginOrigin::Development);
        QCOMPARE(paths.at(2).origin, PluginOrigin::User);
        QVERIFY(paths.at(2).directory.endsWith(QStringLiteral("/phosphor-works/plugins/facet")));
        for (qsizetype i = 3; i < paths.size(); ++i) {
            QCOMPARE(paths.at(i).origin, PluginOrigin::System);
            QVERIFY(paths.at(i).directory.endsWith(QStringLiteral("/phosphor-works/facet")));
        }
    }
};

QTEST_MAIN(TestPluginCatalog)
#include "test_phosphor_registry_plugincatalog.moc"
