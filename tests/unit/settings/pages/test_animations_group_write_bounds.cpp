// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_group_write_bounds.cpp
 * @brief What the group writers REFUSE, BOUND, or fail to do — the arms that
 *        are reached when the caller or the disk misbehaves.
 *
 * Split out of test_animations_group_writes.cpp when that file reached the
 * project's size ceiling. The cut is along a real seam: everything HERE is a
 * bound or a disk-level failure, driven through the real writer rather than
 * asserted at the guard. The converse does not hold — the sibling keeps several
 * refusal slots of its own (the async-discard refusals, the unrecognised-field
 * refusal, the failed clear), which sit beside the success paths they
 * contrast with.
 *
 * Each of these pins a guard that previously survived deletion with the whole
 * suite still green, so each names the production line it fails on:
 *   - the knownFields allowlist, asserted against the FILE, because rawProfile
 *     sanitises on read and would drop a stray key whether or not the writer did
 *   - the value bounds, which keep a profile file under the read cap (a file
 *     past it is skipped WHOLE, so the card renders everything as inherited
 *     while hasOverride still says true)
 *   - the non-string curve type test, without which a numeric becomes an
 *     engaged curve nobody chose
 *   - the Unchanged-path snapshot release, and the dirty flip sampled outside
 *     the written-only branch
 *   - the partial-failure count, which is the whole reason the writer returns
 *     int rather than bool
 *   - the failure-toast latch RESET, so a later failure is announced rather
 *     than swallowed
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QVariant>

#include <unistd.h> // geteuid — the read-only-directory slots are no-ops as root

#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include "phosphor_i18n.h"
#include "settings/pages/animationspagecontroller.h"

using namespace PlasmaZones;

namespace {

/// The same mirrored group the sibling file uses: the two window.appearance
/// legs, which is the real mirrored group in the shipped UI.
const QString kPrimary = QStringLiteral("window.appearance.open");
const QString kMirror = QStringLiteral("window.appearance.close");
QStringList group()
{
    return QStringList{kPrimary, kMirror};
}

} // namespace

class TestAnimationsGroupWriteBounds : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Guards against a leaked process-wide registry publish from an earlier
    /// slot, the same way every sibling animations test file does.
    void init()
    {
        QCOMPARE(PhosphorAnimation::PhosphorProfileRegistry::defaultRegistry(), nullptr);
    }

    /// A field outside the allowlist never reaches DISK.
    ///
    /// Asserted against the file rather than through `rawProfile`, and that is
    /// the whole point: `rawProfile` sanitises on read, so it drops an unknown
    /// key whether or not the writer did. A slot phrased against it would pass
    /// with the allowlist deleted, which is exactly the shape this audit went
    /// looking for. What the guard actually prevents is a stray key landing in
    /// the user's profile file and staying there until some later write happens
    /// to rewrite the object.
    void anUnknownFieldNeverReachesDisk()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unknown profile field")));
        QCOMPARE(c.setOverrideMergedOnPaths(
                     QStringList{kPrimary},
                     QVariantMap{{QStringLiteral("bogusKey"), 1}, {QStringLiteral("duration"), 900}}, QVariant()),
                 1);

        QFile onDisk(tmp.path() + QLatin1Char('/') + kPrimary + QStringLiteral(".json"));
        QVERIFY2(onDisk.open(QIODevice::ReadOnly), "the accepted field did not produce a profile file");
        const QJsonObject obj = QJsonDocument::fromJson(onDisk.readAll()).object();
        QVERIFY2(!obj.contains(QStringLiteral("bogusKey")), "an unknown field was written to the profile file");
        QCOMPARE(obj.value(QStringLiteral("duration")).toInt(), 900);
    }

    /// A PARTIAL failure reports the count that landed, not the refusal
    /// sentinel.
    ///
    /// The whole reason this writer returns int rather than bool. The
    /// all-paths-fail case is pinned by the sibling file's
    /// `aFailedWriteToastsOnceRatherThanOncePerRetry` at 0, which a blanket
    /// `return -1` regression would also fail — but a MIXED batch is the case the
    /// distinction exists for, and nothing reached it: the card stops
    /// re-issuing on a refusal, so folding a partial failure into -1 would
    /// abandon the paths that did write.
    ///
    /// One path is made unwritable by putting a DIRECTORY where its profile
    /// file belongs, which leaves its sibling and the directory itself alone.
    void aPartialFailureReportsTheCountThatLanded()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(QDir(tmp.path()).mkpath(kMirror + QStringLiteral(".json")));

        QSignalSpy toasts(&c, &AnimationsPageController::toastRequested);
        const int written =
            c.setOverrideMergedOnPaths(group(), QVariantMap{{QStringLiteral("duration"), 640}}, QVariant());

        QVERIFY2(written == 1,
                 qPrintable(QStringLiteral("expected the one writable path to be counted, got %1").arg(written)));
        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("duration")).toInt(), 640);
        // And the user is told, once, that not everything saved.
        QCOMPARE(toasts.count(), 1);
        QCOMPARE(toasts.first().at(0).toString(), PhosphorI18n::tr("Some animation settings could not be saved."));
    }

    /// An over-long value is dropped rather than written.
    ///
    /// The allowlist bounds which KEYS reach disk; nothing bounded the values
    /// riding with them, and `presetName` takes an arbitrary string. A profile
    /// file pushed past the read cap is skipped whole on the way back in, so
    /// the card renders every field as inherited while `hasOverride` still
    /// reports true — a card asserting something untrue about itself until the
    /// next write repairs it.
    void anOverLongValueIsDroppedRatherThanWritten()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("over-long value")));
        QCOMPARE(
            c.setOverrideMergedOnPaths(QStringList{kPrimary},
                                       QVariantMap{{QStringLiteral("presetName"), QString(64 * 1024, QLatin1Char('x'))},
                                                   {QStringLiteral("duration"), 850}},
                                       QVariant()),
            1);

        QFile onDisk(tmp.path() + QLatin1Char('/') + kPrimary + QStringLiteral(".json"));
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        const QJsonObject obj = QJsonDocument::fromJson(onDisk.readAll()).object();
        QVERIFY2(!obj.contains(QStringLiteral("presetName")), "an over-long value was written to the profile file");
        QCOMPARE(obj.value(QStringLiteral("duration")).toInt(), 850);
        // The file stayed readable, which is the point: rawProfile still sees
        // the field that did land.
        QCOMPARE(c.rawProfile(kPrimary).value(QStringLiteral("duration")).toInt(), 850);
    }

    /// A non-string curve is treated as "the user did not touch the curve",
    /// not stringified into one.
    ///
    /// Without the type check the QVariant is converted anyway, so a numeric 5
    /// becomes the curve "5" — an engaged curve nobody chose, pinned onto every
    /// path in the group and stopping each from inheriting.
    void aNonStringCurveIsTreatedAsUntouched()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("non-string curve")));
        QCOMPARE(c.setOverrideMergedOnPaths(QStringList{kPrimary}, QVariantMap{{QStringLiteral("duration"), 700}},
                                            QVariant(5)),
                 1);

        QFile onDisk(tmp.path() + QLatin1Char('/') + kPrimary + QStringLiteral(".json"));
        QVERIFY(onDisk.open(QIODevice::ReadOnly));
        const QJsonObject obj = QJsonDocument::fromJson(onDisk.readAll()).object();
        QCOMPARE(obj.value(QStringLiteral("duration")).toInt(), 700);
        QVERIFY2(!obj.contains(QStringLiteral("curve")), "a non-string curve was stringified into an engaged curve");
    }

    /// An Unchanged write releases a snapshot stranded by an earlier edit, and
    /// reports the dirty flip even though it wrote nothing.
    ///
    /// The scenario is the one the writer's own comment describes: an outside
    /// writer restores the pre-edit bytes, so the next identical write compares
    /// equal and the first edit's snapshot would otherwise never be released —
    /// leaving the page reporting pending changes that no further edit can
    /// clear. What makes the restore work is that the snapshot captured the
    /// SEED file's own bytes, so putting those same bytes back is what releases
    /// it — not any property of the format. (The seed does happen to serialise
    /// identically to what the writer produces, since QJsonObject sorts keys,
    /// and the Unchanged verdict is a QJsonObject compare rather than a byte
    /// one, so neither detail is load-bearing here.)
    void anUnchangedRewriteReleasesAStrandedSnapshot()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        const QString filePath = tmp.path() + QLatin1Char('/') + kPrimary + QStringLiteral(".json");
        QJsonObject seed;
        seed.insert(QStringLiteral("duration"), 600);
        seed.insert(QStringLiteral("name"), kPrimary);
        const QByteArray seedBytes = QJsonDocument(seed).toJson(QJsonDocument::Indented);
        {
            QFile f(filePath);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QVERIFY(f.write(seedBytes) == seedBytes.size());
        }

        // Edit it, which snapshots the seed bytes.
        QCOMPARE(c.setOverrideMergedOnPaths(QStringList{kPrimary}, QVariantMap{{QStringLiteral("duration"), 900}},
                                            QVariant()),
                 1);
        QVERIFY(c.hasPendingChanges());

        // An outside writer puts the seed back, byte for byte.
        {
            QFile f(filePath);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QVERIFY(f.write(seedBytes) == seedBytes.size());
        }

        QSignalSpy dirtied(&c, &AnimationsPageController::pendingChangesChanged);
        // Identical to what is on disk now, so nothing is written and the count
        // is 0 — which is a SUCCESS under this writer's convention, not a
        // refusal.
        QCOMPARE(c.setOverrideMergedOnPaths(QStringList{kPrimary}, QVariantMap{{QStringLiteral("duration"), 600}},
                                            QVariant()),
                 0);
        QVERIFY2(!c.hasPendingChanges(), "the stranded snapshot was never released");
        QCOMPARE(dirtied.count(), 1);
    }

    /// The failure toast latch RESETS once a write lands, so a second failure
    /// later in the same session is announced rather than swallowed.
    ///
    /// The once-per-session half is pinned in the sibling file. This is the half that makes
    /// the latch honest: a user who fixes the disk and hits a later failure has
    /// to be told. Driven by taking the directory's write permission away and
    /// giving it back, because the blocker-file technique the sibling uses
    /// cannot be undone once the directory has been created.
    void theFailureToastLatchResetsAfterAWriteLands()
    {
        if (::geteuid() == 0) {
            QSKIP("running as root — directory mode bits are ignored, so the write cannot be made to fail");
        }
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QFile dirAsFile(tmp.path());
        const auto restorePerms = qScopeGuard([&dirAsFile]() {
            dirAsFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        });
        const auto block = [&dirAsFile]() {
            return dirAsFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        };
        const auto unblock = [&dirAsFile]() {
            return dirAsFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        };

        QSignalSpy toasts(&c, &AnimationsPageController::toastRequested);

        // No ignoreMessage: a read-only directory that already EXISTS fails at
        // the QSaveFile open, which logs nothing at all. That silence is the
        // reason the toast has to exist, and it is why this slot asserts on the
        // toast rather than on the journal.
        QVERIFY(block());
        for (int i = 0; i < 2; ++i) {
            c.setOverrideMergedOnPaths(QStringList{kPrimary}, QVariantMap{{QStringLiteral("duration"), 600 + i}},
                                       QVariant());
        }
        QCOMPARE(toasts.count(), 1);

        // One write that lands clears the latch.
        QVERIFY(unblock());
        QCOMPARE(c.setOverrideMergedOnPaths(QStringList{kPrimary}, QVariantMap{{QStringLiteral("duration"), 800}},
                                            QVariant()),
                 1);

        // A later failure must be announced again rather than swallowed.
        QVERIFY(block());
        c.setOverrideMergedOnPaths(QStringList{kPrimary}, QVariantMap{{QStringLiteral("duration"), 950}}, QVariant());
        QVERIFY2(toasts.count() == 2,
                 qPrintable(QStringLiteral("expected a second toast after a successful write reset the latch, got %1")
                                .arg(toasts.count())));
    }
};

QTEST_MAIN(TestAnimationsGroupWriteBounds)
#include "test_animations_group_write_bounds.moc"
