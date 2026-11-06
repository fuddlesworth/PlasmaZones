// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// phosphor-facet-metadata, run as the build runs it.

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

// PHOSPHOR_FACET_METADATA_TOOL, the tool's path, comes from tests/CMakeLists.txt.

class TestFacetMetadataTool : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        QVERIFY(m_dir.emplace().isValid());
    }

    void refusesInvalidMetadataUnlessAllowed()
    {
        const QString source = write(QStringLiteral("bad.json"), R"({ "Id": "m", "Pages": [ { "Id": "p" } ] })");
        const QString out = m_dir->filePath(QStringLiteral("out.json"));
        QString errors;
        QCOMPARE(run({QStringLiteral("localize"), source, out}, &errors), 1);
        QVERIFY2(errors.contains(QStringLiteral("\"Name\" is missing")), qPrintable(errors));
        QVERIFY(errors.contains(QStringLiteral("page p has no \"Name\"")));
        QVERIFY(!QFile::exists(out));
        QCOMPARE(run({QStringLiteral("--allow-invalid"), QStringLiteral("localize"), source, out}), 0);
        QVERIFY(QFile::exists(out));
    }

    void writesEscapedStringsForLupdate()
    {
        const QString source =
            write(QStringLiteral("m.json"),
                  "{ \"Id\": \"m\", \"Name\": \"Say \\\"hi\\\" \\\\ now\", \"Keywords\": [\"k\", \"k\"] }");
        const QString out = m_dir->filePath(QStringLiteral("strings.cpp"));
        QCOMPARE(run({QStringLiteral("strings"), source, out}), 0);
        const QString text = read(out);
        QVERIFY2(text.contains(QStringLiteral("QT_TRANSLATE_NOOP(\"FacetModule\", \"Say \\\"hi\\\" \\\\ now\")")),
                 qPrintable(text));
        // Each string once, however often it appears.
        QCOMPARE(text.count(QStringLiteral(R"("k")")), 1);
    }

    void localizesFromCatalogs()
    {
        const QString source =
            write(QStringLiteral("m.json"), R"({ "Id": "m", "Name": "Hello", "Keywords": ["one", "two"] })");
        const QString catalog = write(QStringLiteral("m_fr.ts"), R"(<?xml version="1.0" encoding="utf-8"?>
<TS version="2.1" language="fr"><context><name>FacetModule</name>
<message><source>Hello</source><translation>Bonjour</translation></message>
<message><source>two</source><translation>deux</translation></message>
</context></TS>)");
        const QString out = m_dir->filePath(QStringLiteral("out.json"));
        QCOMPARE(run({QStringLiteral("localize"), source, out, catalog}), 0);
        const QJsonObject json = QJsonDocument::fromJson(read(out).toUtf8()).object();
        QCOMPARE(json.value(QStringLiteral("Name[fr]")).toString(), QStringLiteral("Bonjour"));
        // An untranslated keyword keeps its source text.
        QCOMPARE(json.value(QStringLiteral("Keywords[fr]")).toVariant().toStringList(),
                 (QStringList{QStringLiteral("one"), QStringLiteral("deux")}));
    }

    void refusesACatalogWithoutALanguage()
    {
        const QString source = write(QStringLiteral("m.json"), R"({ "Id": "m", "Name": "Hello" })");
        const QString catalog = write(QStringLiteral("x.ts"), R"(<TS version="2.1"></TS>)");
        QString errors;
        QCOMPARE(run({QStringLiteral("localize"), source, m_dir->filePath(QStringLiteral("o.json")), catalog}, &errors),
                 1);
        QVERIFY2(errors.contains(QStringLiteral("no language attribute")), qPrintable(errors));
    }

    void writesTheAnchorList()
    {
        const QString source = write(QStringLiteral("m.json"), R"({ "Id": "m", "Name": "M",
            "Pages": [ { "Id": "p", "Name": "P", "Anchors": ["a", "b"] }, { "Id": "q", "Name": "Q" } ],
            "ExtensionPoints": [ { "Page": "p", "Id": "anchors", "Version": 3 } ] })");
        const QString out = m_dir->filePath(QStringLiteral("anchors.json"));
        QCOMPARE(run({QStringLiteral("anchors"), source, out}), 0);
        const QJsonObject json = QJsonDocument::fromJson(read(out).toUtf8()).object();
        QCOMPARE(json.value(QStringLiteral("Module")).toString(), QStringLiteral("m"));
        const QJsonObject p = json.value(QStringLiteral("Pages")).toObject().value(QStringLiteral("p")).toObject();
        QCOMPARE(p.value(QStringLiteral("Version")).toInt(), 3);
        QCOMPARE(p.value(QStringLiteral("Anchors")).toVariant().toStringList(),
                 (QStringList{QStringLiteral("a"), QStringLiteral("b")}));
        const QJsonObject q = json.value(QStringLiteral("Pages")).toObject().value(QStringLiteral("q")).toObject();
        QCOMPARE(q.value(QStringLiteral("Version")).toInt(), 1);
    }

private:
    QString write(const QString& name, const char* text)
    {
        const QString path = m_dir->filePath(name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(text);
        }
        return path;
    }

    static QString read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    }

    static int run(const QStringList& arguments, QString* errors = nullptr)
    {
        QProcess process;
        process.start(QStringLiteral(PHOSPHOR_FACET_METADATA_TOOL), arguments);
        if (!process.waitForFinished(10000)) {
            return -1;
        }
        if (errors) {
            *errors = QString::fromUtf8(process.readAllStandardError());
        }
        return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
    }

    std::optional<QTemporaryDir> m_dir;
};

QTEST_GUILESS_MAIN(TestFacetMetadataTool)
#include "test_facet_metadata_tool.moc"
