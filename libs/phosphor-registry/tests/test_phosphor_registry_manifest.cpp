// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Tests for Manifest::parseObject (in-memory parse) and
// Manifest::parse (file parse). Covers happy path + every rejection
// path so the loader's "refuse on error" contract is locked in.

#include <PhosphorRegistry/Manifest.h>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace PhosphorRegistry;

class TestManifest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void parseObject_acceptsValidManifest();
    void parseObject_rejectsMissingId();
    void parseObject_rejectsMissingDisplayName();
    void parseObject_rejectsMissingAbi();
    void parseObject_rejectsAbiMismatch();
    void parseObject_capabilitiesPopulated();
    void parseObject_capabilitiesEmptyArray();
    void parseObject_capabilitiesSkipsNonStringElements();
    void parseObject_capabilitiesMissingDefaultsEmpty();
    void parseObject_rejectsIdMismatchAgainstDir();
    void parseObject_rejectsNonIntAbi();
    void parseObject_rejectsLeadingDotId();
    void parseObject_rejectsForwardSlashId();
    void parseObject_rejectsBackslashId();
    void parseObject_rejectsTraversalSubstringId();
    void parseFile_acceptsValidFile();
    void parseFile_rejectsMissingFile();
    void parseFile_rejectsMalformedJson();
    void parseFile_rejectsNonObjectRoot();
    void parseFile_rejectsOversizedManifest();
    void parseFile_rejectsEmptyFile();
};

namespace {

QJsonObject makeValidManifestObject(const QString& id = QStringLiteral("clock"))
{
    QJsonObject obj;
    obj.insert(QLatin1String("id"), id);
    obj.insert(QLatin1String("displayName"), QStringLiteral("Test"));
    obj.insert(QLatin1String("abi"), PluginAbiVersion);
    return obj;
}

QString writeTempManifest(QTemporaryDir& dir, const QString& subdirName, const QString& contents)
{
    const QString sub = dir.filePath(subdirName);
    QDir().mkpath(sub);
    const QString path = sub + QStringLiteral("/manifest.json");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {};
    }
    f.write(contents.toUtf8());
    f.close();
    return path;
}

} // namespace

void TestManifest::parseObject_acceptsValidManifest()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pluginDir = dir.filePath(QStringLiteral("clock"));
    QDir().mkpath(pluginDir);

    const Manifest m = Manifest::parseObject(makeValidManifestObject(), pluginDir);
    QVERIFY(m.isValid);
    QCOMPARE(m.id, QStringLiteral("clock"));
    QCOMPARE(m.displayName, QStringLiteral("Test"));
    QCOMPARE(m.abi, PluginAbiVersion);
    QVERIFY(m.parseError.isEmpty());
}

void TestManifest::parseObject_rejectsMissingId()
{
    QJsonObject obj = makeValidManifestObject();
    obj.remove(QLatin1String("id"));
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("'id'")));
}

void TestManifest::parseObject_rejectsMissingDisplayName()
{
    QJsonObject obj = makeValidManifestObject();
    obj.remove(QLatin1String("displayName"));
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("'displayName'")));
}

void TestManifest::parseObject_rejectsMissingAbi()
{
    QJsonObject obj = makeValidManifestObject();
    obj.remove(QLatin1String("abi"));
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("'abi'")));
}

void TestManifest::parseObject_rejectsAbiMismatch()
{
    QJsonObject obj = makeValidManifestObject();
    obj.insert(QLatin1String("abi"), PluginAbiVersion + 99);
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("abi mismatch")));
}

void TestManifest::parseObject_capabilitiesPopulated()
{
    QJsonObject obj = makeValidManifestObject();
    obj.insert(QLatin1String("capabilities"), QJsonArray{QStringLiteral("bar.widget"), QStringLiteral("network.read")});
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(m.isValid);
    QCOMPARE(m.capabilities, (QStringList{QStringLiteral("bar.widget"), QStringLiteral("network.read")}));
}

void TestManifest::parseObject_capabilitiesEmptyArray()
{
    QJsonObject obj = makeValidManifestObject();
    obj.insert(QLatin1String("capabilities"), QJsonArray{});
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(m.isValid);
    QVERIFY(m.capabilities.isEmpty());
}

void TestManifest::parseObject_capabilitiesSkipsNonStringElements()
{
    // Non-string elements (numbers, bools, objects) are silently
    // dropped — the rule is "string-typed entries only" so a typo
    // in the manifest doesn't poison the capability list with
    // garbage values.
    QJsonObject obj = makeValidManifestObject();
    obj.insert(QLatin1String("capabilities"),
               QJsonArray{QStringLiteral("bar.widget"), 5, true, QJsonObject{}, QStringLiteral("network.read")});
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(m.isValid);
    QCOMPARE(m.capabilities, (QStringList{QStringLiteral("bar.widget"), QStringLiteral("network.read")}));
}

void TestManifest::parseObject_capabilitiesMissingDefaultsEmpty()
{
    QJsonObject obj = makeValidManifestObject();
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(m.isValid);
    QVERIFY(m.capabilities.isEmpty());
}

void TestManifest::parseObject_rejectsNonIntAbi()
{
    // Manifest::parseObject now splits three distinct diagnostics:
    // missing 'abi' field, 'abi' present but non-numeric type, and
    // numeric-but-wrong-version mismatch. This test exercises the
    // middle branch — a string-typed abi value must be rejected
    // with the type-specific message, NOT the missing-field one.
    QJsonObject obj = makeValidManifestObject();
    obj.insert(QLatin1String("abi"), QStringLiteral("one"));
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("must be an integer")));
}

void TestManifest::parseObject_rejectsLeadingDotId()
{
    QJsonObject obj = makeValidManifestObject(QStringLiteral(".hidden"));
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("unsafe characters")));
}

void TestManifest::parseObject_rejectsForwardSlashId()
{
    QJsonObject obj = makeValidManifestObject(QStringLiteral("a/b"));
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("unsafe characters")));
}

void TestManifest::parseObject_rejectsBackslashId()
{
    QJsonObject obj = makeValidManifestObject(QStringLiteral("a\\b"));
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("unsafe characters")));
}

void TestManifest::parseObject_rejectsTraversalSubstringId()
{
    QJsonObject obj = makeValidManifestObject(QStringLiteral("foo..bar"));
    const Manifest m = Manifest::parseObject(obj, QString());
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("unsafe characters")));
}

void TestManifest::parseObject_rejectsIdMismatchAgainstDir()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString pluginDir = dir.filePath(QStringLiteral("clock"));
    QDir().mkpath(pluginDir);

    QJsonObject obj = makeValidManifestObject(QStringLiteral("not-clock"));
    const Manifest m = Manifest::parseObject(obj, pluginDir);
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("does not match plugin directory basename")));
}

void TestManifest::parseFile_acceptsValidFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString contents =
        QStringLiteral("{\"id\":\"clock\",\"displayName\":\"Clock\",\"abi\":%1}").arg(PluginAbiVersion);
    const QString path = writeTempManifest(dir, QStringLiteral("clock"), contents);
    QVERIFY(!path.isEmpty());

    const Manifest m = Manifest::parse(path, dir.filePath(QStringLiteral("clock")));
    QVERIFY(m.isValid);
    QCOMPARE(m.id, QStringLiteral("clock"));
    QVERIFY(!m.manifestPath.isEmpty());
}

void TestManifest::parseFile_rejectsMissingFile()
{
    const Manifest m = Manifest::parse(QStringLiteral("/nonexistent/path/manifest.json"), QString());
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("cannot open")));
}

void TestManifest::parseFile_rejectsMalformedJson()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTempManifest(dir, QStringLiteral("clock"), QStringLiteral("{ broken"));
    QVERIFY(!path.isEmpty());

    const Manifest m = Manifest::parse(path, dir.filePath(QStringLiteral("clock")));
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("malformed JSON")));
}

void TestManifest::parseFile_rejectsNonObjectRoot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTempManifest(dir, QStringLiteral("clock"), QStringLiteral("[1,2,3]"));
    QVERIFY(!path.isEmpty());

    const Manifest m = Manifest::parse(path, dir.filePath(QStringLiteral("clock")));
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("not a JSON object")));
}

void TestManifest::parseFile_rejectsOversizedManifest()
{
    // The cap is a security gate — a hostile or corrupt manifest
    // mustn't be able to blow up process memory. Pad past the
    // public ManifestMaxBytes constant so the test exercises the
    // exact boundary even if the cap is bumped later.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString padding;
    padding.fill(QLatin1Char('x'), static_cast<int>(ManifestMaxBytes) + 1024);
    const QString contents =
        QStringLiteral("{\"id\":\"clock\",\"displayName\":\"%1\",\"abi\":%2}").arg(padding).arg(PluginAbiVersion);
    const QString path = writeTempManifest(dir, QStringLiteral("clock"), contents);
    QVERIFY(!path.isEmpty());

    const Manifest m = Manifest::parse(path, dir.filePath(QStringLiteral("clock")));
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("exceeds")));
}

void TestManifest::parseFile_rejectsEmptyFile()
{
    // An empty manifest.json is a configuration error (not a
    // malformed-JSON case). The parser flags it explicitly so the
    // caller doesn't see a confusing "malformed JSON at offset 0"
    // diagnostic.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTempManifest(dir, QStringLiteral("clock"), QString());
    QVERIFY(!path.isEmpty());

    const Manifest m = Manifest::parse(path, dir.filePath(QStringLiteral("clock")));
    QVERIFY(!m.isValid);
    QVERIFY(m.parseError.contains(QStringLiteral("empty")));
}

QTEST_MAIN(TestManifest)
#include "test_phosphor_registry_manifest.moc"
