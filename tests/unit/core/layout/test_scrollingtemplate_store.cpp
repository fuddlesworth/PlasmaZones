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
 *
 * Also the author-time schema contract. data/schemas/scrolling-template.schema.json
 * is not compiled at runtime, so nothing but this file keeps it honest against
 * the C++ that produces and normalizes the documents it describes.
 */

#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <PhosphorFsLoader/SchemaValidator.h>
#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>

namespace {
Q_LOGGING_CATEGORY(testSchema, "plasmazones.test.scrollingtemplate.schema")

QString schemaPath()
{
    return QStringLiteral(P_SOURCE_DIR "/data/schemas/scrolling-template.schema.json");
}
} // namespace

using PhosphorZones::ScrollingTemplate;
using PhosphorZones::ScrollingTemplateColumn;
using PhosphorZones::ScrollingTemplateStore;

class TestScrollingTemplateStore : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Redirect every QStandardPaths location into Qt's OWN test sandbox
        // ($HOME/.qttest/...), so the store's user-directory writes never
        // touch a real profile. This is Qt's sandbox, not the ctest harness's
        // XDG one: it moves the writable locations only, and it does so
        // regardless of how XDG_* is set in the environment.
        QStandardPaths::setTestModeEnabled(true);
    }

    void init()
    {
        // Fresh user template directory per test: test mode pins the
        // writable GenericDataLocation under $HOME/.qttest, so wiping the
        // subdirectory is enough for isolation.
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
        // Unsorted, a near-duplicate inside the dedupe epsilon, one below the
        // floor and one above the ceiling — the height list obeys the same
        // normalization as the width list.
        templ.presetWindowHeights = {0.8, 0.2, 0.805, 0.02, 1.4};
        templ.defaultColumnWidthKind = 42;
        templ.defaultColumnDisplay = -1;
        templ.defaultColumnWidthPresetIndex = 99;
        templ.defaultColumnWidthValue = -3.0;
        QVERIFY(templ.normalize());

        QCOMPARE(templ.columns.size(), 2);
        QCOMPARE(templ.columns.at(0).width, 1.0);
        QCOMPARE(templ.columns.at(0).display, 1);
        QCOMPARE(templ.columns.at(1).width, 0.4);
        QCOMPARE(templ.columns.at(1).display, 0);
        // Sorted ascending, 0.501 deduped into 0.5, 0.01 dropped, 2.0
        // clamped to 1.0.
        QCOMPARE(templ.presetColumnWidths, (QList<qreal>{0.5, 0.9, 1.0}));
        // Same treatment on the height list: 0.02 dropped, 1.4 clamped to
        // 1.0, 0.805 deduped into 0.8, the rest sorted ascending.
        QCOMPARE(templ.presetWindowHeights, (QList<qreal>{0.2, 0.8, 1.0}));
        QCOMPARE(templ.defaultColumnWidthKind, 3);
        QCOMPARE(templ.defaultColumnDisplay, 0);
        QCOMPARE(templ.defaultColumnWidthPresetIndex, templ.presetColumnWidths.size() - 1);
        // A negative default width is not a value any kind can mean, so it is
        // repaired to 0.5 rather than clamped. The Proportion-only bound that
        // follows the repair does not apply here, because the kind landed on
        // Preset (3) with a non-empty preset list.
        QCOMPARE(templ.defaultColumnWidthValue, 0.5);
    }

    void normalizeDemotesPresetKindWithoutVocabulary()
    {
        // The bare-defaults shape: kind Preset with no preset list. The trio
        // the daemon pushes must stay self-consistent, so normalize() demotes
        // to Proportion, zeroes the index, and the Proportion clamp then
        // bounds the value.
        ScrollingTemplate templ = makeTemplate();
        templ.presetColumnWidths.clear();
        templ.defaultColumnWidthKind = 3;
        templ.defaultColumnWidthPresetIndex = 1;
        templ.defaultColumnWidthValue = 5.0; // out of range for Proportion
        QVERIFY(templ.normalize());

        QCOMPARE(templ.defaultColumnWidthKind, 0);
        QCOMPARE(templ.defaultColumnWidthPresetIndex, 0);
        QCOMPARE(templ.defaultColumnWidthValue, 1.0);
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

        // Restore by UNSETTING when the variable was not set to begin with:
        // qputenv with the empty byte array leaves an empty XDG_DATA_DIRS
        // behind, which is a different state from absent (an absent one falls
        // back to the spec's /usr/local/share:/usr/share).
        const bool hadDataDirs = qEnvironmentVariableIsSet("XDG_DATA_DIRS");
        const QByteArray oldDataDirs = qgetenv("XDG_DATA_DIRS");
        qputenv("XDG_DATA_DIRS", systemRoot.path().toUtf8());
        const auto restoreEnv = qScopeGuard([&]() {
            if (hadDataDirs) {
                qputenv("XDG_DATA_DIRS", oldDataDirs);
            } else {
                qunsetenv("XDG_DATA_DIRS");
            }
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

    void schemaMirrorsTheNormalizationConstants()
    {
        // The schema restates two constants that live in C++
        // (PhosphorZones::MinTemplateFraction and MaxTemplateColumns in
        // ScrollingTemplate.h, the latter itself a mirror of the scroll
        // engine's kMaxTemplateEntries). Nothing links the two, so a
        // floor or cap that moves in the code leaves the author-time gate
        // quietly describing the old contract. This pins the restatement.
        QFile file(schemaPath());
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(schemaPath()));
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        QCOMPARE(parseError.error, QJsonParseError::NoError);
        QVERIFY(doc.isObject());
        const QJsonObject schema = doc.object();

        const QJsonObject fraction =
            schema.value(QLatin1String("definitions")).toObject().value(QLatin1String("fraction")).toObject();
        QCOMPARE(fraction.value(QLatin1String("minimum")).toDouble(), PhosphorZones::MinTemplateFraction);
        QCOMPARE(fraction.value(QLatin1String("maximum")).toDouble(), 1.0);

        const QJsonObject columns =
            schema.value(QLatin1String("properties")).toObject().value(QLatin1String("columns")).toObject();
        QCOMPARE(columns.value(QLatin1String("maxItems")).toInt(), PhosphorZones::MaxTemplateColumns);
    }

    void toJsonAndBundledTemplatesSatisfySchema()
    {
        // The schema is author-time only: no loader compiles it, so without
        // this leg the only thing checking it is the pre-commit python gate,
        // and nothing at all checks that what toJson emits would pass it.
        const auto validator = PhosphorFsLoader::SchemaValidator::fromResource(schemaPath(), testSchema());
        QVERIFY2(validator.isValid(), qPrintable(schemaPath()));

        const auto describe = [](const QList<PhosphorFsLoader::SchemaValidator::Error>& errors) {
            QStringList parts;
            for (const auto& error : errors) {
                parts.append((error.path.isEmpty() ? QStringLiteral("(root)") : error.path) + QLatin1String(": ")
                             + error.message);
            }
            return parts.join(QLatin1String("; "));
        };

        const auto serialized = makeTemplate().toJson();
        const auto errors = validator.validate(serialized);
        const QString serializedReport = errors.has_value() ? describe(errors.value()) : QStringLiteral("(no errors)");
        QVERIFY2(!errors.has_value(), qPrintable(serializedReport));

        const QDir bundledDir(QStringLiteral(P_SOURCE_DIR "/data/scrolling-templates"));
        const QStringList bundled = bundledDir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        // An empty directory would make every assertion below vacuous, and the
        // starter set is what the picker shows on a fresh profile.
        QVERIFY(!bundled.isEmpty());
        QStringList failures;
        for (const QString& name : bundled) {
            QFile file(bundledDir.filePath(name));
            if (!file.open(QIODevice::ReadOnly)) {
                failures.append(name + QLatin1String(": cannot open"));
                continue;
            }
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error != QJsonParseError::NoError) {
                failures.append(name + QLatin1String(": ") + parseError.errorString());
                continue;
            }
            const auto fileErrors = validator.validate(doc.object());
            if (fileErrors.has_value()) {
                failures.append(name + QLatin1String(": ") + describe(fileErrors.value()));
                continue;
            }
            // A file that satisfies the schema must also survive the parser it
            // is a contract for, or the schema is describing the wrong shape.
            if (!ScrollingTemplate::fromJson(doc.object()).isValid()) {
                failures.append(name + QLatin1String(": passes the schema but fromJson rejects it"));
            }
        }
        // Accumulated rather than asserted per file, so one bad starter does not
        // hide the state of the rest.
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join(QLatin1String("\n"))));
    }

    void schemaBoundsKindZeroDefaultWidth()
    {
        // The defaultColumnWidth if/then branch is the one part of the schema no
        // bundled starter and no toJson output exercises: they all serialize a
        // fraction that is in range anyway, so a broken conditional would still
        // let every document above pass. These arms drive it directly.
        const auto validator = PhosphorFsLoader::SchemaValidator::fromResource(schemaPath(), testSchema());
        QVERIFY2(validator.isValid(), qPrintable(schemaPath()));

        const auto documentWithWidth = [](int kind, double value) {
            QJsonObject width;
            width.insert(QLatin1String("kind"), kind);
            width.insert(QLatin1String("value"), value);
            QJsonObject doc;
            doc.insert(QLatin1String("id"), QUuid::createUuid().toString());
            doc.insert(QLatin1String("name"), QStringLiteral("Conditional arm"));
            doc.insert(QLatin1String("defaultColumnWidth"), width);
            return doc;
        };

        // Kind 0 reads value as a work-area fraction, so an in-range fraction
        // passes and one past the ceiling does not.
        QVERIFY(!validator.validate(documentWithWidth(0, 0.5)).has_value());
        QVERIFY(validator.validate(documentWithWidth(0, 2.0)).has_value());
        // The same 2.0 under kind 1 is a pixel count, which the conditional
        // leaves unbounded above. Without this arm a plain unconditional maximum
        // on value would satisfy the two arms above.
        QVERIFY(!validator.validate(documentWithWidth(1, 2.0)).has_value());
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
