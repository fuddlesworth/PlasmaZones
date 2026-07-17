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
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopedPointer>
#include <QColor>
#include <QUuid>
#include <memory>
#include <utility>
#include <vector>

#include <unistd.h> // geteuid — the read-only-directory test is a no-op as root

#include <PhosphorZones/LayoutRegistry.h>
#include "config/configbackends.h"
#include "config/configdefaults.h"
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

    /// Strips write permission from a directory for the guard's lifetime, so a
    /// QSaveFile staged inside it fails to open. RAII because the tests below
    /// use QVERIFY between engage and release — an early abort must not leave a
    /// read-only directory behind for IsolatedConfigGuard's teardown to trip on.
    class ReadOnlyDirGuard
    {
    public:
        explicit ReadOnlyDirGuard(QString path)
            : m_path(std::move(path))
            , m_original(QFile::permissions(m_path))
        {
            m_engaged = QFile::setPermissions(m_path, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        }
        ~ReadOnlyDirGuard()
        {
            if (m_engaged) {
                QFile::setPermissions(m_path, m_original);
            }
        }
        ReadOnlyDirGuard(const ReadOnlyDirGuard&) = delete;
        ReadOnlyDirGuard& operator=(const ReadOnlyDirGuard&) = delete;

        bool engaged() const
        {
            return m_engaged;
        }

    private:
        QString m_path;
        QFileDevice::Permissions m_original;
        bool m_engaged = false;
    };

    static QByteArray readFile(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

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

        mgr->setQuickLayoutSlot(PhosphorZones::AssignmentEntry::Snapping, 1, layout->id().toString());

        mgr->removeLayout(layout);

        QVERIFY(!mgr->hasExplicitAssignment(QStringLiteral("screen1")));
        QVERIFY(!mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Snapping).contains(1));
    }

    // A failed sidecar write must abandon the whole removal rather than
    // half-apply it. The entry is dropped from memory only once disk agrees,
    // so a user whose sidecar is momentarily unwritable can simply retry.
    //
    // The bug this guards: the sidecar was persisted AFTER the layout file was
    // already unlinked, so a write failure only warned. Memory lost the entry
    // while disk kept it, and because nothing rewrites the sidecar in between,
    // the NEXT loadLayouts() merged the deleted override's settings straight
    // back onto the restored system layout — the exact inheritance the removal
    // exists to prevent.
    void testLayoutManager_removeLayout_abandonedWhenSidecarWriteFails()
    {
        if (::geteuid() == 0) {
            QSKIP("running as root — directory mode bits are ignored, so the sidecar write cannot be made to fail");
        }

        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("SidecarFail"));
        mgr->addLayout(layout);
        const QString layoutIdStr = layout->id().toString();

        // Give the layout a real sidecar entry — removeLayout deliberately skips
        // the write for a layout that has none, since dropping an absent entry
        // cannot change the file's bytes.
        layout->setShowZoneNumbers(!layout->showZoneNumbers());
        layout->markDirty();
        mgr->saveLayout(layout);

        const QString settingsPath =
            QFileInfo(ConfigDefaults::rulesFilePath()).absolutePath() + QStringLiteral("/layout-settings.json");
        QVERIFY(QFile::exists(settingsPath));
        const QByteArray sidecarBefore = readFile(settingsPath);
        QVERIFY(sidecarBefore.contains(layoutIdStr.toUtf8()));

        const QString filePath = mgr->layoutDirectory() + QStringLiteral("/")
            + layout->id().toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QVERIFY(QFile::exists(filePath));

        QSignalSpy removedSpy(mgr.data(), &PhosphorZones::LayoutRegistry::layoutRemoved);

        {
            // QSaveFile stages its temp file beside the target, so a read-only
            // parent directory is enough to make the commit fail.
            ReadOnlyDirGuard roGuard(QFileInfo(settingsPath).absolutePath());
            QVERIFY(roGuard.engaged());

            mgr->removeLayout(layout);

            // Nothing was destroyed: the layout is still registered and its file
            // is still on disk, so the removal can be retried.
            QCOMPARE(mgr->layoutCount(), 1);
            QCOMPARE(removedSpy.count(), 0);
            QVERIFY(QFile::exists(filePath));
        }

        // Memory and disk still agree — the sidecar is byte-for-byte unchanged,
        // and the in-memory entry that backs it was put back.
        QCOMPARE(readFile(settingsPath), sidecarBefore);

        // Retry now that the directory is writable again: the removal completes
        // and takes the sidecar entry with it.
        mgr->removeLayout(layout);
        QCOMPARE(mgr->layoutCount(), 0);
        QVERIFY(!QFile::exists(filePath));
        QVERIFY(!readFile(settingsPath).contains(layoutIdStr.toUtf8()));
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

    // ═══════════════════════════════════════════════════════════════════════════
    // Quick-layout slot persistence — mode-keyed quicklayouts.json
    // ═══════════════════════════════════════════════════════════════════════════

    // The current on-disk format nests slots per mode
    // ({ "snapping": {...}, "autotile": {...} }). Both modes must survive a
    // save → fresh-load round trip and stay independent.
    void testLayoutManager_quickLayouts_nestedFormatRoundTrip()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("RoundTrip"));
        mgr->addLayout(layout);
        const QString uuid = layout->id().toString();
        const auto snapping = PhosphorZones::AssignmentEntry::Snapping;
        const auto autotile = PhosphorZones::AssignmentEntry::Autotile;

        mgr->setQuickLayoutSlot(snapping, 1, uuid); // each set writes the sidecar
        mgr->setQuickLayoutSlot(autotile, 2, QStringLiteral("autotile:bsp"));

        // A fresh registry on the SAME guard-isolated dirs reloads the sidecar
        // (quicklayouts.json lives next to rules.json, not in the layout
        // dir, so no setLayoutDirectory is needed for quick-slot loading).
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr2(
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        mgr2->loadAssignments();

        QCOMPARE(mgr2->quickLayoutSlots(snapping).value(1), uuid);
        QCOMPARE(mgr2->quickLayoutSlots(autotile).value(2), QStringLiteral("autotile:bsp"));
        // Modes stay independent across the round trip.
        QVERIFY(!mgr2->quickLayoutSlots(snapping).contains(2));
        QVERIFY(!mgr2->quickLayoutSlots(autotile).contains(1));
    }

    // A pre-mode (flat) quicklayouts.json is NOT a supported format: the reader
    // is nested-only, so a flat file loads as empty (old bindings are dropped,
    // the user gets defaults). Guards against re-introducing a second read path.
    void testLayoutManager_quickLayouts_legacyFlatIgnored()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        const QString uuid = QUuid::createUuid().toString();
        const QString path = ConfigDefaults::quickLayoutsFilePath();
        QDir().mkpath(QFileInfo(path).absolutePath());
        QJsonObject flat;
        flat.insert(QStringLiteral("1"), uuid);
        flat.insert(QStringLiteral("3"), uuid);
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QJsonDocument(flat).toJson());
        }

        mgr->loadAssignments(); // re-reads the sidecar we just wrote

        QVERIFY(mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Snapping).isEmpty());
        QVERIFY(mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Autotile).isEmpty());
    }
};

QTEST_MAIN(TestLayoutManagerPersistence)
#include "test_layoutmanager_persistence.moc"
