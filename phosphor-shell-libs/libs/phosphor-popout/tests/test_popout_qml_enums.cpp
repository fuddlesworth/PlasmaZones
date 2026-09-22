// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// The Anchor and ExclusiveMode enums are declared in C++ but exist for QML
// callers, and the path QML reaches them by is not obvious from the
// declaration: QML_ELEMENT on a Q_NAMESPACE publishes the NAMESPACE as the
// type name, so the values live at PhosphorPopout.Anchor.ScreenCenter and
// not at a bare Anchor.ScreenCenter.
//
// This is not a hypothetical. The shell shipped `Anchor.ScreenCenter` and
// the power menu failed at runtime with "ReferenceError: Anchor is not
// defined", because nothing in the C++ tests exercises the QML-facing path
// and the enum only ever reaches QML through a QVariantMap the compiler
// never checks.

#include <PhosphorPopout/IPopoutTransport.h>
#include <PhosphorPopout/PopoutController.h>
#include <PhosphorPopout/PopoutRequest.h>

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTest>

#include <memory>

using namespace PhosphorPopout;

namespace {

// Keeps the whole request rather than just its id, which is what this file
// needs and what the shared FakeTransport in the sibling tests does not
// record. Every surface stays notionally open; nothing here closes one.
class RecordingTransport : public IPopoutTransport
{
public:
    QString openSurface(const PopoutRequest& request) override
    {
        requests.append(request);
        return QStringLiteral("h%1").arg(requests.size());
    }

    void closeSurface(const QString&) override
    {
    }

    void setSurfaceDismissedCallback(std::function<void(const QString&)>) override
    {
    }

    QList<PopoutRequest> requests;
};

// Evaluates one QML expression against an engine with Phosphor.Popout
// imported, and hands back its value. Errors surface as an invalid
// QVariant plus the component's error string, so a failing case reports
// the ReferenceError rather than an opaque mismatch.
QVariant evaluate(const QString& expression, QString* errorOut)
{
    QQmlEngine engine;
    const QString source = QStringLiteral(R"(
import QtQml
import Phosphor.Popout
QtObject { property var probe: %1 }
)")
                               .arg(expression);

    QQmlComponent component(&engine);
    component.setData(source.toUtf8(), QUrl(QStringLiteral("qrc:/tst_popout_enums.qml")));
    std::unique_ptr<QObject> object(component.create());
    if (!object) {
        if (errorOut)
            *errorOut = component.errorString();
        return {};
    }
    // A ReferenceError inside a property binding does not fail creation; it
    // leaves the property undefined and logs. Both failure shapes therefore
    // have to be treated as failure, which is what an invalid QVariant means
    // to the callers below.
    return object->property("probe");
}

} // namespace

class TestPopoutQmlEnums : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // The documented access path resolves, and resolves to the same value
    // the C++ enumerator has. Comparing against the enumerator rather than a
    // literal means reordering the enum cannot make this test pass wrongly.
    void qualifiedEnumPathResolves_data()
    {
        QTest::addColumn<QString>("expression");
        QTest::addColumn<int>("expected");

        QTest::newRow("Anchor.BarCenter")
            << QStringLiteral("PhosphorPopout.Anchor.BarCenter") << int(Anchor::BarCenter);
        QTest::newRow("Anchor.ScreenCenter")
            << QStringLiteral("PhosphorPopout.Anchor.ScreenCenter") << int(Anchor::ScreenCenter);
        QTest::newRow("Anchor.Custom") << QStringLiteral("PhosphorPopout.Anchor.Custom") << int(Anchor::Custom);
        QTest::newRow("ExclusiveMode.Cooperative")
            << QStringLiteral("PhosphorPopout.ExclusiveMode.Cooperative") << int(ExclusiveMode::Cooperative);
        QTest::newRow("ExclusiveMode.Modal")
            << QStringLiteral("PhosphorPopout.ExclusiveMode.Modal") << int(ExclusiveMode::Modal);
        QTest::newRow("ExclusiveMode.Detached")
            << QStringLiteral("PhosphorPopout.ExclusiveMode.Detached") << int(ExclusiveMode::Detached);
    }

    void qualifiedEnumPathResolves()
    {
        QFETCH(QString, expression);
        QFETCH(int, expected);

        QString error;
        const QVariant value = evaluate(expression, &error);
        QVERIFY2(value.isValid(), qPrintable(QStringLiteral("%1 did not resolve: %2").arg(expression, error)));
        QCOMPARE(value.toInt(), expected);
    }

    // The counterpart. Without this the test above would still pass if a
    // future change made the enums available unqualified as well, and the
    // header comment telling readers to qualify would rot unnoticed.
    //
    // This asserts a NEGATIVE, so it would also pass for the wrong reason if
    // `import Phosphor.Popout` ever stopped resolving at all. The case guards
    // itself against that by first asserting a qualified path DOES resolve,
    // rather than relying on a reader noticing the sibling rows went red too.
    void unqualifiedEnumNameIsNotInScope()
    {
        QString error;
        // Self-guard FIRST. Without it this case passes for the wrong reason
        // whenever the module itself stops resolving: every expression would
        // then fail to evaluate, including the one asserted absent below.
        QVERIFY2(
            evaluate(QStringLiteral("PhosphorPopout.Anchor.ScreenCenter"), &error).isValid(),
            qPrintable(QStringLiteral("module did not resolve, so the negative below proves nothing: %1").arg(error)));

        const QVariant anchor = evaluate(QStringLiteral("Anchor.ScreenCenter"), &error);
        QVERIFY(!anchor.isValid());
        const QVariant mode = evaluate(QStringLiteral("ExclusiveMode.Modal"), &error);
        QVERIFY(!mode.isValid());
    }

    // The whole path the shell actually uses: a JS object literal carrying
    // the qualified enums, handed to PopoutController.toggle() from QML and
    // converted to the PopoutRequest gadget on the way in. Reaching the
    // transport with the right anchor is the only thing that proves the
    // enums survive; a value QML failed to convert arrives as the field
    // default (BarCenter / Cooperative) with no error raised anywhere,
    // which is a silently wrong popout rather than a visible failure.
    void requestFromQmlReachesTransportWithEnums()
    {
        RecordingTransport transport;
        PopoutController controller(&transport);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("Popouts"), &controller);

        QQmlComponent component(&engine);
        component.setData(QStringLiteral(R"(
import QtQml
import Phosphor.Popout
QtObject {
    Component.onCompleted: Popouts.toggle({
        "popoutId": "power",
        "anchor": PhosphorPopout.Anchor.ScreenCenter,
        "exclusive": PhosphorPopout.ExclusiveMode.Modal,
        "keyboardFocus": true
    })
}
)")
                              .toUtf8(),
                          QUrl(QStringLiteral("qrc:/tst_popout_request.qml")));
        std::unique_ptr<QObject> object(component.create());
        QVERIFY2(object != nullptr, qPrintable(component.errorString()));

        QCOMPARE(transport.requests.size(), 1);
        QCOMPARE(transport.requests.first().popoutId, QStringLiteral("power"));
        QCOMPARE(transport.requests.first().anchor, Anchor::ScreenCenter);
        QCOMPARE(transport.requests.first().exclusive, ExclusiveMode::Modal);
        QVERIFY(transport.requests.first().keyboardFocus);
    }
};

QTEST_MAIN(TestPopoutQmlEnums)
#include "test_popout_qml_enums.moc"
