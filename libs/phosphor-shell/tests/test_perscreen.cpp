// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

// Tests for libs/phosphor-shell/qml/Phosphor/Shell/PerScreen.qml.
// The key invariants are about delegate identity surviving ScreenModel's
// beginResetModel/endResetModel hot-plug semantics — a naive
// `Instantiator { model: screens }` would tear down every delegate on
// every reset, defeating per-monitor wallpaper / overlay / panel
// consumers. The tests assert delegate QPointers stay alive across
// hot-plug add, hot-plug remove, and primary-screen swap operations.

#include "perscreen_fakemodel.h"

#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QJSValue>
#include <QJSValueList>
#include <QObject>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QString>
#include <QTest>
#include <QUrl>
#include <QVariant>
#include <QtCore/qtclasshelpermacros.h>

using PhosphorShellTests::FakeScreenModel;

class TestPerScreen : public QObject
{
    Q_OBJECT
public:
    Q_DISABLE_COPY_MOVE(TestPerScreen)
    TestPerScreen() = default;

private Q_SLOTS:
    void initialPopulation_createsOneDelegatePerRow();
    void hotplugAdd_createsOnlyTheNewDelegate();
    void hotplugRemove_destroysOnlyThatDelegate();
    void modelReset_survivingScreens_preserveDelegateIdentity();
    void primarySwap_doesNotRecreateDelegates();
    void modelReassign_teardownAllAndRebuild();
    void delegatesReceiveScreenNameIndexIsPrimaryRequiredProps();

private:
    // Build a PerScreen instance with a minimal delegate that records
    // the values it was constructed with. Returned object is parented
    // to `parent`; the engine outlives both via the same parent.
    QObject* makePerScreen(QQmlEngine& engine, QObject* parent, FakeScreenModel* model);

    // Build a delegate component whose root is a QtObject exposing
    // the four required properties PerScreen injects.
    QQmlComponent* makeDelegate(QQmlEngine& engine, QObject* parent);

    // Look up the delegate currently bound to `screen` inside the
    // PerScreen's JS Map. C++ can't introspect a JS Map directly, so
    // we go through the engine's JS interpreter to call Map.prototype.get.
    // Used by every test that asserts on delegate identity / required
    // properties — extracted to avoid duplicating the toScriptValue /
    // callWithInstance dance in three places.
    QObject* delegateFor(QQmlEngine& engine, QObject* perScreen, QObject* screen);
};

namespace {

// Inline QML for the delegate. QtObject is non-visual so the tests
// don't need a Window or scene graph; QPointer is used to track
// identity across model operations.
constexpr const char* kDelegateQml = R"(
    import QtQml
    QtObject {
        required property var phosphorScreen
        required property string name
        required property int index
        required property bool isPrimary
    }
)";

// Inline QML for the PerScreen host. We don't import Phosphor.Shell to
// avoid pulling in the static plugin registration from the test binary;
// PerScreen.qml is loaded directly via path. The test sets `model` and
// `delegate` after instantiation.
constexpr const char* kHostQml = R"(
    import QtQuick
    import "%1" as Shell
    Item {
        property alias perScreen: ps
        Shell.PerScreen { id: ps }
    }
)";

QQmlComponent* loadHost(QQmlEngine& engine, QObject* parent)
{
    // Resolve the source-dir PerScreen.qml; the build doesn't have to
    // install it for the test to find it. CMake sets PERSCREEN_QML_DIR
    // via target_compile_definitions so the path doesn't drift if the
    // QML files relocate. QML's import statement rejects bare absolute
    // paths (Qt 6.8+: "is not a valid import URL"), so wrap as a file:
    // URI which it accepts.
    const QUrl pathUrl = QUrl::fromLocalFile(QStringLiteral(PERSCREEN_QML_DIR));
    const QString hostSrc = QString::fromUtf8(kHostQml).arg(pathUrl.toString());
    auto* host = new QQmlComponent(&engine, parent);
    // The base URL must be a file: URL on disk so QML's import
    // resolution doesn't reject relative-path lookups. We don't
    // actually write the file; setData just uses the URL as the base
    // for resolving imports inside the QML source. An inline://
    // scheme is rejected by Qt 6.8+'s import resolver.
    const QString baseDir = QStringLiteral(PERSCREEN_QML_DIR);
    const QUrl baseUrl = QUrl::fromLocalFile(baseDir + QStringLiteral("/_test_host.qml"));
    host->setData(hostSrc.toUtf8(), baseUrl);
    if (host->status() != QQmlComponent::Ready) {
        qWarning() << "host setData status:" << host->status() << "errors:" << host->errorString();
    }
    return host;
}

} // namespace

QQmlComponent* TestPerScreen::makeDelegate(QQmlEngine& engine, QObject* parent)
{
    auto* delegate = new QQmlComponent(&engine, parent);
    // Base URL must be a real file: URL so QML's import resolution
    // doesn't reject it (Qt 6.8+); we never write the file — setData
    // uses it only as the base for relative imports inside the QML.
    const QString baseDir = QStringLiteral(PERSCREEN_QML_DIR);
    const QUrl baseUrl = QUrl::fromLocalFile(baseDir + QStringLiteral("/_test_delegate.qml"));
    delegate->setData(QByteArray(kDelegateQml), baseUrl);
    if (delegate->status() != QQmlComponent::Ready) {
        qWarning() << "delegate setData status:" << delegate->status() << "errors:" << delegate->errorString();
    }
    return delegate;
}

QObject* TestPerScreen::delegateFor(QQmlEngine& engine, QObject* perScreen, QObject* screen)
{
    // DELIBERATE white-box coupling: `_instances` is PerScreen.qml's private
    // screen->instance JS Map. The identity tests need to resolve "which
    // delegate instance belongs to which screen", and exposing a public
    // accessor for that would widen the component's API solely for tests.
    // If PerScreen.qml renames or restructures `_instances`, this helper is
    // the single place to update (every identity test funnels through it).
    QJSValue map = engine.toScriptValue(perScreen->property("_instances"));
    QJSValueList args{engine.toScriptValue(QVariant::fromValue(screen))};
    QJSValue getResult = map.property(QStringLiteral("get")).callWithInstance(map, args);
    return qvariant_cast<QObject*>(getResult.toVariant());
}

QObject* TestPerScreen::makePerScreen(QQmlEngine& engine, QObject* parent, FakeScreenModel* model)
{
    QQmlComponent* host = loadHost(engine, parent);
    if (host->isError()) {
        qWarning() << "host component errors:" << host->errors();
    }
    QObject* root = host->create();
    if (!root) {
        qWarning() << "host create errors:" << host->errors();
        return nullptr;
    }
    root->setParent(parent);
    QObject* perScreen = root->property("perScreen").value<QObject*>();
    if (!perScreen) {
        delete root;
        return nullptr;
    }
    QQmlComponent* delegate = makeDelegate(engine, parent);
    if (delegate->isError()) {
        qWarning() << "delegate component errors:" << delegate->errors();
    }
    // Order matters: set delegate BEFORE model so the first
    // _rebuild() (triggered by model assignment) has a delegate to
    // construct against. PerScreen also rebuilds in
    // Component.onCompleted, but the host has already completed by the
    // time we set properties here — the modelChanged path is what
    // populates.
    perScreen->setProperty("delegate", QVariant::fromValue(delegate));
    perScreen->setProperty("model", QVariant::fromValue(model));
    return perScreen;
}

void TestPerScreen::initialPopulation_createsOneDelegatePerRow()
{
    QQmlEngine engine;
    FakeScreenModel model;
    model.makeScreen(QStringLiteral("HDMI-1"), 1920, 1080, /*isPrimary=*/true);
    model.makeScreen(QStringLiteral("HDMI-2"));
    QObject parent;

    QObject* ps = makePerScreen(engine, &parent, &model);
    QVERIFY(ps != nullptr);
    QCOMPARE(ps->property("count").toInt(), 2);
}

void TestPerScreen::hotplugAdd_createsOnlyTheNewDelegate()
{
    // Count-only check: 2 → 3 on hot-plug add. The crown-jewel
    // identity-preservation test
    // (`modelReset_survivingScreens_preserveDelegateIdentity`)
    // covers the "existing delegates aren't recreated" path against
    // the same operation; this test pins the simpler invariant.
    QQmlEngine engine;
    FakeScreenModel model;
    model.makeScreen(QStringLiteral("HDMI-1"), 1920, 1080, true);
    model.makeScreen(QStringLiteral("HDMI-2"));
    QObject parent;

    QObject* ps = makePerScreen(engine, &parent, &model);
    QVERIFY(ps != nullptr);
    QCOMPARE(ps->property("count").toInt(), 2);

    model.addScreen(QStringLiteral("HDMI-3"));
    QCOMPARE(ps->property("count").toInt(), 3);
}

void TestPerScreen::hotplugRemove_destroysOnlyThatDelegate()
{
    QQmlEngine engine;
    FakeScreenModel model;
    model.makeScreen(QStringLiteral("HDMI-1"), 1920, 1080, true);
    QObject* s2 = model.makeScreen(QStringLiteral("HDMI-2"));
    model.makeScreen(QStringLiteral("HDMI-3"));
    QObject parent;

    QObject* ps = makePerScreen(engine, &parent, &model);
    QVERIFY(ps != nullptr);
    QCOMPARE(ps->property("count").toInt(), 3);

    model.removeScreen(s2);
    QCOMPARE(ps->property("count").toInt(), 2);
}

void TestPerScreen::modelReset_survivingScreens_preserveDelegateIdentity()
{
    // The crown-jewel invariant: a hot-plug reset that adds OR removes
    // screens MUST leave the delegate QObjects bound to surviving
    // screens with the same identity. This is what makes per-monitor
    // wallpaper / overlay state survive hot-plug without flicker.
    QQmlEngine engine;
    FakeScreenModel model;
    QObject* s1 = model.makeScreen(QStringLiteral("HDMI-1"), 1920, 1080, true);
    QObject* s2 = model.makeScreen(QStringLiteral("HDMI-2"));
    QObject parent;

    QObject* ps = makePerScreen(engine, &parent, &model);
    QVERIFY(ps != nullptr);
    QCOMPARE(ps->property("count").toInt(), 2);

    QPointer<QObject> d1Before = delegateFor(engine, ps, s1);
    QPointer<QObject> d2Before = delegateFor(engine, ps, s2);
    QVERIFY(!d1Before.isNull());
    QVERIFY(!d2Before.isNull());

    // Plug in a third screen — full model reset.
    QObject* s3 = model.addScreen(QStringLiteral("HDMI-3"));
    QCOMPARE(ps->property("count").toInt(), 3);

    QPointer<QObject> d1After = delegateFor(engine, ps, s1);
    QPointer<QObject> d2After = delegateFor(engine, ps, s2);
    QPointer<QObject> d3After = delegateFor(engine, ps, s3);

    // Surviving delegates MUST be the same QObjects as before.
    QCOMPARE(d1After.data(), d1Before.data());
    QCOMPARE(d2After.data(), d2Before.data());
    QVERIFY(!d3After.isNull());
    QVERIFY(d3After.data() != d1Before.data());
    QVERIFY(d3After.data() != d2Before.data());

    // Unplug the middle screen — reset again.
    model.removeScreen(s2);
    QCOMPARE(ps->property("count").toInt(), 2);

    QPointer<QObject> d1Final = delegateFor(engine, ps, s1);
    QPointer<QObject> d3Final = delegateFor(engine, ps, s3);
    QCOMPARE(d1Final.data(), d1Before.data());
    QCOMPARE(d3Final.data(), d3After.data());
    // d2's delegate is destroyed (deleteLater runs asynchronously, so
    // QPointer might not have nulled yet — process pending events).
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(d2Before.isNull());
}

void TestPerScreen::primarySwap_doesNotRecreateDelegates()
{
    QQmlEngine engine;
    FakeScreenModel model;
    QObject* s1 = model.makeScreen(QStringLiteral("HDMI-1"), 1920, 1080, /*isPrimary=*/true);
    QObject* s2 = model.makeScreen(QStringLiteral("HDMI-2"));
    QObject parent;

    QObject* ps = makePerScreen(engine, &parent, &model);
    QVERIFY(ps != nullptr);

    QPointer<QObject> d1Before = delegateFor(engine, ps, s1);
    QPointer<QObject> d2Before = delegateFor(engine, ps, s2);
    QVERIFY(!d1Before.isNull());
    QVERIFY(!d2Before.isNull());
    QCOMPARE(d1Before->property("isPrimary").toBool(), true);
    QCOMPARE(d2Before->property("isPrimary").toBool(), false);

    // Primary swap is signalled via dataChanged with IsPrimaryRole.
    // PerScreen's onDataChanged updates the existing delegates in
    // place; identity must be preserved.
    model.setPrimary(s2);

    QPointer<QObject> d1After = delegateFor(engine, ps, s1);
    QPointer<QObject> d2After = delegateFor(engine, ps, s2);
    QCOMPARE(d1After.data(), d1Before.data());
    QCOMPARE(d2After.data(), d2Before.data());
    QCOMPARE(d1After->property("isPrimary").toBool(), false);
    QCOMPARE(d2After->property("isPrimary").toBool(), true);
}

void TestPerScreen::modelReassign_teardownAllAndRebuild()
{
    QQmlEngine engine;
    FakeScreenModel modelA;
    modelA.makeScreen(QStringLiteral("A-1"));
    modelA.makeScreen(QStringLiteral("A-2"));
    QObject parent;

    QObject* ps = makePerScreen(engine, &parent, &modelA);
    QVERIFY(ps != nullptr);
    QCOMPARE(ps->property("count").toInt(), 2);

    FakeScreenModel modelB;
    modelB.makeScreen(QStringLiteral("B-1"));
    modelB.makeScreen(QStringLiteral("B-2"));
    modelB.makeScreen(QStringLiteral("B-3"));

    ps->setProperty("model", QVariant::fromValue(&modelB));
    QCOMPARE(ps->property("count").toInt(), 3);
}

void TestPerScreen::delegatesReceiveScreenNameIndexIsPrimaryRequiredProps()
{
    QQmlEngine engine;
    FakeScreenModel model;
    QObject* s1 = model.makeScreen(QStringLiteral("HDMI-1"), 1920, 1080, /*isPrimary=*/true);
    QObject* s2 = model.makeScreen(QStringLiteral("HDMI-2"), 2560, 1440, /*isPrimary=*/false);
    QObject parent;

    QObject* ps = makePerScreen(engine, &parent, &model);
    QVERIFY(ps != nullptr);

    QObject* d1 = delegateFor(engine, ps, s1);
    QObject* d2 = delegateFor(engine, ps, s2);
    QVERIFY(d1 != nullptr);
    QVERIFY(d2 != nullptr);

    // The four required properties from the contract.
    QCOMPARE(d1->property("phosphorScreen").value<QObject*>(), s1);
    QCOMPARE(d1->property("name").toString(), QStringLiteral("HDMI-1"));
    QCOMPARE(d1->property("index").toInt(), 0);
    QCOMPARE(d1->property("isPrimary").toBool(), true);

    QCOMPARE(d2->property("phosphorScreen").value<QObject*>(), s2);
    QCOMPARE(d2->property("name").toString(), QStringLiteral("HDMI-2"));
    QCOMPARE(d2->property("index").toInt(), 1);
    QCOMPARE(d2->property("isPrimary").toBool(), false);
}

QTEST_MAIN(TestPerScreen)
#include "test_perscreen.moc"
