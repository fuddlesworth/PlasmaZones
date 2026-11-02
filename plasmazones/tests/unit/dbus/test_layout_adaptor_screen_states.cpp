// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layout_adaptor_screen_states.cpp
 * @brief LayoutAdaptor::getScreenStates' tiledWindowCount key, and the
 *        screenStatesChanged signal that announces a changed answer.
 *
 * The settings app evaluates a rule's conditions against each screen's live
 * state, and a TiledWindowCount condition needs the count the daemon judges it
 * against. The key carries that count while a tiling engine runs on the screen
 * and is ABSENT otherwise, never zero: the daemon leaves the condition
 * unanswered on a screen nothing tiles, and a reader taking 0 there would match
 * a `TiledWindowCount LessThan 2` rule on every snapping screen. Driven against
 * a real ScreenManager over FakePhysicalScreenSource, because getScreenStates
 * walks the effective screen ids.
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCoreApplication>
#include <QRect>
#include <QSignalSpy>
#include <QString>
#include <QTest>

#include <memory>
#include <optional>

#include <PhosphorScreens/Manager.h>
#include <PhosphorZones/LayoutRegistry.h>

#include "FakePhysicalScreenSource.h"
#include "dbus/layoutadaptor/layoutadaptor.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutAdaptorScreenStates : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_guard = std::make_unique<IsolatedConfigGuard>();
        m_parent = new QObject(nullptr);
        m_layoutManager = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), m_parent);
        m_source = std::make_unique<PhosphorScreens::FakePhysicalScreenSource>();
        m_source->addScreen(QStringLiteral("DP-1"), QRect(0, 0, 1920, 1080), QStringLiteral("DP-1"));
        m_source->addScreen(QStringLiteral("DP-2"), QRect(1920, 0, 1920, 1080), QStringLiteral("DP-2"));
        m_screenManager = std::make_unique<PhosphorScreens::ScreenManager>(
            PhosphorScreens::ScreenManagerConfig{.physicalScreenSource = m_source.get(), .useGeometrySensors = false});
        m_screenManager->start();
        m_adaptor = new LayoutAdaptor(m_layoutManager, nullptr, m_screenManager.get(), m_parent);
    }

    void cleanup()
    {
        delete m_parent;
        m_parent = nullptr;
        m_layoutManager = nullptr;
        m_adaptor = nullptr;
        m_screenManager.reset();
        m_source.reset();
        m_guard.reset();
    }

    // DP-1 tiles three windows, DP-2 runs no tiling engine.
    void testTiledWindowCount_presentOnlyWhereATilingEngineRuns()
    {
        m_adaptor->setTiledWindowCountProvider([](const QString& screenId) -> std::optional<int> {
            return screenId == QLatin1String("DP-1") ? std::optional<int>(3) : std::nullopt;
        });

        const QJsonObject dp1 = stateFor(QStringLiteral("DP-1"));
        const QJsonObject dp2 = stateFor(QStringLiteral("DP-2"));
        QVERIFY2(!dp1.isEmpty() && !dp2.isEmpty(), "both screens must be in the reply");
        QCOMPARE(dp1.value(QLatin1String("tiledWindowCount")).toInt(-1), 3);
        QVERIFY2(!dp2.contains(QLatin1String("tiledWindowCount")),
                 "a screen no tiling engine runs on must carry no count, not a zero");
    }

    // A zero count is a real answer on a tiled screen with no windows yet, and
    // has to survive as one.
    void testTiledWindowCount_zeroOnATiledScreenIsKept()
    {
        m_adaptor->setTiledWindowCountProvider([](const QString&) -> std::optional<int> {
            return 0;
        });
        const QJsonObject dp1 = stateFor(QStringLiteral("DP-1"));
        QVERIFY(dp1.contains(QLatin1String("tiledWindowCount")));
        QCOMPARE(dp1.value(QLatin1String("tiledWindowCount")).toInt(-1), 0);
    }

    // No provider wired (an adaptor the daemon has not wired yet, or one
    // whose provider Daemon::stop() cleared): the key is never written.
    void testTiledWindowCount_absentWithoutAProvider()
    {
        QVERIFY(!stateFor(QStringLiteral("DP-1")).contains(QLatin1String("tiledWindowCount")));
    }

    // screenStatesChanged fires once per real change of the answer: a burst of
    // checks in one turn compares once, a check that finds the answer as it
    // was stays quiet, and a field no other signal reports (here the tiled
    // count, which moves no assignment) is announced.
    void testScreenStatesChanged_firesOncePerRealChange()
    {
        int count = 3;
        m_adaptor->setTiledWindowCountProvider([&count](const QString& screenId) -> std::optional<int> {
            return screenId == QLatin1String("DP-1") ? std::optional<int>(count) : std::nullopt;
        });
        QSignalSpy spy(m_adaptor, &LayoutAdaptor::screenStatesChanged);

        // The first comparison has nothing before it, so it announces.
        m_adaptor->scheduleScreenStatesCheck();
        m_adaptor->scheduleScreenStatesCheck();
        QVERIFY(spy.wait());
        QCoreApplication::processEvents();
        QCOMPARE(spy.count(), 1);

        m_adaptor->scheduleScreenStatesCheck();
        QVERIFY(!spy.wait(50));
        QCOMPARE(spy.count(), 1);

        count = 4;
        m_adaptor->scheduleScreenStatesCheck();
        QVERIFY(spy.wait());
        QCOMPARE(spy.count(), 2);
    }

private:
    QJsonObject stateFor(const QString& screenId) const
    {
        const QJsonArray states = QJsonDocument::fromJson(m_adaptor->getScreenStates().toUtf8()).array();
        for (const QJsonValue& value : states) {
            const QJsonObject state = value.toObject();
            if (state.value(QLatin1String("screenId")).toString() == screenId) {
                return state;
            }
        }
        return {};
    }

    std::unique_ptr<IsolatedConfigGuard> m_guard;
    QObject* m_parent = nullptr;
    PhosphorZones::LayoutRegistry* m_layoutManager = nullptr;
    std::unique_ptr<PhosphorScreens::FakePhysicalScreenSource> m_source;
    std::unique_ptr<PhosphorScreens::ScreenManager> m_screenManager;
    LayoutAdaptor* m_adaptor = nullptr;
};

QTEST_MAIN(TestLayoutAdaptorScreenStates)
#include "test_layout_adaptor_screen_states.moc"
