// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Author-time checks on data/whatsnew.json, read straight out of the source
// tree. The schema covers shape (the New/Changed/Fixed prefix, the version
// pattern, the ISO date); what it cannot express is the ORDERING invariant the
// settings dialog depends on. WhatsNewPage reads entry 0 as the newest
// release, seeds its version rail from that entry's series, and lists series
// in array order, while SettingsController::loadWhatsNew() sorts on load. A
// release appended to the end of the file is therefore correct at runtime but
// wrong in the file, and the difference is invisible until someone reads the
// raw JSON. These cases pin both halves: the file is newest-first as written,
// and every version in it parses as a QVersionNumber so the sort has something
// to order by.

#include <QtTest>

#include <PhosphorFsLoader/SchemaValidator.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QVersionNumber>

Q_LOGGING_CATEGORY(lcWhatsNewTest, "plasmazones.test.whatsnew")

namespace {

QJsonObject loadWhatsNew()
{
    QFile file(QStringLiteral(P_SOURCE_DIR "/data/whatsnew.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        return {};
    }
    return doc.object();
}

} // namespace

class TestWhatsNewData : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesAsJson();
    void validatesAgainstSchema();
    void versionsAreParseable();
    void releasesAreNewestFirst();
    void versionsAreUnique();
    void everyHighlightCarriesAKind();
};

void TestWhatsNewData::parsesAsJson()
{
    const auto root = loadWhatsNew();
    QVERIFY(!root.isEmpty());
    QVERIFY(root.value(QLatin1String("releases")).isArray());
    QVERIFY(!root.value(QLatin1String("releases")).toArray().isEmpty());
}

void TestWhatsNewData::validatesAgainstSchema()
{
    const auto validator = PhosphorFsLoader::SchemaValidator::fromResource(
        QStringLiteral(P_SOURCE_DIR "/data/schemas/whatsnew.schema.json"), lcWhatsNewTest());
    const auto errors = validator.validate(loadWhatsNew());
    if (errors) {
        PhosphorFsLoader::logSchemaErrors(lcWhatsNewTest(), *errors);
    }
    QVERIFY(!errors);
}

void TestWhatsNewData::versionsAreParseable()
{
    const auto releases = loadWhatsNew().value(QLatin1String("releases")).toArray();
    for (const auto& entry : releases) {
        const QString version = entry.toObject().value(QLatin1String("version")).toString();
        const QVersionNumber parsed = QVersionNumber::fromString(version);
        QVERIFY2(!parsed.isNull(), qPrintable(QStringLiteral("unparseable version: %1").arg(version)));
    }
}

void TestWhatsNewData::releasesAreNewestFirst()
{
    const auto releases = loadWhatsNew().value(QLatin1String("releases")).toArray();
    QVERIFY(releases.size() > 1);
    for (int i = 1; i < releases.size(); ++i) {
        const QString previous = releases.at(i - 1).toObject().value(QLatin1String("version")).toString();
        const QString current = releases.at(i).toObject().value(QLatin1String("version")).toString();
        QVERIFY2(QVersionNumber::fromString(current) < QVersionNumber::fromString(previous),
                 qPrintable(QStringLiteral("out of order: %1 is listed before %2").arg(previous, current)));
    }
}

void TestWhatsNewData::versionsAreUnique()
{
    const auto releases = loadWhatsNew().value(QLatin1String("releases")).toArray();
    QSet<QString> seen;
    for (const auto& entry : releases) {
        const QString version = entry.toObject().value(QLatin1String("version")).toString();
        QVERIFY2(!seen.contains(version), qPrintable(QStringLiteral("duplicate release: %1").arg(version)));
        seen.insert(version);
    }
}

// The dialog badges and filters on the prefix, so a highlight without one
// renders as a bare bullet that no kind chip can reach. The schema enforces
// this too; the case is here so a schema relaxed by accident still fails.
void TestWhatsNewData::everyHighlightCarriesAKind()
{
    const auto releases = loadWhatsNew().value(QLatin1String("releases")).toArray();
    for (const auto& entry : releases) {
        const auto obj = entry.toObject();
        const auto highlights = obj.value(QLatin1String("highlights")).toArray();
        QVERIFY(!highlights.isEmpty());
        for (const auto& h : highlights) {
            const QString text = h.toString();
            const bool prefixed = text.startsWith(QLatin1String("New: ")) || text.startsWith(QLatin1String("Changed: "))
                || text.startsWith(QLatin1String("Fixed: "));
            QVERIFY2(prefixed,
                     qPrintable(QStringLiteral("highlight without a kind prefix in %1: %2")
                                    .arg(obj.value(QLatin1String("version")).toString(), text)));
        }
    }
}

QTEST_MAIN(TestWhatsNewData)
#include "test_whatsnew_data.moc"
