// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Ties BarController's registered type names to the QML files that actually
// ship in the Phosphor.Bar module.
//
// The built-ins are bound by STRING across two trees: BarController names
// a type, and the module supplies it from libs/phosphor-shell-bar. Renaming a
// QML file, dropping it from QML_FILES, or changing its QT_RESOURCE_ALIAS all
// compile and link cleanly and degrade to a runtime warning plus a widget
// that silently never appears in the bar. Nothing but this test catches that.
//
// The delegates cannot be INSTANTIATED here — they import Phosphor.Shell and
// the Phosphor.Service.* modules, which only the shell process registers — so
// the assertion is on QQmlComponent::url(), which is set as soon as the type
// name resolves to a file, before those imports are evaluated. That cleanly
// separates "this type is not in the module" from "the delegate's own imports
// are unavailable in a unit test".

#include "shell/BarController.h"
#include "shell/QmlComponentBarWidgetFactory.h"

#include <PhosphorTheme/AppearanceStore.h>
#include <QFile>
#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QRegularExpression>
#include <QStringList>
#include <QTest>

#include <utility>

using namespace PhosphorShellApp;

class TestBarWidgetTypes : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void everyBuiltinTypeResolvesInTheModule();
    void theCatalogueMatchesTheRegisteredIds();
    void theDefaultBarLayoutOnlyUsesRegisteredIds();
    void anUnknownTypeNameIsRefused();
};

void TestBarWidgetTypes::everyBuiltinTypeResolvesInTheModule()
{
    QQmlEngine engine;

    for (const BarController::BuiltinWidget& widget : BarController::builtinWidgets()) {
        QQmlComponent component(&engine, BarController::moduleUri(), widget.typeName);

        // url() is non-empty once the module supplies a file for this type
        // name. It stays empty when the name is absent from the module,
        // which is exactly the rename / dropped-QML_FILES failure.
        QVERIFY2(!component.url().isEmpty(),
                 qPrintable(QStringLiteral("type '%1' (id '%2') does not resolve in %3: %4")
                                .arg(widget.typeName, widget.id, BarController::moduleUri(), component.errorString())));
    }
}

void TestBarWidgetTypes::theCatalogueMatchesTheRegisteredIds()
{
    // registerBuiltins() iterates builtinWidgets(), so a divergence here
    // would mean a registration path that bypasses the shared catalogue.
    BarController controller;

    QStringList expected;
    for (const BarController::BuiltinWidget& widget : BarController::builtinWidgets()) {
        expected << widget.id;
    }
    expected.sort();

    QCOMPARE(controller.factoryIds(), expected);
}

void TestBarWidgetTypes::theDefaultBarLayoutOnlyUsesRegisteredIds()
{
    // Read the ids straight out of the SHIPPED BarHost.qml rather than a
    // hand-copy. A copy could only catch an author who edited both places and
    // got the second one wrong; the failure that actually matters is a typo
    // in the QML alone, which drops a widget from the bar with nothing but a
    // runtime warning. The module resource is linked into this binary, so the
    // file the shell loads is the file under test.
    QFile barHost(QStringLiteral(":/qt/qml/Phosphor/Bar/BarHost.qml"));
    QVERIFY2(barHost.open(QIODevice::ReadOnly | QIODevice::Text),
             "BarHost.qml is not in the module resource; the layout guard cannot run");
    const QString source = QString::fromUtf8(barHost.readAll());

    // Evaluate the shipped bindings at both responsive widths. This checks
    // every branch and concat result, rather than assuming a literal array.
    static const QRegularExpression declaration(
        QStringLiteral("readonly\\s+property\\s+string\\s+_(?:left|center|right)GroupsJson\\s*:\\s*"));
    QStringList layoutIds;
    int count = 0;
    auto declarations = declaration.globalMatch(source);
    while (declarations.hasNext()) {
        const qsizetype start = declarations.next().capturedEnd();
        qsizetype end = start;
        int depth = 0;
        for (; end < source.size(); ++end) {
            const QChar c = source.at(end);
            if (c == QLatin1Char('[') || c == QLatin1Char('(') || c == QLatin1Char('{')) {
                ++depth;
            } else if (c == QLatin1Char(']') || c == QLatin1Char(')') || c == QLatin1Char('}')) {
                --depth;
            } else if (c == QLatin1Char('\n') && depth == 0) {
                break;
            }
        }
        QCOMPARE(depth, 0);
        const QString expression = source.mid(start, end - start);
        for (const int width : {400, 800, 1440}) {
            QJSEngine evaluator;
            auto appearance = evaluator.newObject();
            appearance.setProperty(QStringLiteral("settings"),
                                   evaluator.toScriptValue(PhosphorTheme::AppearanceStore::defaults()));
            evaluator.globalObject().setProperty(QStringLiteral("Appearance"), appearance);
            auto panel = evaluator.newObject();
            panel.setProperty(QStringLiteral("width"), width);
            evaluator.globalObject().setProperty(QStringLiteral("panel"), panel);
            const auto value = evaluator.evaluate(QStringLiteral("JSON.parse(") + expression + QLatin1Char(')'));
            QVERIFY2(!value.isError(), qPrintable(value.toString()));
            QVERIFY(value.isArray());
            for (const auto& group : value.toVariant().toList()) {
                for (const auto& id : group.toList()) {
                    layoutIds.append(id.toString());
                }
            }
        }
        ++count;
    }
    QCOMPARE(count, 3);
    QVERIFY(!layoutIds.isEmpty());

    BarController controller;
    const QStringList registered = controller.factoryIds();

    for (const QString& id : std::as_const(layoutIds)) {
        QVERIFY2(registered.contains(id),
                 qPrintable(QStringLiteral("the shipped bar layout uses unregistered id '%1'").arg(id)));
    }
}

void TestBarWidgetTypes::anUnknownTypeNameIsRefused()
{
    // The branch a renamed or dropped delegate would take at runtime.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("component error for")));

    QmlComponentBarWidgetFactory factory(QStringLiteral("bogus"), QStringLiteral("Bogus"), BarController::moduleUri(),
                                         QStringLiteral("NoSuchDelegate"));
    QQmlEngine engine;
    QQuickItem parent;

    QCOMPARE(factory.createWidget(&engine, &parent), nullptr);
}

QTEST_MAIN(TestBarWidgetTypes)

#include "test_bar_widget_types.moc"
