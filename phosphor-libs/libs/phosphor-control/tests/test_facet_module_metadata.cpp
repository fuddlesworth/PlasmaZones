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
