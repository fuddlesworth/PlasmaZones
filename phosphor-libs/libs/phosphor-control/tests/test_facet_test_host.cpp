// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorControl/FacetTestHost.h>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#ifndef PHOSPHOR_CONTROL_FIXTURE_DIR
#error "PHOSPHOR_CONTROL_FIXTURE_DIR must be defined by tests/CMakeLists.txt"
#endif

using namespace PhosphorControl;

namespace {
QString fixture(const char* variant)
{
    return QStringLiteral(PHOSPHOR_CONTROL_FIXTURE_DIR) + QLatin1Char('/') + QLatin1String(variant)
        + QStringLiteral(".so");
}
} // namespace

class TestFacetTestHost : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aWellBehavedModulePasses()
    {
        FacetTestHost host(fixture("good"));
        const QStringList problems = host.run();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    }

    void reportsAModuleThatSkipsAPage()
    {
        FacetTestHost host(fixture("missingpage"));
        const QString problems = host.run().join(QLatin1Char('\n'));
        QVERIFY2(problems.contains(QStringLiteral("did not register page general/advanced")), qPrintable(problems));
    }

    void anExtenderPassesWithoutItsTargets()
    {
        // Its extension domains go through Apply and Discard like any other.
        FacetTestHost host(fixture("extender"));
        const QStringList problems = host.run();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    }

    void reportsAModuleThatSkipsAnExtension()
    {
        FacetTestHost host(fixture("missingextension"));
        const QString problems = host.run().join(QLatin1Char('\n'));
        QVERIFY2(problems.contains(QStringLiteral("did not register extension second")), qPrintable(problems));
    }

    void checksSearchProviders()
    {
        FacetTestHost good(fixture("providers"));
        QStringList problems = good.run();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));

        FacetTestHost stray(fixture("strayprovider"));
        problems = stray.run();
        QCOMPARE(problems.size(), 1);
        QVERIFY2(problems.first().contains(QStringLiteral("elsewhere:page")), qPrintable(problems.first()));
    }

    void checksExtensionsAgainstTheirTargets()
    {
        FacetTestHost fits(fixture("extender"));
        fits.addTargetModule(fixture("good"));
        QStringList problems = fits.run();
        QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));

        FacetTestHost misfits(fixture("badextender"));
        misfits.addTargetModule(fixture("good"));
        problems = misfits.run();
        QCOMPARE(problems.size(), 5);
        for (const QString& problem : std::as_const(problems)) {
            QVERIFY2(problem.startsWith(QStringLiteral("extension badextender:")), qPrintable(problem));
        }
    }

    void checksAnchorsAgainstThePreviousRelease()
    {
        QTemporaryDir dir;
        const auto previous = [&dir](const QByteArray& json) {
            const QString path = dir.filePath(QStringLiteral("anchors.json"));
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(json) != json.size()) {
                qWarning() << "cannot write" << path;
            }
            return path;
        };
        const auto run = [](const QString& anchors) {
            FacetTestHost host(fixture("good"));
            host.setPreviousAnchors(anchors);
            return host.run();
        };
        // Unchanged.
        QCOMPARE(
            run(previous(R"({ "Module": "good", "Pages": { "general": { "Version": 1, "Anchors": ["speed"] } } })")),
            QStringList());
        // An anchor and a page gone with no version bump.
        QStringList problems = run(previous(R"({ "Module": "good", "Pages": {
            "general": { "Version": 1, "Anchors": ["speed", "gone"] }, "removed": { "Version": 1, "Anchors": [] } } })"));
        QCOMPARE(problems.size(), 2);
        QVERIFY2(problems.join(QLatin1Char('\n')).contains(QStringLiteral("anchor gone of page general")),
                 qPrintable(problems.join(QLatin1Char('\n'))));
        QVERIFY2(problems.join(QLatin1Char('\n')).contains(QStringLiteral("page removed")),
                 qPrintable(problems.join(QLatin1Char('\n'))));
        // The previous release had a higher version than this one.
        problems = run(previous(R"({ "Module": "good", "Pages": { "general": { "Version": 3, "Anchors": [] } } })"));
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().contains(QStringLiteral("went down")));
        // Another module's file.
        problems = run(previous(R"({ "Module": "other", "Pages": {} })"));
        QCOMPARE(problems.size(), 1);
        QVERIFY(problems.first().contains(QStringLiteral("not good")));
    }

    void reportsADomainThatAnswersTwice()
    {
        FacetTestHost host(fixture("doubleresult"));
        host.setResultTimeoutMs(200);
        const QStringList problems = host.run();
        // Two pages and one headless domain, each answering apply() twice.
        QCOMPARE(problems.size(), 3);
        for (const QString& problem : problems) {
            QVERIFY2(problem.contains(QStringLiteral("applyResult 2 times")), qPrintable(problem));
        }
    }

    void reportsARefusedModule()
    {
        FacetTestHost host(fixture("wrongapi"));
        const QString problems = host.run().join(QLatin1Char('\n'));
        QVERIFY2(problems.contains(QStringLiteral("refused")) && problems.contains(QStringLiteral("ApiVersion 2")),
                 qPrintable(problems));
    }

    void reportsAMissingFile()
    {
        FacetTestHost host(QStringLiteral("/nonexistent/module.so"));
        QCOMPARE(host.run(), QStringList{QStringLiteral("no module file at /nonexistent/module.so")});
    }
};

QTEST_GUILESS_MAIN(TestFacetTestHost)
#include "test_facet_test_host.moc"
