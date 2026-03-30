// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layoutmanager_cycling.cpp
 * @brief Unit tests for LayoutManager cycle behavior
 */

#include <QTest>
#include <QDir>
#include <QScopedPointer>
#include <QUuid>
#include <memory>
#include <vector>

#include "core/layoutmanager.h"
#include "core/layout.h"
#include "core/zone.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutManagerCycling : public QObject
{
    Q_OBJECT

private:
    Layout* createTestLayout(const QString& name, QObject* parent = nullptr)
    {
        auto* layout = new Layout(name, parent);
        auto* zone = new Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        layout->addZone(zone);
        return layout;
    }

    LayoutManager* createManager(QObject* parent = nullptr)
    {
        m_guards.emplace_back(std::make_unique<IsolatedConfigGuard>());
        auto* mgr = new LayoutManager(parent);
        QString layoutDir = m_guards.back()->dataPath() + QStringLiteral("/plasmazones/layouts");
        QDir().mkpath(layoutDir);
        mgr->setLayoutDirectory(layoutDir);
        return mgr;
    }

    std::vector<std::unique_ptr<IsolatedConfigGuard>> m_guards;

private Q_SLOTS:

    void cleanup()
    {
        m_guards.clear();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: Layout cycling
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_cycleLayout_skipsHiddenLayouts()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* visible1 = createTestLayout(QStringLiteral("Visible1"));
        mgr->addLayout(visible1);

        auto* hidden = createTestLayout(QStringLiteral("Hidden"));
        hidden->setHiddenFromSelector(true);
        mgr->addLayout(hidden);

        auto* visible2 = createTestLayout(QStringLiteral("Visible2"));
        mgr->addLayout(visible2);

        mgr->setActiveLayout(visible1);
        mgr->setCurrentVirtualDesktop(0);
        mgr->assignLayout(QStringLiteral("screen1"), 0, QString(), visible1);

        mgr->cycleToNextLayout(QStringLiteral("screen1"));
        Layout* current = mgr->layoutForScreen(QStringLiteral("screen1"));
        QVERIFY(current != nullptr);
        QVERIFY(current != hidden);
        QCOMPARE(current->name(), QStringLiteral("Visible2"));
    }

    void testLayoutManager_cycleLayout_respectsAllowedScreens()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* layoutAll = createTestLayout(QStringLiteral("AllScreens"));
        mgr->addLayout(layoutAll);

        auto* layoutDP1Only = createTestLayout(QStringLiteral("DP1Only"));
        layoutDP1Only->setAllowedScreens({QStringLiteral("DP-1")});
        mgr->addLayout(layoutDP1Only);

        mgr->setActiveLayout(layoutAll);
        mgr->assignLayout(QStringLiteral("HDMI-1"), 0, QString(), layoutAll);

        mgr->cycleToNextLayout(QStringLiteral("HDMI-1"));
        Layout* current = mgr->layoutForScreen(QStringLiteral("HDMI-1"));
        QCOMPARE(current->name(), QStringLiteral("AllScreens"));
    }

    void testLayoutManager_cycleLayout_allFilteredOut_returnsNull()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* hidden = createTestLayout(QStringLiteral("Hidden"));
        hidden->setHiddenFromSelector(true);
        mgr->addLayout(hidden);

        mgr->cycleToNextLayout(QStringLiteral("screen1"));
        QCOMPARE(mgr->layoutCount(), 1);
    }

    void testLayoutManager_cycleLayout_wrapsAround()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* l1 = createTestLayout(QStringLiteral("L1"));
        mgr->addLayout(l1);

        auto* l2 = createTestLayout(QStringLiteral("L2"));
        mgr->addLayout(l2);

        auto* l3 = createTestLayout(QStringLiteral("L3"));
        mgr->addLayout(l3);

        mgr->setCurrentVirtualDesktop(0);

        mgr->assignLayout(QStringLiteral("screen1"), 0, QString(), l3);
        mgr->setActiveLayout(l3);

        mgr->cycleToNextLayout(QStringLiteral("screen1"));
        Layout* current = mgr->layoutForScreen(QStringLiteral("screen1"));
        QCOMPARE(current->name(), QStringLiteral("L1"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: Active/previous layout tracking
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_setActiveLayout_updatesPreviousLayout()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* l1 = createTestLayout(QStringLiteral("First"));
        mgr->addLayout(l1);

        auto* l2 = createTestLayout(QStringLiteral("Second"));
        mgr->addLayout(l2);

        mgr->setActiveLayout(l1);
        QCOMPARE(mgr->previousLayout(), l1);

        mgr->setActiveLayout(l2);
        QCOMPARE(mgr->activeLayout(), l2);
        QCOMPARE(mgr->previousLayout(), l1);
    }
};

QTEST_MAIN(TestLayoutManagerCycling)
#include "test_layoutmanager_cycling.moc"
