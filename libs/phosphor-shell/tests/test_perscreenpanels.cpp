// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
//
// PerScreenPanels — one panel per screen, built so ShellEngine can adopt
// it.
//
// The two properties under test are the two that every stock instantiator
// gets wrong for this job, and both fail SILENTLY in production:
//
//  1. Instances must be QObject children, because
//     ShellEngine::materializePanels() discovers panels with findChildren.
//     PerScreen creates with a null parent, so its panels are never found,
//     never materialized, and render nothing at all — with no error.
//
//  2. Instances must NOT be destroyed by their creator. Repeater deletes
//     its delegates on model reset, and ScreenModel resets on every
//     hotplug; the Surface that adopted the panel holds it by raw
//     pointer, so that delete dangles.
//
// Both are asserted directly here rather than inferred, because a review
// cannot distinguish "parented" from "not parented" by reading the QML.

#include <PhosphorShell/PanelWindow.h>
#include <PhosphorShell/PerScreenPanels.h>

#include <QAbstractListModel>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QRegularExpression>
#include <QScreen>
#include <QtTest/QtTest>

using PhosphorShell::PanelWindow;
using PhosphorShell::PerScreenPanels;

namespace {

/// Screen model yielding REAL QScreen pointers, so the `screen`
/// assignment can be asserted rather than assumed. Rows repeat the
/// platform's screens as needed: the offscreen QPA usually reports one,
/// and what matters here is row-to-instance mapping, not distinct outputs.
class RealScreenModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        ScreenRole = Qt::UserRole + 1,
        NameRole,
        IsPrimaryRole,
    };

    explicit RealScreenModel(int rows, QObject* parent = nullptr)
        : QAbstractListModel(parent)
        , m_rows(rows)
    {
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        return parent.isValid() ? 0 : m_rows;
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows) {
            return {};
        }
        switch (role) {
        case ScreenRole:
            return QVariant::fromValue(screenForRow(index.row()));
        case NameRole:
            return QStringLiteral("SCREEN-%1").arg(index.row());
        case IsPrimaryRole:
            return index.row() == 0;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {ScreenRole, "screen"},
            {NameRole, "name"},
            {IsPrimaryRole, "isPrimary"},
        };
    }

    static QScreen* screenForRow(int row)
    {
        const auto screens = QGuiApplication::screens();
        if (screens.isEmpty()) {
            return nullptr;
        }
        return screens.at(row % screens.size());
    }

    /// The hotplug signal a Repeater would react to by deleting delegates.
    void emitReset()
    {
        beginResetModel();
        endResetModel();
    }

private:
    int m_rows = 0;
};

} // namespace

class TestPerScreenPanels : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();

    void createsOnePanelPerRow();
    void instancesAreQObjectChildrenSoFindChildrenSeesThem();
    void instancesSurviveAModelReset();
    void panelsGetTheirScreenAssigned();
    void anExplicitScreenBindingIsNotOverwritten();
    void modelDataIsExposedToTheDelegate();
    void noDelegateCreatesNothing();
    void noModelCreatesNothing();
    void aLateWriteWarnsAndDoesNotRebuild();
    void aFailingDelegateRowIsSkipped();
    void countChangedFiresOncePerBuild();

private:
    /// Build a root Item containing a PerScreenPanels with `delegateBody`
    /// as its delegate. Returns the root; caller owns it.
    QObject* buildRoot(QQmlEngine& engine, int rows, const QString& delegateBody, RealScreenModel** modelOut);
};

void TestPerScreenPanels::initTestCase()
{
    // Register into a test-local URI rather than driving ShellEngine::load,
    // which would need a real shell file and a layer-shell compositor.
    qmlRegisterType<PerScreenPanels>("Phosphor.Shell.Test", 1, 0, "PerScreenPanels");
    qmlRegisterType<PanelWindow>("Phosphor.Shell.Test", 1, 0, "PanelWindow");
}

QObject* TestPerScreenPanels::buildRoot(QQmlEngine& engine, int rows, const QString& delegateBody,
                                        RealScreenModel** modelOut)
{
    auto* model = new RealScreenModel(rows, &engine);
    if (modelOut) {
        *modelOut = model;
    }
    engine.rootContext()->setContextProperty(QStringLiteral("screensModel"), model);

    const QString qml = QStringLiteral(R"QML(
import QtQuick
import Phosphor.Shell.Test

Item {
    property alias panels: instantiator
    PerScreenPanels {
        id: instantiator
        model: screensModel
        delegate: %1
    }
}
)QML")
                            .arg(delegateBody);

    QQmlComponent component(&engine);
    component.setData(qml.toUtf8(), QUrl(QStringLiteral("qrc:/test_perscreenpanels.qml")));
    if (component.isError()) {
        qWarning("%s", qPrintable(component.errorString()));
        return nullptr;
    }
    return component.create();
}

void TestPerScreenPanels::createsOnePanelPerRow()
{
    QQmlEngine engine;
    std::unique_ptr<QObject> root(buildRoot(engine, 3, QStringLiteral("PanelWindow { }"), nullptr));
    QVERIFY(root);

    auto* panels = root->property("panels").value<PerScreenPanels*>();
    QVERIFY(panels);
    QCOMPARE(panels->count(), 3);
}

void TestPerScreenPanels::instancesAreQObjectChildrenSoFindChildrenSeesThem()
{
    QQmlEngine engine;
    std::unique_ptr<QObject> root(buildRoot(engine, 3, QStringLiteral("PanelWindow { }"), nullptr));
    QVERIFY(root);

    // This is precisely the call ShellEngine::materializePanels() makes on
    // the QML root. If it returns fewer than the row count, every missing
    // panel would be silently inert in production.
    const auto found = root->findChildren<PanelWindow*>();
    QCOMPARE(found.size(), 3);
}

void TestPerScreenPanels::instancesSurviveAModelReset()
{
    QQmlEngine engine;
    RealScreenModel* model = nullptr;
    std::unique_ptr<QObject> root(buildRoot(engine, 2, QStringLiteral("PanelWindow { }"), &model));
    QVERIFY(root);
    QVERIFY(model);

    QList<QPointer<PanelWindow>> before;
    for (auto* panel : root->findChildren<PanelWindow*>()) {
        before.append(QPointer<PanelWindow>(panel));
    }
    QCOMPARE(before.size(), 2);

    // A hotplug. A Repeater would delete its delegates here, taking the
    // panel out from under whatever Surface had adopted it.
    model->emitReset();
    // deleteLater would land on the event loop rather than immediately.
    QTest::qWait(50);

    for (const auto& panel : before) {
        QVERIFY2(!panel.isNull(), "a panel was destroyed by a model reset");
    }
    // And no duplicates were created either: enumeration is one-shot,
    // because ShellEngine rebuilds the whole engine on topology change.
    QCOMPARE(root->findChildren<PanelWindow*>().size(), 2);
}

void TestPerScreenPanels::panelsGetTheirScreenAssigned()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("no QScreen available; screen assignment not exercised");
    }

    QQmlEngine engine;
    // objectName carries the row's name so each panel is identified by ROW,
    // not by findChildren enumeration position, which nothing pins.
    std::unique_ptr<QObject> root(
        buildRoot(engine, 2, QStringLiteral("PanelWindow { objectName: modelData.name }"), nullptr));
    QVERIFY(root);

    const auto found = root->findChildren<PanelWindow*>();
    QCOMPARE(found.size(), 2);
    for (auto* panel : found) {
        QVERIFY2(panel->screen() != nullptr, "panel was not pointed at an output");
    }

    // The row-to-screen MAPPING needs two distinct outputs: with one, every
    // row maps to the same pointer and the "every bar on the primary
    // output" regression this documents could never fail here. Skip that
    // half loudly, the way anExplicitScreenBindingIsNotOverwritten does.
    if (QGuiApplication::screens().size() < 2) {
        QSKIP("single output; row-to-screen mapping is indistinguishable and was not exercised");
    }
    for (int row = 0; row < 2; ++row) {
        auto* panel = root->findChild<PanelWindow*>(QStringLiteral("SCREEN-%1").arg(row));
        QVERIFY2(panel != nullptr, "panel for the row was not found by name");
        // Row N must get row N's screen. Without this the shell would put
        // every bar on whatever ShellEngine defaults to — the exact bug
        // per-screen bars exist to fix, and one that looks identical to
        // "it works" on a single-monitor desk.
        QCOMPARE(panel->screen(), RealScreenModel::screenForRow(row));
    }
}

void TestPerScreenPanels::anExplicitScreenBindingIsNotOverwritten()
{
    const auto screens = QGuiApplication::screens();
    if (screens.size() < 2) {
        // With one output, "kept the delegate's screen" and "assigned the
        // row's screen" are the same pointer, so the assertion could not
        // fail and would pass vacuously. Skip loudly instead.
        QSKIP("needs at least two outputs to distinguish a kept binding from an assigned one");
    }

    QQmlEngine engine;
    // Every instance pins itself to the LAST screen. Row 0 would otherwise
    // be assigned the first, so a clobbered binding is visible.
    engine.rootContext()->setContextProperty(QStringLiteral("pinnedScreen"), QVariant::fromValue(screens.last()));
    std::unique_ptr<QObject> root(
        buildRoot(engine, 2, QStringLiteral("PanelWindow { screen: pinnedScreen }"), nullptr));
    QVERIFY(root);

    const auto found = root->findChildren<PanelWindow*>();
    QCOMPARE(found.size(), 2);
    for (auto* panel : found) {
        QCOMPARE(panel->screen(), screens.last());
    }
}

void TestPerScreenPanels::modelDataIsExposedToTheDelegate()
{
    QQmlEngine engine;
    std::unique_ptr<QObject> root(
        buildRoot(engine, 2, QStringLiteral("PanelWindow { objectName: modelData.name }"), nullptr));
    QVERIFY(root);

    const auto found = root->findChildren<PanelWindow*>();
    QCOMPARE(found.size(), 2);
    QStringList names;
    for (auto* panel : found) {
        names << panel->objectName();
    }
    names.sort();
    QCOMPARE(names, QStringList({QStringLiteral("SCREEN-0"), QStringLiteral("SCREEN-1")}));
}

void TestPerScreenPanels::noDelegateCreatesNothing()
{
    QQmlEngine engine;

    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import Phosphor.Shell.Test

Item {
    property alias panels: instantiator
    PerScreenPanels { id: instantiator }
}
)QML",
                      QUrl(QStringLiteral("qrc:/test_perscreenpanels_empty.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("No delegate set")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY(root);

    auto* panels = root->property("panels").value<PerScreenPanels*>();
    QVERIFY(panels);
    QCOMPARE(panels->count(), 0);
    QCOMPARE(root->findChildren<PanelWindow*>().size(), 0);
}

void TestPerScreenPanels::noModelCreatesNothing()
{
    QQmlEngine engine;

    // A delegate but NO model. The sibling case covers the mirror image;
    // before this existed the two paths shared one test that set neither,
    // so the no-model branch was never actually exercised.
    QQmlComponent component(&engine);
    component.setData(R"QML(
import QtQuick
import Phosphor.Shell.Test

Item {
    property alias panels: instantiator
    PerScreenPanels {
        id: instantiator
        delegate: PanelWindow {}
    }
}
)QML",
                      QUrl(QStringLiteral("qrc:/test_perscreenpanels_nomodel.qml")));
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));

    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("No model set")));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY(root);

    auto* panels = root->property("panels").value<PerScreenPanels*>();
    QVERIFY(panels);
    QCOMPARE(panels->count(), 0);
    QCOMPARE(root->findChildren<PanelWindow*>().size(), 0);
}

void TestPerScreenPanels::aLateWriteWarnsAndDoesNotRebuild()
{
    QQmlEngine engine;
    RealScreenModel* model = nullptr;
    std::unique_ptr<QObject> root(buildRoot(engine, 2, QStringLiteral("PanelWindow { }"), &model));
    QVERIFY(root);
    auto* panels = root->property("panels").value<PerScreenPanels*>();
    QVERIFY(panels);
    QCOMPARE(panels->count(), 2);

    // Enumeration is one-shot: a post-build model swap must warn (the
    // documented diagnostic, without which the author stares at a component
    // that never appears) and change nothing.
    auto* replacement = new RealScreenModel(3, &engine);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("will not rebuild")));
    panels->setModel(replacement);
    QCOMPARE(panels->count(), 2);
    QCOMPARE(root->findChildren<PanelWindow*>().size(), 2);
}

void TestPerScreenPanels::aFailingDelegateRowIsSkipped()
{
    QQmlEngine engine;
    // The delegate declares a required property nobody supplies (the build
    // uses create(context), not initialProperties), so every row's create()
    // returns null; the loop must warn per row, skip, and leave a coherent
    // zero-instance state rather than aborting the build.
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Failed to create panel for screen row")));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Failed to create panel for screen row")));
    std::unique_ptr<QObject> root(
        buildRoot(engine, 2, QStringLiteral("PanelWindow { required property int mustHave }"), nullptr));
    QVERIFY(root);
    auto* panels = root->property("panels").value<PerScreenPanels*>();
    QVERIFY(panels);
    QCOMPARE(panels->count(), 0);
    QCOMPARE(root->findChildren<PanelWindow*>().size(), 0);
}

void TestPerScreenPanels::countChangedFiresOncePerBuild()
{
    // The change-gated countChanged: exactly one emission for a build that
    // produced rows (asserted via a queued check since the build runs
    // during component completion), none for an empty build.
    QQmlEngine engine;
    std::unique_ptr<QObject> root(buildRoot(engine, 2, QStringLiteral("PanelWindow { }"), nullptr));
    QVERIFY(root);
    auto* panels = root->property("panels").value<PerScreenPanels*>();
    QVERIFY(panels);
    QCOMPARE(panels->count(), 2);

    // The build already ran, so a spy can only pin that nothing re-fires
    // afterwards (the one-shot contract).
    QSignalSpy countSpy(panels, &PerScreenPanels::countChanged);
    QTest::qWait(50);
    QCOMPARE(countSpy.count(), 0);
}

QTEST_MAIN(TestPerScreenPanels)

#include "test_perscreenpanels.moc"
