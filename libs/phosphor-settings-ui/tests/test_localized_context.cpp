// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

#include "PhosphorSettingsUi/LocalizedContext.h"

using PhosphorSettingsUi::LocalizedContext;

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
        const QString prev = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(QStringLiteral("test-app-localized"));
        {
            LocalizedContext ctx;
            QCOMPARE(ctx.translationContext(), QStringLiteral("test-app-localized"));
        }
        QCoreApplication::setApplicationName(prev);
    }

    void applicationNameChangeEmitsNotify()
    {
        // Confirms the ctor-time connect to QCoreApplication::applicationNameChanged:
        // a late setApplicationName must re-fire translationContextChanged when
        // no explicit override is set.
        const QString prev = QCoreApplication::applicationName();
        QCoreApplication::setApplicationName(QStringLiteral("test-app-pre"));
        LocalizedContext ctx;
        QSignalSpy spy(&ctx, &LocalizedContext::translationContextChanged);
        QCoreApplication::setApplicationName(QStringLiteral("test-app-post"));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(ctx.translationContext(), QStringLiteral("test-app-post"));
        QCoreApplication::setApplicationName(prev);
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
