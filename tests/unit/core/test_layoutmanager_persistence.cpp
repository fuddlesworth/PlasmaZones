// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_layoutmanager_persistence.cpp
 * @brief Unit tests for PhosphorZones::LayoutRegistry save/load, remove, add/duplicate
 */

#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedPointer>
#include <QColor>
#include <QUuid>
#include <memory>
#include <vector>

#include <PhosphorZones/LayoutRegistry.h>
#include "config/configbackends.h"
#include <PhosphorZones/Layout.h>
#include <PhosphorZones/Zone.h>
#include "../helpers/StubSettings.h"
#include "../helpers/IsolatedConfigGuard.h"
#include "../helpers/LayoutRegistryTestHelpers.h"

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

    PhosphorZones::LayoutRegistry* createManager(QObject* parent = nullptr)
    {
        m_guards.emplace_back(std::make_unique<IsolatedConfigGuard>());
        auto* mgr = PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts"), parent);
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
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

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
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("ToDelete"));
        mgr->addLayout(layout);

        QString filePath = mgr->layoutDirectory() + QStringLiteral("/") + layout->id().toString(QUuid::WithoutBraces)
            + QStringLiteral(".json");
        QVERIFY(QFile::exists(filePath));

        QSignalSpy removedSpy(mgr.data(), &PhosphorZones::LayoutRegistry::layoutRemoved);

        mgr->removeLayout(layout);

        QCOMPARE(removedSpy.count(), 1);
        QVERIFY(!QFile::exists(filePath));
        QCOMPARE(mgr->layoutCount(), 0);
    }

    void testLayoutManager_removeLayout_cleansAssignments()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

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
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

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
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* original = createTestLayout(QStringLiteral("Original"));
        mgr->addLayout(original);

        PhosphorZones::Layout* duplicate = mgr->duplicateLayout(original);
        QVERIFY(duplicate != nullptr);
        QVERIFY(duplicate->id() != original->id());
        QCOMPARE(duplicate->name(), QStringLiteral("Original (Copy)"));
        QCOMPARE(duplicate->zoneCount(), original->zoneCount());
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Per-layout settings split to sidecar on save, merged back on load
    // ═══════════════════════════════════════════════════════════════════════════

    void testLayoutManager_settings_splitOnSaveMergedOnLoad()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        const QString layoutDir = mgr->layoutDirectory();

        auto* layout = new PhosphorZones::Layout(QStringLiteral("WithSettings"));
        layout->setZonePadding(8);
        layout->setUseFullScreenGeometry(true);
        layout->setAutoAssign(true);
        auto* zone = new PhosphorZones::Zone();
        zone->setRelativeGeometry(QRectF(0, 0, 1, 1));
        zone->setUseCustomColors(true);
        zone->setHighlightColor(QColor(QStringLiteral("#ff112233")));
        layout->addZone(zone);
        const QUuid id = layout->id();
        mgr->addLayout(layout); // triggers save → split

        // The structural layout file is slim: no settings keys, no zone appearance.
        const QString filePath =
            layoutDir + QStringLiteral("/") + id.toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QFile lf(filePath);
        QVERIFY(lf.open(QIODevice::ReadOnly));
        const QJsonObject onDisk = QJsonDocument::fromJson(lf.readAll()).object();
        QVERIFY(!onDisk.contains(QStringLiteral("zonePadding")));
        QVERIFY(!onDisk.contains(QStringLiteral("useFullScreenGeometry")));
        QVERIFY(
            !onDisk.value(QStringLiteral("zones")).toArray().at(0).toObject().contains(QStringLiteral("appearance")));

        // A fresh registry on the SAME dirs (guard still alive) reloads the layout
        // and merges its settings back from the sidecar.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr2(
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        mgr2->setLayoutDirectory(layoutDir);
        mgr2->loadLayouts();

        PhosphorZones::Layout* reloaded = mgr2->layoutById(id);
        QVERIFY(reloaded != nullptr);
        // A genuinely fresh load from disk, not the in-memory object mgr still
        // holds — proves the merge ran against the on-disk sidecar.
        QVERIFY(reloaded != layout);
        QCOMPARE(reloaded->zonePadding(), 8);
        QVERIFY(reloaded->useFullScreenGeometry());
        QVERIFY(reloaded->autoAssign());
        QCOMPARE(reloaded->zones().size(), 1);
        QVERIFY(reloaded->zones().at(0)->useCustomColors());
        QCOMPARE(reloaded->zones().at(0)->highlightColor(), QColor(QStringLiteral("#ff112233")));
    }

    void testLayoutManager_duplicateLayout_resetsVisibility()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

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
