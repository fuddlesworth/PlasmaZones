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

#include <QFile>
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

    // property var leftGroups: [["focusedapp"]]  ->  the quoted ids within.
    //
    // Escaped literals rather than raw strings: moc's preprocessor mis-parses
    // a raw string containing parentheses and quotes, and fails this file
    // with "missing ')' in macro usage".
    // Balance-aware extraction, not a regex capture over the literal: any
    // regex form has a truncation mode (a lazy multi-line match ends at
    // the first `]` that reaches end-of-line, a greedy one swallows the
    // next declaration), and a truncated match still LOOKS parsed, so no
    // count check catches it. Locating each declaration and scanning to
    // the matching bracket by depth is format-proof: one-line and
    // reformatted multi-line lists both extract in full.
    static const QRegularExpression groupsDeclaration(
        QStringLiteral("property\\s+var\\s+(?:left|center|right)Groups\\s*:\\s*"));
    static const QRegularExpression quotedId(QStringLiteral("\"([^\"]+)\""));

    QStringList layoutIds;
    int parsedDeclarations = 0;
    int declaredCount = 0;
    auto declarations = groupsDeclaration.globalMatch(source);
    while (declarations.hasNext()) {
        const auto match = declarations.next();
        ++declaredCount;
        const qsizetype open = match.capturedEnd(0);
        if (open >= source.size() || source.at(open) != QLatin1Char('[')) {
            continue; // counted but not parsed: the QCOMPARE below fails loudly
        }
        int depth = 0;
        qsizetype close = open;
        for (; close < source.size(); ++close) {
            const QChar c = source.at(close);
            if (c == QLatin1Char('[')) {
                ++depth;
            } else if (c == QLatin1Char(']') && --depth == 0) {
                break;
            }
        }
        if (depth != 0) {
            continue; // unbalanced: same loud failure below
        }
        ++parsedDeclarations;
        const QString literal = source.mid(open, close - open + 1);
        auto ids = quotedId.globalMatch(literal);
        while (ids.hasNext()) {
            layoutIds << ids.next().captured(1);
        }
    }
    // Every declaration must have produced a fully-extracted literal, so a
    // formatting change that defeats the extraction fails loudly instead
    // of shrinking coverage in silence.
    QCOMPARE(parsedDeclarations, declaredCount);

    // Guard the guard: a parse that silently found nothing would pass
    // vacuously forever.
    QVERIFY2(!layoutIds.isEmpty(), "no *Groups ids parsed out of BarHost.qml; the layout guard is vacuous");

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
