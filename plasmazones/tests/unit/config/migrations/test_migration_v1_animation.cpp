// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include "config/configmigration.h"
#include "config/configdefaults.h"
#include "config/configbackends.h"
#include "helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestMigrationV1Animation : public QObject
{
    Q_OBJECT

private:
    void writeIniFile(const QString& path, const QString& content)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
        QTextStream out(&f);
        out << content;
    }

    QJsonObject readJsonConfig(const QString& path)
    {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QJsonDocument::fromJson(f.readAll()).object();
    }

private Q_SLOTS:

    /// v1→v2 animation migration: the five legacy per-field keys
    /// (AnimationDuration, AnimationEasingCurve, AnimationMinDistance,
    /// AnimationSequenceMode, AnimationStaggerInterval) must fold into
    /// one v2 `Animations/Profile` JSON blob with the original values
    /// preserved. The PR review found that the existing comprehensive
    /// test exercised the migration path but never asserted the blob's
    /// CONTENTS — the most load-bearing assertion was missing.
    void testV1AnimationMigration_foldsIntoProfileBlob()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Animations]\n"
                                    "AnimationsEnabled=true\n"
                                    "AnimationDuration=200\n"
                                    "AnimationEasingCurve=easeOutCubic\n"
                                    "AnimationMinDistance=10\n"
                                    "AnimationSequenceMode=0\n"
                                    "AnimationStaggerInterval=25\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        const QJsonObject animations = root.value(QStringLiteral("Animations")).toObject();
        QVERIFY(animations.contains(QStringLiteral("Enabled")));
        QCOMPARE(animations.value(QStringLiteral("Enabled")).toBool(), true);

        // The migration writes the Profile blob as a STRING containing a
        // compact JSON object, even though the live schema stores the
        // profile as a nested object (settings.cpp reads it via
        // readProfileObject as a QVariantMap). The stringified form is a
        // deliberate migration artifact — it keeps Animations/Profile a
        // single scalar leaf for the schema/migration cross-check (see the
        // rationale comment at the profile write in migrateV1ToV2,
        // configmigration.cpp) — and the Store's legacy-string fallback
        // parses it on first load, normalising to a nested object on the
        // next save.
        const QString profileString = animations.value(QStringLiteral("Profile")).toString();
        QVERIFY2(!profileString.isEmpty(), "Migration did not produce Animations/Profile blob from v1 per-field keys");
        const QJsonDocument profileDoc = QJsonDocument::fromJson(profileString.toUtf8());
        QVERIFY2(profileDoc.isObject(), "Animations/Profile must be a JSON object string");
        const QJsonObject profile = profileDoc.object();

        // Every v1 value MUST be preserved through to the v2 blob,
        // EXCEPT for unresolved curve specs which are intentionally
        // dropped (see the "curve" assertion below).
        //
        // Curves are stored in their WIRE FORMAT — the migration runs
        // the v1 curve string through a stack-local CurveRegistry. If
        // the registry resolves the input, `Curve::toString()` is
        // stored (canonical form). If it does not resolve — as with
        // the v1 friendly name "easeOutCubic", which the CurveRegistry
        // has no factory for — the migration DROPS the field rather
        // than writing it through verbatim. Persisting an unresolved
        // spec would make `Profile::fromJson` emit the "curve spec …
        // did not resolve" warning on every daemon start forever;
        // dropping lets the library-default OutCubic apply silently
        // and matches the "no backwards compat" spirit of decision S.
        // The canonicalisation happy-path is exercised in
        // `testV1AnimationMigration_canonicalisesKnownCurve` below.
        QCOMPARE(profile.value(QStringLiteral("duration")).toDouble(-1.0), 200.0);
        QVERIFY2(!profile.contains(QStringLiteral("curve")),
                 "Unresolved v1 curve spec must be dropped, not written verbatim into the v2 blob");
        QCOMPARE(profile.value(QStringLiteral("minDistance")).toInt(-1), 10);
        QCOMPARE(profile.value(QStringLiteral("sequenceMode")).toInt(-1), 0);
        QCOMPARE(profile.value(QStringLiteral("staggerInterval")).toInt(-1), 25);

        // The v1 per-field keys must be gone from the v2 tree — we
        // migrate into the blob, not alongside it (no write-once-
        // test-forever shadow complexity).
        QVERIFY(!animations.contains(QStringLiteral("AnimationDuration")));
        QVERIFY(!animations.contains(QStringLiteral("AnimationEasingCurve")));
        QVERIFY(!animations.contains(QStringLiteral("AnimationMinDistance")));
        QVERIFY(!animations.contains(QStringLiteral("AnimationSequenceMode")));
        QVERIFY(!animations.contains(QStringLiteral("AnimationStaggerInterval")));
    }

    /// v1→v2 animation migration MUST canonicalise the curve spec
    /// through `CurveRegistry` when the input resolves to a known
    /// factory. Without this, a settings-UI edit after migration would
    /// re-serialise the curve via `Easing::toString()` (which always
    /// emits canonical wire form) and produce a spurious config rewrite
    /// on first interaction — even though the user made no change.
    ///
    /// Inputs exercised:
    ///   - A cubic-bezier wire-form with non-canonical precision
    ///     ("0.25,0.1,0.25,1.0") → canonicalises to "0.25,0.10,0.25,1.00"
    ///     (two decimal places per `Easing::toString`).
    ///   - A spring wire-form ("spring:14.0,0.6") → canonicalises to
    ///     the same spec (spring wire form is already canonical) —
    ///     proves spring inputs don't get corrupted by the registry
    ///     round-trip either.
    void testV1AnimationMigration_canonicalisesKnownCurve()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Animations]\n"
                                    "AnimationDuration=200\n"
                                    "AnimationEasingCurve=0.25,0.1,0.25,1.0\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        const QJsonObject animations = root.value(QStringLiteral("Animations")).toObject();
        const QString profileString = animations.value(QStringLiteral("Profile")).toString();
        QVERIFY(!profileString.isEmpty());

        const QJsonObject profile = QJsonDocument::fromJson(profileString.toUtf8()).object();
        const QString storedCurve = profile.value(QStringLiteral("curve")).toString();

        // The stored curve MUST be the canonical wire form with two
        // decimal places per component — not the original input string.
        QCOMPARE(storedCurve, QStringLiteral("0.25,0.10,0.25,1.00"));
    }

    /// Companion to `_canonicalisesKnownCurve`: a spring spec round-trips
    /// through CurveRegistry without corruption. Spring's canonical form
    /// uses two-decimal precision on both omega and zeta.
    void testV1AnimationMigration_canonicalisesSpringCurve()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Animations]\n"
                                    "AnimationDuration=250\n"
                                    "AnimationEasingCurve=spring:14.0,0.6\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        const QJsonObject animations = root.value(QStringLiteral("Animations")).toObject();
        const QString profileString = animations.value(QStringLiteral("Profile")).toString();
        QVERIFY(!profileString.isEmpty());

        const QJsonObject profile = QJsonDocument::fromJson(profileString.toUtf8()).object();
        const QString storedCurve = profile.value(QStringLiteral("curve")).toString();

        // Spring::toString uses two-decimal precision.
        QCOMPARE(storedCurve, QStringLiteral("spring:14.00,0.60"));
    }

    /// v1 config missing all the animation keys — v2 result should
    /// contain NO Profile blob (or an empty one). No crash, no
    /// fabricated defaults. The daemon's fan-out layer will then see
    /// a default-constructed Profile at runtime and publish library
    /// defaults for each well-known path.
    void testV1AnimationMigration_missingKeysYieldNoProfileBlob()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Animations]\n"
                                    "AnimationsEnabled=true\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        const QJsonObject animations = root.value(QStringLiteral("Animations")).toObject();
        QCOMPARE(animations.value(QStringLiteral("Enabled")).toBool(), true);
        // Blob is absent OR an empty object-string — either shape is
        // acceptable as "no stored override"; both parse through
        // Profile::fromJson to a default-constructed Profile.
        const QString profileString = animations.value(QStringLiteral("Profile")).toString();
        if (!profileString.isEmpty()) {
            const QJsonDocument doc = QJsonDocument::fromJson(profileString.toUtf8());
            QVERIFY(doc.isObject());
            QVERIFY(doc.object().isEmpty());
        }
    }

    // =========================================================================
    // PR #344 senior review: partial customization + clamping + forward-compat
    // =========================================================================

    /// A v1 config with ONLY `AnimationDuration=200` (no curve, no
    /// minDistance, etc.) migrates to a v2 Profile blob that contains
    /// ONLY the duration field — the other fields are absent, so
    /// `Profile::fromJson` leaves them unset and `effective*` substitutes
    /// library defaults. The migration must NOT fabricate defaults for
    /// unset v1 keys.
    void testV1AnimationMigration_partialCustomization()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Animations]\n"
                                    "AnimationDuration=200\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        const QJsonObject animations = root.value(QStringLiteral("Animations")).toObject();
        const QString profileString = animations.value(QStringLiteral("Profile")).toString();
        QVERIFY2(!profileString.isEmpty(),
                 "Migration did not produce a Profile blob for a single-field v1 customisation");
        const QJsonObject profile = QJsonDocument::fromJson(profileString.toUtf8()).object();

        // The only set field is duration; every other Profile field must
        // be absent from the blob.
        QCOMPARE(profile.value(QStringLiteral("duration")).toInt(-1), 200);
        QVERIFY(!profile.contains(QStringLiteral("curve")));
        QVERIFY(!profile.contains(QStringLiteral("minDistance")));
        QVERIFY(!profile.contains(QStringLiteral("sequenceMode")));
        QVERIFY(!profile.contains(QStringLiteral("staggerInterval")));
    }

    /// v1 values outside the defined Min/Max range must be clamped to
    /// the range during migration. A v1 config with, say, a negative
    /// duration silently gets rejected by `Profile::fromJson` at load
    /// time — producing a broken-looking UI (user's saved custom value
    /// silently reverts to library default) plus a noisy warning on
    /// every daemon start. Clamping at migration time keeps the user's
    /// intent as close as the range allows and avoids the warnings.
    ///
    /// SequenceMode is a closed enum — out-of-range values snap to the
    /// project default (ConfigDefaults::animationSequenceMode()) rather
    /// than the nearest bound, to avoid silently aliasing e.g. 999 onto
    /// a semantically-unrelated mode.
    void testV1AnimationMigration_clampsOutOfRange()
    {
        IsolatedConfigGuard guard;
        writeIniFile(ConfigDefaults::legacyConfigFilePath(),
                     QStringLiteral("[Animations]\n"
                                    "AnimationDuration=-50\n"
                                    "AnimationMinDistance=-10\n"
                                    "AnimationSequenceMode=999\n"
                                    "AnimationStaggerInterval=600000\n"));

        QVERIFY(ConfigMigration::ensureJsonConfig());

        const QJsonObject root = readJsonConfig(ConfigDefaults::configFilePath());
        const QJsonObject animations = root.value(QStringLiteral("Animations")).toObject();
        const QString profileString = animations.value(QStringLiteral("Profile")).toString();
        QVERIFY(!profileString.isEmpty());
        const QJsonObject profile = QJsonDocument::fromJson(profileString.toUtf8()).object();

        // Duration: below-min → clamp to min.
        const int durationStored = profile.value(QStringLiteral("duration")).toInt(-1);
        QVERIFY2(durationStored >= ConfigDefaults::animationDurationMin(),
                 qPrintable(QStringLiteral("Duration stored=%1 but Min=%2")
                                .arg(durationStored)
                                .arg(ConfigDefaults::animationDurationMin())));
        QVERIFY(durationStored <= ConfigDefaults::animationDurationMax());
        QCOMPARE(durationStored, ConfigDefaults::animationDurationMin());

        // MinDistance: below-min → clamp to min (0 by project default).
        const int minDistanceStored = profile.value(QStringLiteral("minDistance")).toInt(-1);
        QCOMPARE(minDistanceStored, ConfigDefaults::animationMinDistanceMin());

        // SequenceMode: 999 is outside the enum range → snap to default.
        const int seqModeStored = profile.value(QStringLiteral("sequenceMode")).toInt(-1);
        QCOMPARE(seqModeStored, ConfigDefaults::animationSequenceMode());

        // StaggerInterval: above-max → clamp to max.
        const int staggerStored = profile.value(QStringLiteral("staggerInterval")).toInt(-1);
        QCOMPARE(staggerStored, ConfigDefaults::animationStaggerIntervalMax());
    }

    /// Regression guard for v2-already-stamped Animations idempotency.
    /// A config already at the current schema version must not re-run
    /// `migrateV1ToV2` — the per-field v1 keys are already gone and the
    /// `Animations.Profile` blob is the canonical v2 shape. Re-running
    /// the migration would either (a) stamp an empty Profile blob
    /// (because no v1 keys are present) or (b) double-process the
    /// existing blob (if a future bug routed it through the assembly
    /// path). Either case corrupts the user's persisted profile.
    ///
    /// The contract under test: `ensureJsonConfig` on a v2-stamped
    /// config containing a non-default `Animations.Profile` blob is a
    /// pure no-op — the on-disk bytes are byte-identical before and
    /// after.
    void testV2AlreadyStamped_AnimationsBlobIdempotent()
    {
        IsolatedConfigGuard guard;

        // Build a v2-stamped config with a non-default Animations.Profile
        // blob. The blob shape mirrors what `ConfigDefaults::animationProfile`
        // would produce — but with custom values so a re-processing path
        // would silently overwrite them with defaults and fail the
        // byte-identical comparison.
        QJsonObject profileBlob;
        profileBlob[QStringLiteral("duration")] = 275;
        profileBlob[QStringLiteral("minDistance")] = 8;
        profileBlob[QStringLiteral("sequenceMode")] = 1;
        profileBlob[QStringLiteral("staggerInterval")] = 42;
        profileBlob[QStringLiteral("curve")] = QStringLiteral("0.42,0.00,0.58,1.00");
        const QString profileString = QString::fromUtf8(QJsonDocument(profileBlob).toJson(QJsonDocument::Compact));

        QJsonObject animations;
        animations[QStringLiteral("Enabled")] = true;
        animations[QStringLiteral("Profile")] = profileString;

        QJsonObject root;
        root[QStringLiteral("_version")] = PlasmaZones::ConfigSchemaVersion;
        root[QStringLiteral("Animations")] = animations;

        QDir().mkpath(QFileInfo(ConfigDefaults::configFilePath()).absolutePath());
        QVERIFY(PhosphorConfig::JsonBackend::writeJsonAtomically(ConfigDefaults::configFilePath(), root));

        // Snapshot bytes BEFORE migration call.
        QFile fileBefore(ConfigDefaults::configFilePath());
        QVERIFY(fileBefore.open(QIODevice::ReadOnly));
        const QByteArray beforeBytes = fileBefore.readAll();
        fileBefore.close();
        QVERIFY(!beforeBytes.isEmpty());

        // Run ensureJsonConfig — already at current version, must be a
        // pure no-op.
        QVERIFY(ConfigMigration::ensureJsonConfig());

        // Snapshot bytes AFTER migration call. Must be byte-identical.
        QFile fileAfter(ConfigDefaults::configFilePath());
        QVERIFY(fileAfter.open(QIODevice::ReadOnly));
        const QByteArray afterBytes = fileAfter.readAll();
        fileAfter.close();

        QCOMPARE(afterBytes, beforeBytes);

        // Belt-and-suspenders: parse and verify the Profile blob is
        // byte-equal to what we wrote (no key reorder, no whitespace
        // shuffle, no value coercion). The byte comparison above
        // already pins this; the structured check makes a regression
        // diff readable.
        const QJsonObject afterRoot = readJsonConfig(ConfigDefaults::configFilePath());
        QCOMPARE(afterRoot.value(QStringLiteral("_version")).toInt(), PlasmaZones::ConfigSchemaVersion);
        const QJsonObject afterAnimations = afterRoot.value(QStringLiteral("Animations")).toObject();
        QCOMPARE(afterAnimations.value(QStringLiteral("Enabled")).toBool(), true);
        QCOMPARE(afterAnimations.value(QStringLiteral("Profile")).toString(), profileString);
    }
};

QTEST_MAIN(TestMigrationV1Animation)
#include "test_migration_v1_animation.moc"
