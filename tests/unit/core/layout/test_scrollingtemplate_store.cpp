// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_scrollingtemplate_store.cpp
 *
 * ScrollingTemplate JSON round-trip / normalization and the
 * ScrollingTemplateStore's persistence contract: user-dir CRUD, the
 * system-dir shadowing precedence (a user file sharing a bundled template's
 * id overrides it; deleting the user file resurfaces the bundled original),
 * and the delete refusal for pure system templates.
 */

#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>

using PhosphorZones::ScrollingTemplate;
using PhosphorZones::ScrollingTemplateColumn;
using PhosphorZones::ScrollingTemplateStore;

class TestScrollingTemplateStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Redirect every QStandardPaths location into the test sandbox so
        // the store's user-directory writes never touch a real profile.
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        // Fresh user template directory per test: test mode pins the
        // writable GenericDataLocation, so wiping the subdirectory is
        // enough for isolation.
        QDir(userTemplateDir()).removeRecursively();
    }

    void jsonRoundTripIsExact()
    {
        ScrollingTemplate templ = makeTemplate();
        const ScrollingTemplate restored = ScrollingTemplate::fromJson(templ.toJson());
        QCOMPARE(restored, templ);
    }

    void normalizeClampsAndDrops()
    {
        ScrollingTemplate templ = makeTemplate();
        templ.columns = {
            {0.02, 0}, // sub-floor: dropped
            {1.7, 1}, // clamped to 1.0, display kept
            {0.4, 9}, // bad display: reset to 0
        };
        templ.presetColumnWidths = {0.9, 0.5, 0.501, 0.01, 2.0};
        templ.defaultColumnWidthKind = 42;
        templ.defaultColumnDisplay = -1;
        templ.defaultColumnWidthPresetIndex = 99;
        QVERIFY(templ.normalize());

        QCOMPARE(templ.columns.size(), 2);
        QCOMPARE(templ.columns.at(0).width, 1.0);
        QCOMPARE(templ.columns.at(0).display, 1);
        QCOMPARE(templ.columns.at(1).width, 0.4);
        QCOMPARE(templ.columns.at(1).display, 0);
        // Sorted ascending, 0.501 deduped into 0.5, 0.01 dropped, 2.0
        // clamped to 1.0.
        QCOMPARE(templ.presetColumnWidths, (QList<qreal>{0.5, 0.9, 1.0}));
        QCOMPARE(templ.defaultColumnWidthKind, 3);
        QCOMPARE(templ.defaultColumnDisplay, 0);
        QCOMPARE(templ.defaultColumnWidthPresetIndex, templ.presetColumnWidths.size() - 1);
    }

    void malformedJsonYieldsInvalid()
    {
        QVERIFY(!ScrollingTemplate::fromJson(QJsonObject()).isValid());
        QJsonObject noName;
        noName.insert(QLatin1String("id"), QUuid::createUuid().toString());
        QVERIFY(!ScrollingTemplate::fromJson(noName).isValid());
    }

    void storeCrudRoundTrip()
    {
        ScrollingTemplateStore store;
        store.loadTemplates();
        QSignalSpy changedSpy(&store, &ScrollingTemplateStore::templatesChanged);

        ScrollingTemplate templ = makeTemplate();
        templ.id = QUuid(); // store assigns
        const QUuid id = store.saveTemplate(templ);
        QVERIFY(!id.isNull());
        QCOMPARE(changedSpy.count(), 1);
        QVERIFY(store.contains(id));
        QCOMPARE(store.templateById(id).name, templ.name);
        QVERIFY(!store.templateById(id).isSystem);
        QVERIFY(QFile::exists(userTemplateDir() + QLatin1Char('/') + id.toString(QUuid::WithoutBraces)
                              + QLatin1String(".json")));

        const QUuid copyId = store.duplicateTemplate(id);
        QVERIFY(!copyId.isNull());
        QVERIFY(copyId != id);
        QCOMPARE(store.templateById(copyId).name, templ.name + QStringLiteral(" (Copy)"));

        QVERIFY(store.removeTemplate(copyId));
        QVERIFY(!store.contains(copyId));
        // Unknown id refuses.
        QVERIFY(!store.removeTemplate(QUuid::createUuid()));
    }

    void invalidTemplateRefusedBySave()
    {
        ScrollingTemplateStore store;
        ScrollingTemplate nameless = makeTemplate();
        nameless.name.clear();
        QCOMPARE(store.saveTemplate(nameless), QUuid());
        QCOMPARE(store.count(), 0);
    }

    void systemTemplateShadowingAndResurface()
    {
        // Simulate a bundled template via XDG_DATA_DIRS pointing at a temp
        // "system" tree. Test mode keeps the WRITABLE location sandboxed
        // while locateAll still consults XDG_DATA_DIRS.
        QTemporaryDir systemRoot;
        QVERIFY(systemRoot.isValid());
        const QString systemTemplateDir = systemRoot.path() + QLatin1String("/plasmazones/scrolling-templates");
        QVERIFY(QDir().mkpath(systemTemplateDir));

        ScrollingTemplate bundled = makeTemplate();
        QFile systemFile(systemTemplateDir + QLatin1String("/bundled.json"));
        QVERIFY(systemFile.open(QIODevice::WriteOnly));
        systemFile.write(QJsonDocument(bundled.toJson()).toJson());
        systemFile.close();

        const QByteArray oldDataDirs = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", systemRoot.path().toUtf8());
        const auto restoreEnv = qScopeGuard([&]() {
            qputenv("XDG_DATA_DIRS", oldDataDirs);
        });

        ScrollingTemplateStore store;
        store.loadTemplates();
        QVERIFY(store.contains(bundled.id));
        QVERIFY(store.templateById(bundled.id).isSystem);

        // A pure system template refuses deletion.
        QVERIFY(!store.removeTemplate(bundled.id));

        // Editing writes a USER copy that shadows the bundled file.
        ScrollingTemplate edited = store.templateById(bundled.id);
        edited.name = QStringLiteral("Edited name");
        QCOMPARE(store.saveTemplate(edited), bundled.id);
        QCOMPARE(store.templateById(bundled.id).name, QStringLiteral("Edited name"));
        QVERIFY(!store.templateById(bundled.id).isSystem);

        // Deleting the user copy resurfaces the bundled original.
        QVERIFY(store.removeTemplate(bundled.id));
        QVERIFY(store.contains(bundled.id));
        QCOMPARE(store.templateById(bundled.id).name, bundled.name);
        QVERIFY(store.templateById(bundled.id).isSystem);
    }

private:
    static QString userTemplateDir()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QLatin1String("/plasmazones/scrolling-templates");
    }

    static ScrollingTemplate makeTemplate()
    {
        ScrollingTemplate templ;
        templ.id = QUuid::createUuid();
        templ.name = QStringLiteral("Test template");
        templ.description = QStringLiteral("A template for the tests.");
        templ.columns = {{0.6, 0}, {0.4, 1}};
        templ.defaultColumnWidthKind = 3;
        templ.defaultColumnWidthValue = 0.5;
        templ.defaultColumnWidthPresetIndex = 1;
        templ.defaultColumnDisplay = 0;
        templ.presetColumnWidths = {0.4, 0.5, 0.6};
        templ.presetWindowHeights = {0.5};
        return templ;
    }
};

QTEST_GUILESS_MAIN(TestScrollingTemplateStore)
#include "test_scrollingtemplate_store.moc"
