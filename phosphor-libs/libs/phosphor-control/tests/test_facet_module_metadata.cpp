// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorControl/FacetModuleMetadata.h>

#include <QJsonDocument>
#include <QTest>

using namespace PhosphorControl;

namespace {

QJsonObject json(const char* text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

QStringList errorsFor(const char* text)
{
    QStringList errors;
    FacetModuleMetadata::parse(json(text), QLocale::c(), &errors);
    return errors;
}

} // namespace

class TestFacetModuleMetadata : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesAModule()
    {
        QStringList errors;
        const FacetModuleMetadata metadata = FacetModuleMetadata::parse(json(R"({
            "Id": "placement", "ApiVersion": 1, "Name": "Window Placement", "Description": "Snap, tile, scroll",
            "Icon": "org.phosphor_works.PlasmaZones", "Category": "window-management", "Order": 10,
            "Keywords": ["snap", "tile"], "TranslationDomain": "facet-module-placement",
            "Categories": [ { "Id": "accessibility", "Name": "Accessibility", "Icon": "a11y", "Order": 70 } ],
            "Pages": [ { "Id": "snapping", "Name": "Snapping", "Icon": "snap", "Keywords": ["drag"] } ] })"),
                                                                        QLocale::c(), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        QCOMPARE(metadata.id, QStringLiteral("placement"));
        QCOMPARE(metadata.apiVersion, 1);
        QCOMPARE(metadata.name, QStringLiteral("Window Placement"));
        QCOMPARE(metadata.category, QStringLiteral("window-management"));
        QCOMPARE(metadata.order, 10);
        QCOMPARE(metadata.keywords, (QStringList{QStringLiteral("snap"), QStringLiteral("tile")}));
        QCOMPARE(metadata.translationDomain, QStringLiteral("facet-module-placement"));
        QCOMPARE(metadata.categories.size(), 1);
        QCOMPARE(metadata.categories.first().order, 70);
        QCOMPARE(metadata.pages.size(), 1);
        QCOMPARE(metadata.pages.first().keywords, QStringList{QStringLiteral("drag")});
        QVERIFY(metadata.page(QStringLiteral("snapping")));
        QVERIFY(!metadata.page(QStringLiteral("nope")));
    }

    void picksTheBestTranslation_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<QString>("expected");
        QTest::newRow("full locale") << QStringLiteral("pt_BR") << QStringLiteral("Brasil");
        QTest::newRow("language only") << QStringLiteral("pt_PT") << QStringLiteral("Portugal");
        QTest::newRow("untranslated") << QStringLiteral("fr_FR") << QStringLiteral("English");
    }

    void picksTheBestTranslation()
    {
        QFETCH(QString, locale);
        QFETCH(QString, expected);
        QStringList errors;
        const FacetModuleMetadata metadata = FacetModuleMetadata::parse(
            json(R"({ "Id": "m", "Name": "English", "Name[pt]": "Portugal", "Name[pt_BR]": "Brasil" })"),
            QLocale(locale), &errors);
        QVERIFY(errors.isEmpty());
        QCOMPARE(metadata.name, expected);
    }

    void ordersParentPagesBeforeTheirChildren()
    {
        QStringList errors;
        const FacetModuleMetadata metadata = FacetModuleMetadata::parse(json(R"({
            "Id": "m", "Name": "M", "Pages": [
                { "Id": "a/b/c", "Name": "C", "Parent": "a/b" },
                { "Id": "a/b", "Name": "B", "Parent": "a" },
                { "Id": "z", "Name": "Z" },
                { "Id": "a", "Name": "A" } ] })"),
                                                                        QLocale::c(), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        QStringList order;
        for (const FacetPageMetadata& page : metadata.pages) {
            order.append(page.id);
        }
        QCOMPARE(
            order,
            (QStringList{QStringLiteral("a"), QStringLiteral("a/b"), QStringLiteral("a/b/c"), QStringLiteral("z")}));
    }

    void acceptsACrossModuleParent()
    {
        QVERIFY(errorsFor(R"({ "Id": "m", "Name": "M",
            "Pages": [ { "Id": "p", "Name": "P", "Parent": "placement:snapping/behavior" } ] })")
                    .isEmpty());
    }

    void parsesExtensionPointsAndExtensions()
    {
        QStringList errors;
        const FacetModuleMetadata metadata = FacetModuleMetadata::parse(json(R"({
            "Id": "m", "Name": "M",
            "Pages": [ { "Id": "p", "Name": "P", "Anchors": ["speed", "size"] } ],
            "ExtensionPoints": [ { "Page": "p", "Id": "panel", "Version": 3 } ],
            "Extensions": [ { "Id": "e", "Target": "other:page", "Slot": "after:x", "TargetVersion": 2,
                              "Name": "E", "Name[de]": "Ee", "Keywords": ["k"], "Order": 5 } ] })"),
                                                                        QLocale(QStringLiteral("de_DE")), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        QCOMPARE(metadata.page(QStringLiteral("p"))->anchors,
                 (QStringList{QStringLiteral("speed"), QStringLiteral("size")}));
        QCOMPARE(metadata.extensionPoints.size(), 1);
        QCOMPARE(metadata.extensionPoints.first().version, 3);
        const FacetExtensionMetadata* const extension = metadata.extension(QStringLiteral("e"));
        QVERIFY(extension);
        QCOMPARE(extension->target, QStringLiteral("other:page"));
        QCOMPARE(extension->slot, QStringLiteral("after:x"));
        QCOMPARE(extension->targetVersion, 2);
        QCOMPARE(extension->name, QStringLiteral("Ee"));
        QCOMPARE(extension->keywords, QStringList{QStringLiteral("k")});
        QCOMPARE(extension->order, 5);
        QVERIFY(!metadata.extension(QStringLiteral("nope")));
    }

    void offersSlotsAtTheirVersion_data()
    {
        QTest::addColumn<QString>("page");
        QTest::addColumn<QString>("slot");
        QTest::addColumn<int>("version");
        QTest::newRow("top") << QStringLiteral("p") << QStringLiteral("top") << 4;
        QTest::newRow("bottom") << QStringLiteral("p") << QStringLiteral("bottom") << 4;
        QTest::newRow("before an anchor") << QStringLiteral("p") << QStringLiteral("before:speed") << 4;
        QTest::newRow("after an anchor") << QStringLiteral("p") << QStringLiteral("after:speed") << 4;
        QTest::newRow("unknown anchor") << QStringLiteral("p") << QStringLiteral("after:size") << 0;
        QTest::newRow("named point") << QStringLiteral("p") << QStringLiteral("panel") << 2;
        QTest::newRow("anchors is not a slot") << QStringLiteral("p") << QStringLiteral("anchors") << 0;
        QTest::newRow("unknown point") << QStringLiteral("p") << QStringLiteral("other") << 0;
        QTest::newRow("default anchors version") << QStringLiteral("q") << QStringLiteral("top") << 1;
        QTest::newRow("point on another page") << QStringLiteral("q") << QStringLiteral("panel") << 0;
        QTest::newRow("unknown page") << QStringLiteral("z") << QStringLiteral("top") << 0;
    }

    void offersSlotsAtTheirVersion()
    {
        QFETCH(QString, page);
        QFETCH(QString, slot);
        QFETCH(int, version);
        QStringList errors;
        const FacetModuleMetadata metadata = FacetModuleMetadata::parse(json(R"({
            "Id": "m", "Name": "M",
            "Pages": [ { "Id": "p", "Name": "P", "Anchors": ["speed"] }, { "Id": "q", "Name": "Q" } ],
            "ExtensionPoints": [ { "Page": "p", "Id": "anchors", "Version": 4 },
                                 { "Page": "p", "Id": "panel", "Version": 2 } ] })"),
                                                                        QLocale::c(), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        QCOMPARE(metadata.slotVersion(page, slot), version);
    }

    void parsesSearchEntries()
    {
        QStringList errors;
        const FacetModuleMetadata metadata = FacetModuleMetadata::parse(json(R"({
            "Id": "m", "Name": "M", "SearchProviders": true,
            "Pages": [ { "Id": "p", "Name": "P", "Anchors": ["speed", "size"] } ],
            "Search": [ { "Page": "p", "Anchor": "speed", "Title": "Speed", "Title[de]": "Tempo",
                          "Keywords": ["fast"], "AdvancedOnly": true },
                        { "Page": "p", "Anchor": "size", "Kind": "Section", "Title": "Size" } ] })"),
                                                                        QLocale(QStringLiteral("de_DE")), &errors);
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        QVERIFY(metadata.searchProviders);
        QCOMPARE(metadata.search.size(), 2);
        QCOMPARE(metadata.search.at(0).title, QStringLiteral("Tempo"));
        QCOMPARE(metadata.search.at(0).keywords, QStringList{QStringLiteral("fast")});
        QVERIFY(metadata.search.at(0).advancedOnly);
        QVERIFY(!metadata.search.at(0).section);
        QVERIFY(metadata.search.at(1).section);
        QVERIFY(
            !FacetModuleMetadata::parse(json(R"({ "Id": "m", "Name": "M" })"), QLocale::c(), &errors).searchProviders);
    }

    void reportsEveryProblem_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("no id") << QByteArray(R"({ "Name": "M" })") << QStringLiteral("\"Id\"");
        QTest::newRow("bad id") << QByteArray(R"({ "Id": "a:b", "Name": "M" })") << QStringLiteral("\"Id\"");
        QTest::newRow("no name") << QByteArray(R"({ "Id": "m" })") << QStringLiteral("\"Name\" is missing");
        QTest::newRow("bad category") << QByteArray(R"({ "Id": "m", "Name": "M", "Category": "Bad Cat" })")
                                      << QStringLiteral("\"Category\"");
        QTest::newRow("own parent") << QByteArray(R"({ "Id": "m", "Name": "M", "Parent": "m" })")
                                    << QStringLiteral("names the module itself");
        QTest::newRow("page with colon") << QByteArray(
            R"({ "Id": "m", "Name": "M", "Pages": [ { "Id": "a:b", "Name": "X" } ] })")
                                         << QStringLiteral("page id a:b");
        QTest::newRow("page without name") << QByteArray(R"({ "Id": "m", "Name": "M", "Pages": [ { "Id": "a" } ] })")
                                           << QStringLiteral("page a has no \"Name\"");
        QTest::newRow("duplicate page") << QByteArray(
            R"({ "Id": "m", "Name": "M", "Pages": [ { "Id": "a", "Name": "A" }, { "Id": "a", "Name": "B" } ] })")
                                        << QStringLiteral("declared twice");
        QTest::newRow("unknown local parent")
            << QByteArray(R"({ "Id": "m", "Name": "M", "Pages": [ { "Id": "a", "Name": "A", "Parent": "x" } ] })")
            << QStringLiteral("does not declare");
        QTest::newRow("malformed cross parent")
            << QByteArray(R"({ "Id": "m", "Name": "M", "Pages": [ { "Id": "a", "Name": "A", "Parent": ":x" } ] })")
            << QStringLiteral("<module>:<page>");
        QTest::newRow("cycle") << QByteArray(R"({ "Id": "m", "Name": "M", "Pages": [
                   { "Id": "a", "Name": "A", "Parent": "b" }, { "Id": "b", "Name": "B", "Parent": "a" } ] })")
                               << QStringLiteral("its own ancestor");
        QTest::newRow("category without name")
            << QByteArray(R"({ "Id": "m", "Name": "M", "Categories": [ { "Id": "c" } ] })")
            << QStringLiteral("category c has no \"Name\"");

        QTest::newRow("point on an undeclared page")
            << QByteArray(R"({ "Id": "m", "Name": "M", "ExtensionPoints": [ { "Page": "x", "Id": "p" } ] })")
            << QStringLiteral("which the module does not declare");
        QTest::newRow("point named top") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Pages": [ { "Id": "a", "Name": "A" } ], "ExtensionPoints": [ { "Page": "a", "Id": "top" } ] })")
                                         << QStringLiteral("not be top or bottom");
        QTest::newRow("point version zero") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Pages": [ { "Id": "a", "Name": "A" } ],
                   "ExtensionPoints": [ { "Page": "a", "Id": "p", "Version": 0 } ] })")
                                            << QStringLiteral("which is below 1");
        QTest::newRow("point declared twice") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Pages": [ { "Id": "a", "Name": "A" } ],
                   "ExtensionPoints": [ { "Page": "a", "Id": "p" }, { "Page": "a", "Id": "p" } ] })")
                                              << QStringLiteral("extension point p is declared twice");
        QTest::newRow("extension without id") << QByteArray(
            R"({ "Id": "m", "Name": "M", "Extensions": [ { "Target": "o:p", "Slot": "top", "Name": "E" } ] })")
                                              << QStringLiteral("extension id  must match");
        QTest::newRow("extension declared twice") << QByteArray(R"({ "Id": "m", "Name": "M", "Extensions": [
                   { "Id": "e", "Target": "o:p", "Slot": "top", "Name": "E" },
                   { "Id": "e", "Target": "o:p", "Slot": "top", "Name": "E" } ] })")
                                                  << QStringLiteral("extension e is declared twice");
        QTest::newRow("extension with a local target") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Extensions": [ { "Id": "e", "Target": "p", "Slot": "top", "Name": "E" } ] })")
                                                       << QStringLiteral("which is not <module>:<page>");
        QTest::newRow("extension on its own module") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Extensions": [ { "Id": "e", "Target": "m:p", "Slot": "top", "Name": "E" } ] })")
                                                     << QStringLiteral("targets its own module");
        QTest::newRow("extension with a bad slot") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Extensions": [ { "Id": "e", "Target": "o:p", "Slot": "after:", "Name": "E" } ] })")
                                                   << QStringLiteral("has slot after:");
        QTest::newRow("extension target version zero") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Extensions": [ { "Id": "e", "Target": "o:p", "Slot": "top", "TargetVersion": 0, "Name": "E" } ] })")
                                                       << QStringLiteral("TargetVersion 0");
        QTest::newRow("extension without name") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Extensions": [ { "Id": "e", "Target": "o:p", "Slot": "top" } ] })")
                                                << QStringLiteral("extension e has no \"Name\"");
        QTest::newRow("anchor with a colon") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Pages": [ { "Id": "p", "Name": "P", "Anchors": ["a:b"] } ] })")
                                             << QStringLiteral("has anchor a:b");
        QTest::newRow("anchor listed twice") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Pages": [ { "Id": "p", "Name": "P", "Anchors": ["a", "a"] } ] })")
                                             << QStringLiteral("has anchor a,");
        QTest::newRow("search entry on an undeclared page") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Search": [ { "Page": "x", "Anchor": "a", "Title": "T" } ] })")
                                                            << QStringLiteral("search entry x#a is on page x");
        QTest::newRow("search entry at an unlisted anchor") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Pages": [ { "Id": "p", "Name": "P", "Anchors": ["a"] } ],
                   "Search": [ { "Page": "p", "Anchor": "b", "Title": "T" } ] })")
                                                            << QStringLiteral("names anchor b");
        QTest::newRow("search entry of an unknown kind") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Pages": [ { "Id": "p", "Name": "P", "Anchors": ["a"] } ],
                   "Search": [ { "Page": "p", "Anchor": "a", "Kind": "Thing", "Title": "T" } ] })")
                                                         << QStringLiteral("\"Kind\" Thing");
        QTest::newRow("search entry without a title") << QByteArray(R"({ "Id": "m", "Name": "M",
                   "Pages": [ { "Id": "p", "Name": "P", "Anchors": ["a"] } ],
                   "Search": [ { "Page": "p", "Anchor": "a" } ] })")
                                                      << QStringLiteral("p#a has no \"Title\"");
    }

    void reportsEveryProblem()
    {
        QFETCH(QByteArray, text);
        QFETCH(QString, expected);
        const QStringList errors = errorsFor(text.constData());
        QVERIFY2(errors.join(QLatin1Char('\n')).contains(expected), qPrintable(errors.join(QLatin1Char('\n'))));
    }
};

QTEST_GUILESS_MAIN(TestFacetModuleMetadata)
#include "test_facet_module_metadata.moc"
