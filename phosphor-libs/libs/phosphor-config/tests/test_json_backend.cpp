// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorConfig/IGroupPathResolver.h>
#include <PhosphorConfig/JsonBackend.h>
#include <PhosphorConfig/Schema.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <optional>

namespace PhosphorConfig::tests {

/// Resolver that captures every group name routing through it — exercises
/// the IGroupPathResolver plug-in hook without pulling in Phosphor.
class CapturingResolver : public IGroupPathResolver
{
public:
    std::optional<QStringList> toJsonPath(const QString& groupName) const override
    {
        if (groupName.startsWith(QStringLiteral("Screen:"))) {
            const QString id = groupName.mid(7);
            if (id.isEmpty()) {
                return QStringList{};
            }
            return QStringList{QStringLiteral("Screens"), id};
        }
        return std::nullopt;
    }

    QStringList reservedRootKeys() const override
    {
        return {QStringLiteral("Screens")};
    }

    QStringList enumerate(const QJsonObject& root) const override
    {
        QStringList out;
        const QJsonObject screens = root.value(QStringLiteral("Screens")).toObject();
        for (auto it = screens.constBegin(); it != screens.constEnd(); ++it) {
            out << QStringLiteral("Screen:") + it.key();
        }
        return out;
    }
};

class TestJsonBackend : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init()
    {
        m_tmp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_tmp->isValid());
        m_path = m_tmp->filePath(QStringLiteral("config.json"));
    }

    void cleanup()
    {
        m_tmp.reset();
    }

    void emptyFile_returnsDefaults()
    {
        JsonBackend b(m_path);
        auto g = b.group(QStringLiteral("NoSuchGroup"));
        QCOMPARE(g->readInt(QStringLiteral("missing"), 42), 42);
        QCOMPARE(g->readString(QStringLiteral("missing"), QStringLiteral("d")), QStringLiteral("d"));
        QCOMPARE(g->readBool(QStringLiteral("missing"), true), true);
        QCOMPARE(g->readDouble(QStringLiteral("missing"), 3.14), 3.14);
    }

    void roundTrip_flatGroup()
    {
        {
            JsonBackend b(m_path);
            auto g = b.group(QStringLiteral("Window"));
            g->writeInt(QStringLiteral("Width"), 800);
            g->writeBool(QStringLiteral("Maximized"), true);
            g->writeString(QStringLiteral("Title"), QStringLiteral("PhosphorConfig"));
            g.reset();
            b.sync();
        }

        JsonBackend b(m_path);
        auto g = b.group(QStringLiteral("Window"));
        QCOMPARE(g->readInt(QStringLiteral("Width"), 0), 800);
        QCOMPARE(g->readBool(QStringLiteral("Maximized"), false), true);
        QCOMPARE(g->readString(QStringLiteral("Title")), QStringLiteral("PhosphorConfig"));
    }

    void roundTrip_dotPathGroup()
    {
        {
            JsonBackend b(m_path);
            auto g = b.group(QStringLiteral("Snapping.Behavior.ZoneSpan"));
            g->writeBool(QStringLiteral("Enabled"), true);
            g->writeInt(QStringLiteral("Modifier"), 2);
            g.reset();
            b.sync();
        }

        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(root.contains(QStringLiteral("Snapping")));
        const QJsonObject zoneSpan = root.value(QStringLiteral("Snapping"))
                                         .toObject()
                                         .value(QStringLiteral("Behavior"))
                                         .toObject()
                                         .value(QStringLiteral("ZoneSpan"))
                                         .toObject();
        QCOMPARE(zoneSpan.value(QStringLiteral("Enabled")).toBool(), true);
        QCOMPARE(zoneSpan.value(QStringLiteral("Modifier")).toInt(), 2);
    }

    void resolver_routesGroupsToNestedPath()
    {
        auto resolver = std::make_shared<CapturingResolver>();

        {
            JsonBackend b(m_path);
            b.setPathResolver(resolver);
            auto g = b.group(QStringLiteral("Screen:DP-1"));
            g->writeInt(QStringLiteral("Width"), 2560);
            g.reset();
            b.sync();
        }

        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(root.contains(QStringLiteral("Screens")));
        QCOMPARE(root.value(QStringLiteral("Screens"))
                     .toObject()
                     .value(QStringLiteral("DP-1"))
                     .toObject()
                     .value(QStringLiteral("Width"))
                     .toInt(),
                 2560);
    }

    void resolver_malformedGroupIsDroppedWithWarning()
    {
        auto resolver = std::make_shared<CapturingResolver>();
        JsonBackend b(m_path);
        b.setPathResolver(resolver);

        auto g = b.group(QStringLiteral("Screen:"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("refusing write to malformed group")));
        g->writeInt(QStringLiteral("x"), 1); // Should warn and no-op
    }

    void groupList_enumeratesDotPathsAndResolverGroups()
    {
        auto resolver = std::make_shared<CapturingResolver>();

        JsonBackend b(m_path);
        b.setPathResolver(resolver);
        {
            auto g = b.group(QStringLiteral("A.B"));
            g->writeInt(QStringLiteral("k"), 1);
        }
        {
            auto g = b.group(QStringLiteral("Screen:DP-1"));
            g->writeInt(QStringLiteral("k"), 1);
        }

        const auto groups = b.groupList();
        QVERIFY(groups.contains(QStringLiteral("A")));
        QVERIFY(groups.contains(QStringLiteral("A.B")));
        QVERIFY(groups.contains(QStringLiteral("Screen:DP-1")));
        // The resolver-reserved "Screens" root should NOT appear as a dot-path group.
        QVERIFY(!groups.contains(QStringLiteral("Screens")));
    }

    void versionStamp_freshInstall()
    {
        {
            JsonBackend b(m_path);
            b.setVersionStamp(QStringLiteral("_version"), 3);
            auto g = b.group(QStringLiteral("Something"));
            g->writeInt(QStringLiteral("k"), 1);
            g.reset();
            b.sync();
        }

        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(root.value(QStringLiteral("_version")).toInt(), 3);
    }

    void atomicWrite_exposedForExternalUse()
    {
        QJsonObject root;
        root[QStringLiteral("k")] = 42;
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, root));

        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject readBack = QJsonDocument::fromJson(f.readAll()).object();
        QCOMPARE(readBack.value(QStringLiteral("k")).toInt(), 42);
    }

    void concurrentGroup_writesAreRefusedOnSecondInstance()
    {
#ifdef QT_DEBUG
        // Debug build: the constructor asserts — skip the live-fight check
        // since Q_ASSERT_X aborts the test process before we can exercise it.
        // The release-mode safety net (refuseWrite) is exercised in the
        // RelWithDebInfo / Release package CI jobs.
        QSKIP("Skipping re-entrancy fight in debug builds (Q_ASSERT_X aborts)");
#else
        JsonBackend b(m_path);
        auto first = b.group(QStringLiteral("G"));
        first->writeInt(QStringLiteral("k"), 1);

        QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("refusing writes on group")));
        auto second = b.group(QStringLiteral("G"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("dropping writeInt")));
        second->writeInt(QStringLiteral("k"), 42); // dropped
        second.reset();
        // first's value must survive the dropped write.
        QCOMPARE(first->readInt(QStringLiteral("k"), 0), 1);
#endif
    }

    void writeString_storesVerbatim()
    {
        // writeString is always verbatim — no content-dependent reshape.
        // A string that happens to look like a JSON array survives the
        // round-trip as the literal text. Structured data takes a different
        // code path (writeJson) that stores natively.
        JsonBackend b(m_path);
        auto g = b.group(QStringLiteral("G"));
        const QString jsonLike = QStringLiteral("[1,2,3]");
        g->writeString(QStringLiteral("raw"), jsonLike);
        QCOMPARE(g->readString(QStringLiteral("raw")), jsonLike);
    }

    void writeJson_storesNativeArray()
    {
        JsonBackend b(m_path);
        {
            auto g = b.group(QStringLiteral("G"));
            QJsonArray arr;
            arr.append(1);
            arr.append(2);
            arr.append(3);
            g->writeJson(QStringLiteral("structured"), arr);
        }
        b.sync();

        // The value must be stored as a native JSON array on disk, not as
        // a quoted JSON string. External readers inspecting the file should
        // see the structure directly.
        QFile f(m_path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        const QJsonValue stored = root.value(QStringLiteral("G")).toObject().value(QStringLiteral("structured"));
        QVERIFY(stored.isArray());
        QCOMPARE(stored.toArray().size(), 3);
    }

    void readJson_roundTripsNativeValue()
    {
        JsonBackend b(m_path);
        {
            auto g = b.group(QStringLiteral("G"));
            QJsonObject obj;
            obj[QLatin1String("k")] = QStringLiteral("v");
            g->writeJson(QStringLiteral("structured"), obj);
        }

        auto g = b.group(QStringLiteral("G"));
        const QJsonValue got = g->readJson(QStringLiteral("structured"));
        QVERIFY(got.isObject());
        QCOMPARE(got.toObject().value(QStringLiteral("k")).toString(), QStringLiteral("v"));
    }

    void sync_returnsFalseOnInvalidPath()
    {
        // Point the backend at an unwritable parent directory.
        JsonBackend b(QStringLiteral("/proc/1/no-write-here/config.json"));
        auto g = b.group(QStringLiteral("G"));
        g->writeInt(QStringLiteral("k"), 1);
        g.reset();
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("PhosphorConfig::JsonBackend: failed to")));
        QVERIFY(!b.sync());
    }

    void sync_preservesFilePermissions()
    {
        // QSaveFile commits via a rename to a fresh inode; without the
        // explicit perm restore in writeJsonAtomically, a user's chmod 600
        // would silently widen on every save.
        JsonBackend b(m_path);
        {
            auto g = b.group(QStringLiteral("G"));
            g->writeInt(QStringLiteral("k"), 1);
        }
        QVERIFY(b.sync());

        // Qt encodes both "Owner" (mapped to platform ACL) and "User" (file
        // owner's effective perms) bits; on Linux setPermissions writes both
        // sides of the pair for each bit, so the readback includes both.
        const QFile::Permissions tightened = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadUser | QFile::WriteUser;
        QVERIFY(QFile::setPermissions(m_path, tightened));
        QCOMPARE(QFile::permissions(m_path), tightened);

        // Next sync writes a new value and must preserve the tightened perms.
        {
            auto g = b.group(QStringLiteral("G"));
            g->writeInt(QStringLiteral("k"), 2);
        }
        QVERIFY(b.sync());
        QCOMPARE(QFile::permissions(m_path), tightened);
    }

    void keyList_returnsScalarLeavesOnly()
    {
        JsonBackend b(m_path);
        auto parent = b.group(QStringLiteral("Parent"));
        parent->writeInt(QStringLiteral("scalarA"), 1);
        parent->writeString(QStringLiteral("scalarB"), QStringLiteral("x"));
        parent.reset();

        // Create a sub-group so "Parent" now has both scalars and an object child.
        auto child = b.group(QStringLiteral("Parent.Child"));
        child->writeInt(QStringLiteral("inner"), 2);
        child.reset();

        auto parentAgain = b.group(QStringLiteral("Parent"));
        const QStringList keys = parentAgain->keyList();
        QVERIFY(keys.contains(QStringLiteral("scalarA")));
        QVERIFY(keys.contains(QStringLiteral("scalarB")));
        // Child is a sub-group, not a scalar key — must NOT appear.
        QVERIFY(!keys.contains(QStringLiteral("Child")));
    }

    void hasKey_reportsJsonObjectValues()
    {
        // Schema-declared keys that hold structured values (QVariantMap,
        // trigger lists written as JSON objects, etc.) land as JSON object
        // nodes on disk. hasKey() must report them — otherwise Store::write's
        // equality short-circuit and Store::reset's "is there a stored value?"
        // guard would silently skip such keys, leaving the on-disk state
        // out of sync with Store's view.
        JsonBackend b(m_path);
        auto g = b.group(QStringLiteral("G"));
        QJsonObject payload;
        payload[QStringLiteral("k")] = QStringLiteral("v");
        g->writeJson(QStringLiteral("structured"), payload);
        QVERIFY(g->hasKey(QStringLiteral("structured")));
    }

    void destructor_flushesDirtySynchronousState()
    {
        // QSettings auto-syncs on destruction; PhosphorConfig must do the
        // same so a consumer that wrote under Synchronous mode without
        // calling sync() doesn't silently lose the change at process
        // shutdown.
        {
            JsonBackend b(m_path);
            auto g = b.group(QStringLiteral("G"));
            g->writeInt(QStringLiteral("n"), 7);
            g.reset();
            // No explicit b.sync() — rely on the destructor to flush.
        }
        QVERIFY(QFile::exists(m_path));
        JsonBackend b2(m_path);
        auto g2 = b2.group(QStringLiteral("G"));
        QCOMPARE(g2->readInt(QStringLiteral("n"), 0), 7);
    }

    // ── Deferred sync policy ──────────────────────────────────────────────

    void deferredSync_coalescesRepeatedSyncsIntoOneFlush()
    {
        JsonBackend b(m_path);
        b.setSyncPolicy(JsonBackend::SyncPolicy::Deferred, 50);

        auto g = b.group(QStringLiteral("Group"));
        g->writeInt(QStringLiteral("counter"), 1);
        g.reset();

        // sync() with Deferred policy must NOT have committed anything yet.
        // The file shouldn't exist until the debounce window elapses, even
        // if we sync repeatedly in rapid succession.
        QVERIFY(b.sync());
        QVERIFY(b.sync());
        QVERIFY(b.sync());
        QVERIFY(!QFile::exists(m_path));

        // Wait past the debounce window. QTRY_* pumps the event loop so the
        // QTimer can fire; a bare sleep would keep the timer starved.
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_path), 2000);

        // File landed exactly once — the three sync() calls coalesced.
        QJsonDocument doc = QJsonDocument::fromJson(readFile(m_path));
        QCOMPARE(doc.object().value(QStringLiteral("Group")).toObject().value(QStringLiteral("counter")).toInt(), 1);
    }

    void deferredSync_flushPendingCommitsImmediately()
    {
        JsonBackend b(m_path);
        b.setSyncPolicy(JsonBackend::SyncPolicy::Deferred, 10'000); // long window

        auto g = b.group(QStringLiteral("Group"));
        g->writeString(QStringLiteral("k"), QStringLiteral("v"));
        g.reset();

        QVERIFY(b.sync());
        QVERIFY(!QFile::exists(m_path)); // pending

        // flushPending() forces the commit now instead of waiting 10s.
        QVERIFY(b.flushPending());
        QVERIFY(QFile::exists(m_path));

        // Second flushPending() on a now-clean backend is a cheap no-op.
        QVERIFY(b.flushPending());
    }

    void deferredSync_switchBackToSynchronousFlushesPending()
    {
        JsonBackend b(m_path);
        b.setSyncPolicy(JsonBackend::SyncPolicy::Deferred, 10'000);

        auto g = b.group(QStringLiteral("Group"));
        g->writeBool(QStringLiteral("flag"), true);
        g.reset();

        QVERIFY(b.sync()); // deferred; nothing on disk yet
        QVERIFY(!QFile::exists(m_path));

        // Switching back to Synchronous must flush the in-flight deferred
        // write — otherwise a consumer that flips policies mid-write would
        // silently drop the change.
        b.setSyncPolicy(JsonBackend::SyncPolicy::Synchronous);
        QVERIFY(QFile::exists(m_path));
        QCOMPARE(b.syncPolicy(), JsonBackend::SyncPolicy::Synchronous);
    }

    void deferredSync_destructorFlushesPending()
    {
        {
            JsonBackend b(m_path);
            b.setSyncPolicy(JsonBackend::SyncPolicy::Deferred, 10'000);

            auto g = b.group(QStringLiteral("Group"));
            g->writeInt(QStringLiteral("n"), 42);
            g.reset();

            QVERIFY(b.sync()); // deferred
            QVERIFY(!QFile::exists(m_path));
            // scope-exit: ~JsonBackend must flush pending so we don't lose
            // the write when a daemon gets torn down between sync() and the
            // timer firing.
        }
        QVERIFY(QFile::exists(m_path));
        QJsonDocument doc = QJsonDocument::fromJson(readFile(m_path));
        QCOMPARE(doc.object().value(QStringLiteral("Group")).toObject().value(QStringLiteral("n")).toInt(), 42);
    }

    // ─── Unreadable store: writes refused, last good document kept ─────────

    void unreadableStore_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("parse error") << QByteArray("{not json");
        QTest::newRow("non-object root") << QByteArray("[1, 2, 3]");
    }

    /// An unreadable file must never be overwritten by the stand-in root:
    /// that is how a hand edit with one typo used to cost the whole config.
    void unreadableStore()
    {
        QFETCH(QByteArray, bytes);
        QVERIFY(writeFile(m_path, bytes));
        JsonBackend b(m_path);
        QVERIFY(b.loadFailed());
        {
            auto g = b.group(QStringLiteral("G"));
            QCOMPARE(g->readString(QStringLiteral("k"), QStringLiteral("d")), QStringLiteral("d"));
            g->writeString(QStringLiteral("k"), QStringLiteral("v"));
        }
        QVERIFY(!b.sync());
        QVERIFY(!b.commit());
        QVERIFY(b.isDirty());
        QCOMPARE(readFile(m_path), bytes);
    }

    void missingStore_isNotALoadFailure()
    {
        JsonBackend b(m_path);
        QVERIFY(!b.loadFailed());
        {
            auto g = b.group(QStringLiteral("G"));
            g->writeString(QStringLiteral("k"), QStringLiteral("v"));
        }
        QVERIFY(b.commit());
        QVERIFY(QFile::exists(m_path));
    }

    /// A reparse that cannot read the file falls back to what this backend
    /// last wrote, not to an empty root, so readers see "unchanged" rather
    /// than every key at its default.
    void failedReparse_keepsLastWrittenDocument()
    {
        JsonBackend b(m_path);
        {
            auto g = b.group(QStringLiteral("G"));
            g->writeString(QStringLiteral("k"), QStringLiteral("written"));
        }
        QVERIFY(b.commit());
        QVERIFY(writeFile(m_path, QByteArray("garbage")));
        b.reparseConfiguration();
        QVERIFY(b.loadFailed());
        QVERIFY(!b.isDirty());
        {
            auto g = b.group(QStringLiteral("G"));
            QCOMPARE(g->readString(QStringLiteral("k")), QStringLiteral("written"));
        }

        // Repairing the file and reparsing clears the refusal.
        QVERIFY(writeFile(m_path, QByteArray(R"({"G":{"k":"repaired"}})")));
        b.reparseConfiguration();
        QVERIFY(!b.loadFailed());
        {
            auto g = b.group(QStringLiteral("G"));
            QCOMPARE(g->readString(QStringLiteral("k")), QStringLiteral("repaired"));
            g->writeString(QStringLiteral("k"), QStringLiteral("again"));
        }
        QVERIFY(b.commit());
        QCOMPARE(QJsonDocument::fromJson(readFile(m_path))
                     .object()
                     .value(QStringLiteral("G"))
                     .toObject()
                     .value(QStringLiteral("k"))
                     .toString(),
                 QStringLiteral("again"));
    }

    /// Load::Deferred reads nothing until the first reload, so a caller that
    /// reloads at once reads and reports the file one time. Until then the
    /// root is not the file, so a write must not replace it.
    void deferredLoad_readsAtTheFirstReloadAndRefusesWritesBefore()
    {
        QVERIFY(writeFile(m_path, QByteArray(R"({"G":{"k":"file"}})")));
        JsonBackend b(m_path, JsonBackend::Load::Deferred);
        {
            auto g = b.group(QStringLiteral("G"));
            QCOMPARE(g->readString(QStringLiteral("k"), QStringLiteral("none")), QStringLiteral("none"));
            g->writeString(QStringLiteral("k"), QStringLiteral("early"));
        }
        QVERIFY(!b.commit());
        QVERIFY(readFile(m_path).contains("file"));
        QCOMPARE(b.reload(), QString());
        {
            auto g = b.group(QStringLiteral("G"));
            QCOMPARE(g->readString(QStringLiteral("k")), QStringLiteral("file"));
            g->writeString(QStringLiteral("k"), QStringLiteral("late"));
        }
        QVERIFY(b.commit());
        QVERIFY(readFile(m_path).contains("late"));
    }

    void deferredLoad_refusesWritesEvenWhenOverwritingUnreadable()
    {
        QVERIFY(writeFile(m_path, QByteArray(R"({"G":{"k":"file"}})")));
        JsonBackend b(m_path, JsonBackend::Load::Deferred);
        b.setOverwriteUnreadable(true);
        {
            auto g = b.group(QStringLiteral("G"));
            g->writeString(QStringLiteral("k"), QStringLiteral("early"));
        }
        QVERIFY(!b.commit());
        QVERIFY(readFile(m_path).contains("file"));
    }

    /// blankFile() tells a file of only white space from one that holds {}.
    void blankFile_tellsWhiteSpaceFromAnEmptyObject()
    {
        QVERIFY(writeFile(m_path, QByteArray(" \n")));
        JsonBackend b(m_path);
        QVERIFY(b.blankFile());
        QVERIFY(writeFile(m_path, QByteArray("{}")));
        QCOMPARE(b.reload(), QString());
        QVERIFY(!b.blankFile());
        QVERIFY(QFile::remove(m_path));
        QCOMPARE(b.reload(), QString());
        QVERIFY(!b.blankFile());
    }

    /// reload() is the reparse for a caller that reports problems itself: it
    /// returns the reason, and a clean read ends the refusal that the first
    /// load set. Afterglow reloads this way, and without the second half every
    /// later write stayed refused once it had started on a broken file.
    void reload_reportsAndEndsTheRefusal()
    {
        QVERIFY(writeFile(m_path, QByteArray("{not json")));
        JsonBackend b(m_path);
        QVERIFY(b.loadFailed());
        const QString problem = b.reload();
        QVERIFY(problem.contains(QStringLiteral("does not parse")));
        QVERIFY(b.loadFailed());

        QVERIFY(writeFile(m_path, QByteArray(R"({"G":{"k":"fixed"}})")));
        QCOMPARE(b.reload(), QString());
        QVERIFY(!b.loadFailed());
        {
            auto g = b.group(QStringLiteral("G"));
            QCOMPARE(g->readString(QStringLiteral("k")), QStringLiteral("fixed"));
            g->writeString(QStringLiteral("k"), QStringLiteral("saved"));
        }
        QVERIFY(b.commit());
        QVERIFY(readFile(m_path).contains("saved"));
    }

    /// reload() takes the file's stamp, so a file rewritten since an earlier
    /// load but read cleanly now is not mistaken for one broken after this read.
    void reload_takesTheStampOfItsRead()
    {
        QVERIFY(writeFile(m_path, QByteArray(R"({"G":{"k":"one"}})")));
        JsonBackend b(m_path);
        // Another size, so the stamp moves on a coarse-mtime filesystem too.
        QVERIFY(writeFile(m_path, QByteArray(R"({"G":{"k":"a longer value"}})")));
        QCOMPARE(b.reload(), QString());
        {
            auto g = b.group(QStringLiteral("G"));
            g->writeString(QStringLiteral("k"), QStringLiteral("two"));
        }
        QVERIFY(b.commit());
        QVERIFY(!b.loadFailed());
    }

    void reload_opensAMissingFileAsEmpty()
    {
        JsonBackend b(m_path);
        QCOMPARE(b.reload(), QString());
        QVERIFY(!b.loadFailed());
    }

    /// A file that holds only white space (a `touch`) holds nothing a write
    /// could lose, so it reads as empty and accepts writes.
    void whiteSpaceFile_readsAsEmpty()
    {
        QVERIFY(writeFile(m_path, QByteArray(" \n\t\n")));
        JsonBackend b(m_path);
        QVERIFY(!b.loadFailed());
        {
            auto g = b.group(QStringLiteral("G"));
            g->writeString(QStringLiteral("k"), QStringLiteral("v"));
        }
        QVERIFY(b.commit());
        QCOMPARE(QJsonDocument::fromJson(readFile(m_path))
                     .object()
                     .value(QStringLiteral("G"))
                     .toObject()
                     .value(QStringLiteral("k"))
                     .toString(),
                 QStringLiteral("v"));
    }

    /// A refused write stays refused until a reparse reads the repaired
    /// file. Repairing the file alone is not enough: the backend cannot know
    /// it is readable again until it looks.
    void refusedWrite_staysRefusedUntilReparse()
    {
        QVERIFY(writeFile(m_path, QByteArray("garbage")));
        JsonBackend b(m_path);
        {
            auto g = b.group(QStringLiteral("G"));
            g->writeString(QStringLiteral("k"), QStringLiteral("pending"));
        }
        QVERIFY(!b.commit());
        QVERIFY(writeFile(m_path, QByteArray("{}")));
        QVERIFY(!b.commit());
        QCOMPARE(readFile(m_path), QByteArray("{}"));
    }

    /// A file broken AFTER the backend loaded it cleanly is not overwritten
    /// by the next flush. Most writers never reparse before they save, so
    /// the refusal cannot wait for a reparse to notice the breakage.
    void brokenAfterLoad_refusedAtFlush()
    {
        QVERIFY(writeFile(m_path, QByteArray(R"({"G":{"k":"v"}})")));
        JsonBackend b(m_path);
        QVERIFY(!b.loadFailed());
        // Make sure the rewrite moves the size, so the stamp check cannot
        // miss it on a coarse-mtime filesystem.
        const QByteArray broken("{ \"G\": { \"k\": oops a hand edit with a typo");
        QVERIFY(writeFile(m_path, broken));
        {
            auto g = b.group(QStringLiteral("G"));
            g->writeString(QStringLiteral("k"), QStringLiteral("app"));
        }
        QVERIFY(!b.commit());
        QVERIFY(b.loadFailed());
        QCOMPARE(readFile(m_path), broken);
    }

    /// The opt-out for machine-written files: a failed load does not block
    /// the next write.
    void overwriteUnreadable_writesAnyway()
    {
        QVERIFY(writeFile(m_path, QByteArray("garbage")));
        JsonBackend b(m_path);
        b.setOverwriteUnreadable(true);
        QVERIFY(b.loadFailed());
        {
            auto g = b.group(QStringLiteral("G"));
            g->writeString(QStringLiteral("k"), QStringLiteral("v"));
        }
        QVERIFY(b.commit());
        QCOMPARE(QJsonDocument::fromJson(readFile(m_path))
                     .object()
                     .value(QStringLiteral("G"))
                     .toObject()
                     .value(QStringLiteral("k"))
                     .toString(),
                 QStringLiteral("v"));
    }

    void unreadableStore_refusesMigration()
    {
        const QByteArray bytes("{broken");
        QVERIFY(writeFile(m_path, bytes));
        JsonBackend b(m_path);
        Schema schema;
        schema.version = 2;
        schema.migrations = {
            {1,
             [](QJsonObject& root) {
                 root[QStringLiteral("_version")] = 2;
             }},
        };
        QVERIFY(!b.applyMigration(schema));
        QCOMPARE(readFile(m_path), bytes);
    }

    // ─── Overlay ──────────────────────────────────────────────────────────

    /// Overlay values win over the file's for every typed reader, and file
    /// keys the overlay does not name stay visible.
    void overlay_winsOverFile()
    {
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, object(R"({"Audio": {"enabled": true, "volume": 50}})")));
        JsonBackend b(m_path);
        b.setOverlay(object(R"({"Audio": {"enabled": false, "device": "hdmi", "gain": 1.5}})"));
        QCOMPARE(b.overlay(), object(R"({"Audio": {"enabled": false, "device": "hdmi", "gain": 1.5}})"));

        auto g = b.group(QStringLiteral("Audio"));
        QCOMPARE(g->readBool(QStringLiteral("enabled"), true), false);
        QCOMPARE(g->readString(QStringLiteral("enabled")), QStringLiteral("false"));
        QCOMPARE(g->readJson(QStringLiteral("enabled")), QJsonValue(false));
        QCOMPARE(g->readString(QStringLiteral("device")), QStringLiteral("hdmi"));
        QCOMPARE(g->readDouble(QStringLiteral("gain")), 1.5);
        QCOMPARE(g->readInt(QStringLiteral("volume")), 50);
    }

    /// hasKey and keyList include keys only the overlay supplies. A
    /// persisted view sees the file alone.
    void overlay_keyListAndHasKeyIncludeOverlayKeys()
    {
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, object(R"({"Audio": {"volume": 50}})")));
        JsonBackend b(m_path);
        b.setOverlay(object(R"({"Audio": {"device": "hdmi"}})"));
        {
            auto g = b.group(QStringLiteral("Audio"));
            QVERIFY(g->hasKey(QStringLiteral("device")));
            QVERIFY(g->hasKey(QStringLiteral("volume")));
            QStringList keys = g->keyList();
            keys.sort();
            QCOMPARE(keys, (QStringList{QStringLiteral("device"), QStringLiteral("volume")}));
        }
        auto persisted = b.persistedGroup(QStringLiteral("Audio"));
        QVERIFY(!persisted->hasKey(QStringLiteral("device")));
        QCOMPARE(persisted->keyList(), QStringList{QStringLiteral("volume")});
        QCOMPARE(persisted->readString(QStringLiteral("device"), QStringLiteral("none")), QStringLiteral("none"));
    }

    /// reparseConfiguration reloads the file and keeps the overlay winning
    /// over the reloaded values.
    void overlay_survivesReparse()
    {
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, object(R"({"Audio": {"enabled": true, "volume": 50}})")));
        JsonBackend b(m_path);
        b.setOverlay(object(R"({"Audio": {"enabled": false}})"));

        QVERIFY(JsonBackend::writeJsonAtomically(m_path, object(R"({"Audio": {"enabled": true, "volume": 70}})")));
        b.reparseConfiguration();

        auto g = b.group(QStringLiteral("Audio"));
        QCOMPARE(g->readBool(QStringLiteral("enabled"), true), false);
        QCOMPARE(g->readInt(QStringLiteral("volume")), 70);
    }

    /// Writing one key of a group the overlay also covers persists that key
    /// and nothing of the overlay: neither the overridden key's overlay value
    /// nor a key only the overlay supplies. Setting the overlay is not a
    /// change either.
    void overlay_fileWriteDoesNotLeakOverlay()
    {
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, object(R"({"Audio": {"enabled": true}})")));
        {
            JsonBackend b(m_path);
            b.setOverlay(object(R"({"Audio": {"enabled": false, "device": "hdmi"}, "Extra": {"k": 1}})"));
            QVERIFY(!b.isDirty());

            auto g = b.group(QStringLiteral("Audio"));
            g->writeInt(QStringLiteral("volume"), 80);
            // The written value reads back; the overlay still wins elsewhere.
            QCOMPARE(g->readInt(QStringLiteral("volume")), 80);
            QCOMPARE(g->readBool(QStringLiteral("enabled"), true), false);
            g.reset();
            b.writeRootString(QStringLiteral("rootKey"), QStringLiteral("v"));
            QVERIFY(b.sync());
            QCOMPARE(b.jsonRootSnapshot(), object(R"({"Audio": {"enabled": true, "volume": 80},
                                                      "General": {"rootKey": "v"}})"));
        }
        QCOMPARE(QJsonDocument::fromJson(readFile(m_path)).object(),
                 object(R"({"Audio": {"enabled": true, "volume": 80}, "General": {"rootKey": "v"}})"));
    }

    /// deleteKey removes the file's value only: a key the overlay supplies
    /// stays readable, and the delete still reaches disk.
    void overlay_deleteKeyActsOnFileOnly()
    {
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, object(R"({"Audio": {"enabled": true, "volume": 50}})")));
        JsonBackend b(m_path);
        b.setOverlay(object(R"({"Audio": {"enabled": false}})"));
        {
            auto g = b.group(QStringLiteral("Audio"));
            g->deleteKey(QStringLiteral("enabled"));
            QVERIFY(g->hasKey(QStringLiteral("enabled")));
            QCOMPARE(g->readBool(QStringLiteral("enabled"), true), false);
        }
        QVERIFY(b.sync());
        QCOMPARE(QJsonDocument::fromJson(readFile(m_path)).object(), object(R"({"Audio": {"volume": 50}})"));
    }

    /// Nested groups merge key by key, at every depth, the way merging the
    /// overlay into the document would: a dot-path group sees file and
    /// overlay siblings together, an object-valued key merges rather than
    /// being replaced, and a group only the overlay supplies is enumerated.
    void overlay_nestedGroupsMergeKeyByKey()
    {
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, object(R"({
            "Snapping": {"Other": 1, "Behavior": {"ZoneSpan": {"Enabled": true, "Modifier": 2}}},
            "Keys": {"bindings": {"copy": "ctrl+c", "paste": "ctrl+v"}}
        })")));
        JsonBackend b(m_path);
        b.setOverlay(object(R"({
            "Snapping": {"Behavior": {"ZoneSpan": {"Modifier": 5}, "Fresh": {"on": true}}},
            "Keys": {"bindings": {"copy": "ctrl+shift+c"}},
            "Overlaid": {"Only": {"k": 1}}
        })"));
        {
            auto g = b.group(QStringLiteral("Snapping.Behavior.ZoneSpan"));
            QCOMPARE(g->readBool(QStringLiteral("Enabled")), true);
            QCOMPARE(g->readInt(QStringLiteral("Modifier")), 5);
        }
        {
            auto g = b.group(QStringLiteral("Snapping"));
            QCOMPARE(g->keyList(), QStringList{QStringLiteral("Other")});
        }
        {
            auto g = b.group(QStringLiteral("Keys"));
            QCOMPARE(g->readJson(QStringLiteral("bindings")),
                     QJsonValue(object(R"({"copy": "ctrl+shift+c", "paste": "ctrl+v"})")));
        }
        {
            auto g = b.group(QStringLiteral("Overlaid.Only"));
            QCOMPARE(g->readInt(QStringLiteral("k")), 1);
        }
        QStringList groups = b.groupList();
        groups.sort();
        QCOMPARE(groups,
                 (QStringList{QStringLiteral("Keys"), QStringLiteral("Keys.bindings"), QStringLiteral("Overlaid"),
                              QStringLiteral("Overlaid.Only"), QStringLiteral("Snapping"),
                              QStringLiteral("Snapping.Behavior"), QStringLiteral("Snapping.Behavior.Fresh"),
                              QStringLiteral("Snapping.Behavior.ZoneSpan")}));
    }

    /// A non-object overlay value where the file has a group replaces the
    /// group outright, exactly as merging it into the document would.
    void overlay_scalarAtGroupPositionReplacesGroup()
    {
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, object(R"({"Audio": {"volume": 50}})")));
        JsonBackend b(m_path);
        b.setOverlay(object(R"({"Audio": 5})"));
        {
            auto g = b.group(QStringLiteral("Audio"));
            QVERIFY(!g->hasKey(QStringLiteral("volume")));
        }
        QVERIFY(!b.groupList().contains(QStringLiteral("Audio")));
        // Clearing the overlay restores the file's view.
        b.setOverlay({});
        auto g = b.group(QStringLiteral("Audio"));
        QCOMPARE(g->readInt(QStringLiteral("volume")), 50);
    }

    /// Root keys and resolver-mapped groups see the overlay too: it has the
    /// document's shape, so a group lives wherever the file would put it.
    void overlay_appliesToRootKeysAndResolverPaths()
    {
        QVERIFY(JsonBackend::writeJsonAtomically(
            m_path, object(R"({"General": {"theme": "file", "kept": "yes"}, "Screens": {"a": {"scale": 1}}})")));
        JsonBackend b(m_path);
        b.setPathResolver(std::make_shared<CapturingResolver>());
        b.setOverlay(object(R"({"General": {"theme": "overlay"}, "Screens": {"a": {"scale": 2}, "b": {"scale": 3}}})"));

        QCOMPARE(b.readRootString(QStringLiteral("theme")), QStringLiteral("overlay"));
        QCOMPARE(b.readRootString(QStringLiteral("kept")), QStringLiteral("yes"));
        {
            auto g = b.group(QStringLiteral("Screen:a"));
            QCOMPARE(g->readInt(QStringLiteral("scale")), 2);
        }
        {
            auto g = b.group(QStringLiteral("Screen:b"));
            QCOMPARE(g->readInt(QStringLiteral("scale")), 3);
        }
        QStringList groups = b.groupList();
        groups.sort();
        QCOMPARE(groups, (QStringList{QStringLiteral("Screen:a"), QStringLiteral("Screen:b")}));
    }

    /// A reparse that finds the file unreadable keeps the overlay laid over
    /// the last good document, the persisted view and the snapshot show that
    /// document alone, and a repaired file clears the failure without
    /// dropping the overlay.
    void overlay_laidOverLastGoodRootOnFailedReparse()
    {
        QVERIFY(JsonBackend::writeJsonAtomically(m_path, object(R"({"G": {"a": 1, "b": 2}})")));
        JsonBackend b(m_path);
        b.setOverlay(object(R"({"G": {"a": 9, "c": 7}})"));

        QVERIFY(writeFile(m_path, QByteArray("garbage")));
        b.reparseConfiguration();
        QVERIFY(b.loadFailed());
        {
            auto g = b.group(QStringLiteral("G"));
            QCOMPARE(g->readInt(QStringLiteral("a")), 9);
            QCOMPARE(g->readInt(QStringLiteral("b")), 2);
            QCOMPARE(g->readInt(QStringLiteral("c")), 7);
        }
        {
            auto g = b.persistedGroup(QStringLiteral("G"));
            QCOMPARE(g->readInt(QStringLiteral("a")), 1);
            QVERIFY(!g->hasKey(QStringLiteral("c")));
        }
        QCOMPARE(b.jsonRootSnapshot(), object(R"({"G": {"a": 1, "b": 2}})"));
        QCOMPARE(readFile(m_path), QByteArray("garbage"));

        QVERIFY(JsonBackend::writeJsonAtomically(m_path, object(R"({"G": {"a": 3}})")));
        b.reparseConfiguration();
        QVERIFY(!b.loadFailed());
        {
            auto g = b.group(QStringLiteral("G"));
            QCOMPARE(g->readInt(QStringLiteral("a")), 9);
            QVERIFY(!g->hasKey(QStringLiteral("b")));
        }
        QCOMPARE(b.jsonRootSnapshot(), object(R"({"G": {"a": 3}})"));
    }

private:
    [[nodiscard]] static bool writeFile(const QString& path, const QByteArray& bytes)
    {
        QFile f(path);
        return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(bytes) == bytes.size();
    }

    /// Parse a JSON object literal. Test inputs are always well formed.
    static QJsonObject object(const char* json)
    {
        return QJsonDocument::fromJson(QByteArray(json)).object();
    }

    static QByteArray readFile(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return f.readAll();
    }
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_path;
};

} // namespace PhosphorConfig::tests

QTEST_MAIN(PhosphorConfig::tests::TestJsonBackend)
#include "test_json_backend.moc"
