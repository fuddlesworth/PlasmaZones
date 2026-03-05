// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layoutmanager_assignment.cpp
 * @brief Unit tests for LayoutManager fallback cascade, default layout, quick slots
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
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutManagerAssignment : public QObject
{
    Q_OBJECT

private:
    Layout* createTestLayout(const QString& name, QObject* parent = nullptr)
    {
        auto* layout = new Layout(name, LayoutType::Custom, parent);
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
    // P1: layoutForScreen fallback cascade
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_layoutForScreen_fallbackCascade()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* defaultLayout = createTestLayout(QStringLiteral("Default"));
        mgr->addLayout(defaultLayout);

        auto* screenLayout = createTestLayout(QStringLiteral("ScreenSpecific"));
        mgr->addLayout(screenLayout);

        auto* desktopLayout = createTestLayout(QStringLiteral("DesktopSpecific"));
        mgr->addLayout(desktopLayout);

        mgr->assignLayout(QStringLiteral("DP-1"), 0, QString(), screenLayout);
        mgr->assignLayout(QStringLiteral("DP-1"), 2, QString(), desktopLayout);

        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 2)->name(), QStringLiteral("DesktopSpecific"));
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("DP-1"), 1)->name(), QStringLiteral("ScreenSpecific"));

        Layout* fallback = mgr->layoutForScreen(QStringLiteral("HDMI-1"));
        QVERIFY(fallback != nullptr);
        QCOMPARE(fallback->name(), QStringLiteral("Default"));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P2: Quick layout slots
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_quickLayoutSlot_validRange_1to9()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Quick"));
        mgr->addLayout(layout);

        QString layoutId = layout->id().toString();

        mgr->setQuickLayoutSlot(1, layoutId);
        QVERIFY(mgr->quickLayoutSlots().contains(1));

        mgr->setQuickLayoutSlot(9, layoutId);
        QVERIFY(mgr->quickLayoutSlots().contains(9));

        mgr->setQuickLayoutSlot(0, layoutId);
        QVERIFY(!mgr->quickLayoutSlots().contains(0));

        mgr->setQuickLayoutSlot(10, layoutId);
        QVERIFY(!mgr->quickLayoutSlots().contains(10));

        mgr->setQuickLayoutSlot(1, QString());
        QVERIFY(!mgr->quickLayoutSlots().contains(1));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P2: Default layout resolution
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_defaultLayout_settingsIdTakesPrecedence()
    {
        QScopedPointer<LayoutManager> mgr(createManager());
        auto* settings = new StubSettings(mgr.data());
        mgr->setSettings(settings);

        auto* first = createTestLayout(QStringLiteral("First"));
        mgr->addLayout(first);

        auto* second = createTestLayout(QStringLiteral("Second"));
        mgr->addLayout(second);

        QCOMPARE(mgr->defaultLayout()->name(), QStringLiteral("First"));

        settings->setTestDefaultLayoutId(second->id().toString());
        QCOMPARE(mgr->defaultLayout()->name(), QStringLiteral("Second"));
    }

    void testLayoutManager_defaultLayout_fallbackToFirstLayout()
    {
        QScopedPointer<LayoutManager> mgr(createManager());
        auto* settings = new StubSettings(mgr.data());
        mgr->setSettings(settings);

        auto* layout = createTestLayout(QStringLiteral("OnlyLayout"));
        mgr->addLayout(layout);

        settings->setTestDefaultLayoutId(QUuid::createUuid().toString());
        QCOMPARE(mgr->defaultLayout()->name(), QStringLiteral("OnlyLayout"));

        settings->setTestDefaultLayoutId(QString());
        QCOMPARE(mgr->defaultLayout()->name(), QStringLiteral("OnlyLayout"));
    }
};

QTEST_MAIN(TestLayoutManagerAssignment)
#include "test_layoutmanager_assignment.moc"
