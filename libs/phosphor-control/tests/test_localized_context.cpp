// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "PhosphorControl/LocalizedContext.h"

using PhosphorControl::LocalizedContext;

namespace {
/// RAII guard for the QCoreApplication::applicationName setting. The
/// previous manual snapshot/restore pattern was easy to forget on the
/// failure path of any nested QCOMPARE/QVERIFY — a RAII type cleans
/// up automatically even when QTest aborts mid-test.
class ApplicationNameGuard
{
public:
    explicit ApplicationNameGuard(const QString& temporaryName)
        : m_previous(QCoreApplication::applicationName())
    {
        QCoreApplication::setApplicationName(temporaryName);
    }
    ~ApplicationNameGuard()
    {
        QCoreApplication::setApplicationName(m_previous);
    }
    ApplicationNameGuard(const ApplicationNameGuard&) = delete;
    ApplicationNameGuard& operator=(const ApplicationNameGuard&) = delete;

private:
    QString m_previous;
};
} // namespace

class TestLocalizedContext : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initialContextMatchesApplicationName()
    {
        // Set an explicit applicationName so the test actually verifies
        // the fallback contract (translationContext follows applicationName
        // when no explicit override is set) rather than comparing two
        // empty strings.
        ApplicationNameGuard guard(QStringLiteral("test-app-localized"));
        LocalizedContext ctx;
        QCOMPARE(ctx.translationContext(), QStringLiteral("test-app-localized"));
    }

    void applicationNameChangeEmitsNotify()
    {
        // Confirms the ctor-time connect to QCoreApplication::applicationNameChanged:
        // a late setApplicationName must re-fire translationContextChanged when
        // no explicit override is set.
        ApplicationNameGuard guard(QStringLiteral("test-app-pre"));
        LocalizedContext ctx;
        QSignalSpy spy(&ctx, &LocalizedContext::translationContextChanged);
        QCoreApplication::setApplicationName(QStringLiteral("test-app-post"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(ctx.translationContext(), QStringLiteral("test-app-post"));
    }

    void i18nReturnsInputWhenNoTranslatorInstalled()
    {
        LocalizedContext ctx;
        QCOMPARE(ctx.i18n(QStringLiteral("Hello")), QStringLiteral("Hello"));
    }

    void i18ncReturnsInputWhenNoTranslatorInstalled()
    {
        LocalizedContext ctx;
        QCOMPARE(ctx.i18nc(QStringLiteral("greeting"), QStringLiteral("Hello")), QStringLiteral("Hello"));
    }

    void i18npChoosesSingularForOne()
    {
        LocalizedContext ctx;
        // Qt's translate() substitutes %n with the count, so "%n item" + n=1
        // becomes "1 item".
        QCOMPARE(ctx.i18np(QStringLiteral("%n item"), QStringLiteral("%n items"), 1), QStringLiteral("1 item"));
    }

    void i18npChoosesPluralForMany()
    {
        LocalizedContext ctx;
        QCOMPARE(ctx.i18np(QStringLiteral("%n item"), QStringLiteral("%n items"), 3), QStringLiteral("3 items"));
    }

    void translationContextSettable()
    {
        LocalizedContext ctx;
        QSignalSpy spy(&ctx, &LocalizedContext::translationContextChanged);

        ctx.setTranslationContext(QStringLiteral("MyApp"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(ctx.translationContext(), QStringLiteral("MyApp"));

        // Same value doesn't re-emit.
        ctx.setTranslationContext(QStringLiteral("MyApp"));
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestLocalizedContext)
#include "test_localized_context.moc"
