// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorControl/FacetTestHost.h>

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
