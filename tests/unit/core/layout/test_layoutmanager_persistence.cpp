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
#include <PhosphorZones/ScrollingTemplate.h>
#include <PhosphorZones/ScrollingTemplateStore.h>
#include <PhosphorZones/Zone.h>
#include "helpers/StubSettings.h"
#include "helpers/IsolatedConfigGuard.h"
#include "helpers/LayoutRegistryTestHelpers.h"

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

        // The context rule SURVIVES the delete since the explicit no-layout
        // state exists: the purge rewrites the dead reference to the reserved
        // word rather than dropping the rule, so the screen keeps NO layout
        // instead of silently inheriting the registry-wide default (the same
        // verdict deleting a scrolling screen's template reaches — see the
        // delete-scrubs tests in test_layoutmanager_assignment.cpp). The
        // quick slot still sweeps: a stale binding must not resurrect the
        // deleted layout on a shortcut press.
        QVERIFY(mgr->hasExplicitAssignment(QStringLiteral("screen1")));
        const auto entry = mgr->assignmentEntryForScreen(QStringLiteral("screen1"), 0);
        QCOMPARE(entry.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(entry.snappingLayout, QString(PhosphorZones::NoSnappingLayout));
        QCOMPARE(mgr->layoutForScreen(QStringLiteral("screen1"), 0, QString()), nullptr);
        QVERIFY(!mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Snapping).contains(1));

        // The purge must also fire the per-context refresh signal: overlays
        // and the autotile derive re-read the cascade off layoutAssigned, so
        // a scrub that mutates the store silently leaves them on the deleted
        // layout until something unrelated nudges them.
        QSignalSpy purgeAssignedSpy(mgr.data(), &PhosphorZones::LayoutRegistry::layoutAssigned);
        auto* second = createTestLayout(QStringLiteral("AlsoAssigned"));
        mgr->addLayout(second);
        // A SURVIVING layout, so the fresh registry below has a non-null
        // defaultLayout(): without it the reloaded nullptr assertion passes
        // even when the sentinel never reached disk (an empty registry
        // answers nullptr for everything).
        auto* survivor = createTestLayout(QStringLiteral("Survivor"));
        mgr->addLayout(survivor);
        mgr->assignLayout(QStringLiteral("screen1"), 0, QString(), second);
        purgeAssignedSpy.clear();
        mgr->removeLayout(second);
        QVERIFY2(purgeAssignedSpy.count() >= 1, "the delete scrub must emit layoutAssigned for the affected context");

        // And the scrub must have reached DISK, not just the in-memory rule
        // set: a fresh registry over the same guard-isolated dirs reads the
        // reserved word back, so a daemon restart cannot resurrect the
        // deleted layout's context (the failure mode a save() swallowed by
        // setAllRules would produce). Same fresh-registry pattern the sidecar
        // tests below use.
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr2(
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        mgr2->setLayoutDirectory(mgr->layoutDirectory());
        mgr2->loadLayouts();
        mgr2->loadAssignments();
        const auto reloaded = mgr2->assignmentEntryForScreen(QStringLiteral("screen1"), 0);
        QCOMPARE(reloaded.mode, PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(reloaded.snappingLayout, QString(PhosphorZones::NoSnappingLayout));
        // Discriminating, thanks to Survivor above: the fresh registry HAS a
        // default layout, so nullptr here proves the on-disk sentinel is in
        // force rather than an empty registry answering nullptr for
        // everything.
        QVERIFY(mgr2->layoutCount() >= 1);
        QCOMPARE(mgr2->layoutForScreen(QStringLiteral("screen1"), 0, QString()), nullptr);
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
    // ({ "snapping": {...}, "autotile": {...}, "scrolling": {...} }). All
    // three modes must survive a save → fresh-load round trip and stay
    // independent.
    void testLayoutManager_quickLayouts_nestedFormatRoundTrip()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());

        auto* layout = createTestLayout(QStringLiteral("RoundTrip"));
        mgr->addLayout(layout);
        const QString uuid = layout->id().toString();
        const auto snapping = PhosphorZones::AssignmentEntry::Snapping;
        const auto autotile = PhosphorZones::AssignmentEntry::Autotile;
        const auto scrolling = PhosphorZones::AssignmentEntry::Scrolling;

        mgr->setQuickLayoutSlot(snapping, 1, uuid); // each set writes the sidecar
        mgr->setQuickLayoutSlot(autotile, 2, QStringLiteral("autotile:bsp"));
        // Scrolling owns its OWN array and on-disk key since the
        // native-template pivot; its slot values are template ids.
        PhosphorZones::ScrollingTemplateStore store;
        mgr->setScrollingTemplateStore(&store);
        PhosphorZones::ScrollingTemplate slotTemplate;
        slotTemplate.name = QStringLiteral("SlotTemplate");
        slotTemplate.presetColumnWidths = {0.5};
        const QString templId = store.saveTemplate(slotTemplate).toString();
        mgr->setQuickLayoutSlot(scrolling, 3, templId);
        mgr->setScrollingTemplateStore(nullptr);

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
        // The scrolling-mode write round-trips through its OWN on-disk key
        // and never leaks into the snapping array.
        QCOMPARE(mgr2->quickLayoutSlots(scrolling).value(3), templId);
        QVERIFY(!mgr2->quickLayoutSlots(snapping).contains(3));
        QVERIFY(!mgr2->quickLayoutSlots(autotile).contains(3));
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

        const auto writeSidecar = [&path](const QJsonObject& document) {
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly)) {
                return false;
            }
            f.write(QJsonDocument(document).toJson());
            return true;
        };

        // POSITIVE CONTROL first: the same hand-computed path, holding a
        // document in the SUPPORTED nested shape, must load. Without this the
        // emptiness below would also be the result of writing to a path the
        // registry never reads, and the test would pass for the wrong reason.
        QJsonObject nestedSlots;
        nestedSlots.insert(QStringLiteral("1"), uuid);
        QJsonObject nested;
        nested.insert(PhosphorZones::LayoutRegistry::QuickSlotsSnappingKey, nestedSlots);
        QVERIFY(writeSidecar(nested));
        mgr->loadAssignments();
        QCOMPARE(mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Snapping).value(1), uuid);

        // Now the flat document, over the same path the control just proved is
        // live. The reader is nested-only, so every mode comes back empty.
        QJsonObject flat;
        flat.insert(QStringLiteral("1"), uuid);
        flat.insert(QStringLiteral("3"), uuid);
        QVERIFY(writeSidecar(flat));

        mgr->loadAssignments(); // re-reads the sidecar we just wrote

        QVERIFY(mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Snapping).isEmpty());
        QVERIFY(mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Autotile).isEmpty());
        // Scrolling owns a third array read from the same document, so it has
        // to be asserted too or a flat file could leak into it unnoticed.
        QVERIFY(mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Scrolling).isEmpty());
    }

    // setAllQuickLayoutSlots is the batch peer of setQuickLayoutSlot and had no
    // coverage at all. Its scrolling arm HAND-DUPLICATES the single setter's
    // canonicalization and validation rather than sharing it, so the two can
    // drift silently: an unbraced id stored verbatim would survive the delete
    // of the template it names (the purge sweep compares exact strings), and a
    // malformed or unknown id stored at all would leave a dangling slot.
    void testLayoutManager_setAllQuickLayoutSlots_validatesAndCanonicalizes()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        PhosphorZones::ScrollingTemplateStore store;
        mgr->setScrollingTemplateStore(&store);

        PhosphorZones::ScrollingTemplate templ;
        templ.name = QStringLiteral("Batch");
        templ.presetColumnWidths = {0.5};
        const QUuid templId = store.saveTemplate(templ);
        QVERIFY(!templId.isNull());

        // Slot 1 braced (accepted as is), slot 2 the SAME id unbraced and
        // upper-cased (must come back canonical), slot 3 a well-formed id no
        // template owns, slot 4 not a UUID at all. Only 1 and 2 survive.
        const QString unbraced = templId.toString(QUuid::WithoutBraces).toUpper();
        QHash<int, QString> batch;
        batch.insert(1, templId.toString());
        batch.insert(2, unbraced);
        batch.insert(3, QStringLiteral("{99999999-9999-9999-9999-999999999999}"));
        batch.insert(4, QStringLiteral("not-a-uuid"));
        mgr->setAllQuickLayoutSlots(PhosphorZones::AssignmentEntry::Scrolling, batch);

        const QHash<int, QString> stored = mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(stored.size(), 2);
        QCOMPARE(stored.value(1), templId.toString());
        QCOMPARE(stored.value(2), templId.toString());
        QVERIFY(!stored.contains(3));
        QVERIFY(!stored.contains(4));

        // The batch write goes through the sidecar like the single setter, so
        // a fresh registry on the same isolated dirs reads back the canonical
        // spellings rather than what the caller handed in.
        mgr->setScrollingTemplateStore(nullptr);
        QScopedPointer<PhosphorZones::LayoutRegistry> reloaded(
            PlasmaZones::TestHelpers::makeLayoutRegistry(QStringLiteral("plasmazones/layouts")));
        reloaded->loadAssignments();
        const QHash<int, QString> afterReload = reloaded->quickLayoutSlots(PhosphorZones::AssignmentEntry::Scrolling);
        QCOMPARE(afterReload.value(1), templId.toString());
        QCOMPARE(afterReload.value(2), templId.toString());
        QCOMPARE(afterReload.size(), 2);
    }

    // The batch setter CLEARS the mode's array before applying, and only that
    // mode's. A caller replacing the scrolling slots must not disturb the
    // snapping or autotile arrays sharing the sidecar.
    void testLayoutManager_setAllQuickLayoutSlots_clearsOnlyItsOwnMode()
    {
        QScopedPointer<PhosphorZones::LayoutRegistry> mgr(createManager());
        auto* layout = createTestLayout(QStringLiteral("Slotted"));
        mgr->addLayout(layout);
        const QString uuid = layout->id().toString();

        mgr->setQuickLayoutSlot(PhosphorZones::AssignmentEntry::Snapping, 1, uuid);
        mgr->setQuickLayoutSlot(PhosphorZones::AssignmentEntry::Autotile, 1, QStringLiteral("autotile:bsp"));
        mgr->setQuickLayoutSlot(PhosphorZones::AssignmentEntry::Snapping, 5, uuid);

        // Replacing the snapping array drops slot 5 (not named in the batch)
        // and leaves autotile untouched.
        QHash<int, QString> batch;
        batch.insert(2, uuid);
        mgr->setAllQuickLayoutSlots(PhosphorZones::AssignmentEntry::Snapping, batch);

        const QHash<int, QString> snapping = mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Snapping);
        QCOMPARE(snapping.size(), 1);
        QCOMPARE(snapping.value(2), uuid);
        QCOMPARE(mgr->quickLayoutSlots(PhosphorZones::AssignmentEntry::Autotile).value(1),
                 QStringLiteral("autotile:bsp"));
    }
};

QTEST_MAIN(TestLayoutManagerPersistence)
#include "test_layoutmanager_persistence.moc"
