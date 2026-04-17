// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layoutmanager_persistence.cpp
 * @brief Unit tests for LayoutManager save/load, remove, add/duplicate
 */

#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedPointer>
#include <QUuid>
#include <memory>
#include <vector>

#include "core/layoutmanager.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestLayoutManagerPersistence : public QObject
{
    Q_OBJECT

private:
    PhosphorZones::Layout* createTestLayout(const QString& name, QObject* parent = nullptr)
    {
        auto* layout = new PhosphorZones::Layout(name, parent);
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        layout->addZone(zone);
        return layout;
    }

    LayoutManager* createManager(QObject* parent = nullptr)
    {
        m_guards.emplace_back(std::make_unique<IsolatedConfigGuard>());
        auto* mgr = new LayoutManager(parent);
        // Override layout dir to a path under the guard's temp dir to avoid
        // static-cache issues in PhosphorZones::Layout::isSystemLayout().
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
    // P0: Save behavior
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_saveLayout_onlyWritesDirtyLayouts()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("SaveTest"));
        mgr->addLayout(layout);

        QString filePath = mgr->layoutDirectory() + QStringLiteral("/") + layout->id().toString(QUuid::WithoutBraces)
            + QStringLiteral(".json");
        QVERIFY(QFile::exists(filePath));

        QVERIFY(!layout->isDirty());

        QFile::remove(filePath);
        QVERIFY(!QFile::exists(filePath));

        mgr->saveLayout(layout);
        QVERIFY(!QFile::exists(filePath));

        layout->markDirty();
        mgr->saveLayout(layout);
        QVERIFY(QFile::exists(filePath));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P0: Remove layout
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_removeLayout_deletesFile()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("ToDelete"));
        mgr->addLayout(layout);

        QString filePath = mgr->layoutDirectory() + QStringLiteral("/") + layout->id().toString(QUuid::WithoutBraces)
            + QStringLiteral(".json");
        QVERIFY(QFile::exists(filePath));

        QSignalSpy removedSpy(mgr.data(), &LayoutManager::layoutRemoved);

        mgr->removeLayout(layout);

        QCOMPARE(removedSpy.count(), 1);
        QVERIFY(!QFile::exists(filePath));
        QCOMPARE(mgr->layoutCount(), 0);
    }

    void testLayoutManager_removeLayout_cleansAssignments()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("Assigned"));
        mgr->addLayout(layout);

        mgr->assignLayout(QStringLiteral("screen1"), 0, QString(), layout);
        QVERIFY(mgr->hasExplicitAssignment(QStringLiteral("screen1")));

        mgr->setQuickLayoutSlot(1, layout->id().toString());

        mgr->removeLayout(layout);

        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("screen1")));
        QVERIFY(!mgr->quickLayoutSlots().contains(1));
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // P1: addLayout / duplicateLayout
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_addLayout_connectsModifiedToSave()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("AutoSave"));
        mgr->addLayout(layout);

        QVERIFY(!layout->isDirty());

        layout->setName(QStringLiteral("Modified"));

        QVERIFY(!layout->isDirty());

        QString filePath = mgr->layoutDirectory() + QStringLiteral("/") + layout->id().toString(QUuid::WithoutBraces)
            + QStringLiteral(".json");
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        QCOMPARE(doc.object()[QStringLiteral("name")].toString(), QStringLiteral("Modified"));
    }

    void testLayoutManager_duplicateLayout_hasNewId()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* original = createTestLayout(QStringLiteral("Original"));
        mgr->addLayout(original);

        PhosphorZones::Layout* duplicate = mgr->duplicateLayout(original);
        QVERIFY(duplicate != nullptr);
        QVERIFY(duplicate->id() != original->id());
        QCOMPARE(duplicate->name(), QStringLiteral("Original (Copy)"));
        QCOMPARE(duplicate->zoneCount(), original->zoneCount());
    }

    void testLayoutManager_duplicateLayout_resetsVisibility()
    {
        QScopedPointer<LayoutManager> mgr(createManager());

        auto* original = createTestLayout(QStringLiteral("Restricted"));
        original->setHiddenFromSelector(true);
        original->setAllowedScreens({QStringLiteral("DP-1")});
        original->setAllowedDesktops({1, 2});
        original->setAllowedActivities({QStringLiteral("activity1")});
        mgr->addLayout(original);

        PhosphorZones::Layout* duplicate = mgr->duplicateLayout(original);
        QVERIFY(duplicate != nullptr);

        QVERIFY(!duplicate->hiddenFromSelector());
        QVERIFY(duplicate->allowedScreens().isEmpty());
        QVERIFY(duplicate->allowedDesktops().isEmpty());
        QVERIFY(duplicate->allowedActivities().isEmpty());
    }
};

QTEST_MAIN(TestLayoutManagerPersistence)
#include "test_layoutmanager_persistence.moc"
