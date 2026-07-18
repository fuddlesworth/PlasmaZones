// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_settings_animation_profile.cpp
 * @brief Settings — animation Profile JSON-blob storage & per-field signals.
 *
 * Pinned behaviour:
 *   - Full round-trip of every Profile field (curve, duration, minDistance,
 *     sequenceMode, staggerInterval)
 *   - Malformed-blob → library defaults; missing-blob → project defaults
 *   - Per-field signal discrimination (only the changed field's NOTIFY fires)
 *   - Aggregate setAnimationProfile is no-op when blob round-trips identical
 *   - setAnimationProfile MERGES into the stored object (preserves unknown
 *     fields, doesn't replace)
 *   - Unresolved curve specs round-trip verbatim
 */

#include <QSignalSpy>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <PhosphorAnimation/Profile.h>

#include "config/configbackends.h"
#include "config/configdefaults.h"
#include "config/settings.h"
#include "../helpers/IsolatedConfigGuard.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

class TestSettingsAnimationProfile : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    /// Two writers over one config file (settings app + daemon in
    /// production): a composite-blob write through a STALE but CLEAN
    /// backend must not resurrect old sibling fields over a value the
    /// other writer already committed. Encodes the live #795 repro:
    /// writer A saves duration=2000; writer B, whose cached document
    /// predates that save, patches sequenceMode; the duration must
    /// survive on disk because B's setter refreshes its clean backend
    /// from disk before the read-modify-write.
    void testAnimationProfile_staleCleanWriterDoesNotClobberSibling()
    {
        IsolatedConfigGuard guard;
        {
            // Establish the config file (runs migrations/stamping) so the
            // later instances start from a committed, clean state.
            Settings init;
            init.save();
        }

        Settings writerB; // caches the pre-change document

        {
            Settings writerA;
            writerA.setAnimationDuration(2000);
            writerA.save();
        }

        // 0 is the non-default value (schema default is 1) — a default
        // write would no-op in the setter and never exercise the blob RMW.
        writerB.setAnimationSequenceMode(0);
        writerB.save();

        Settings reloaded;
        QCOMPARE(reloaded.animationDuration(), 2000);
        QCOMPARE(reloaded.animationSequenceMode(), 0);
    }

    /// Every Profile field (curve, duration, minDistance, sequenceMode,
    /// staggerInterval) must round-trip through save → reload with the
    /// original value intact.
    void testAnimationProfile_allFieldsRoundTrip()
    {
        IsolatedConfigGuard guard;
        // Curve is stored/round-tripped as its WIRE FORMAT (e.g., a
        // cubic-bezier is serialised as "x1,y1,x2,y2"), not as the
        // caller's input string. CurveRegistry::create parses both the
        // wire format AND friendly names, but the on-disk form is
        // always the wire string. Use a valid wire-format string here
        // so the round-trip assertion is meaningful.
        const QString curveWire = QStringLiteral("0.25,0.75,0.75,0.25");
        {
            Settings settings;
            settings.setAnimationDuration(275);
            settings.setAnimationEasingCurve(curveWire);
            settings.setAnimationMinDistance(9);
            settings.setAnimationSequenceMode(1);
            settings.setAnimationStaggerInterval(42);
            settings.save();
        }

        Settings reloaded;
        QCOMPARE(reloaded.animationDuration(), 275);
        QCOMPARE(reloaded.animationEasingCurve(), curveWire);
        QCOMPARE(reloaded.animationMinDistance(), 9);
        QCOMPARE(reloaded.animationSequenceMode(), 1);
        QCOMPARE(reloaded.animationStaggerInterval(), 42);
    }

    /// A malformed Profile blob on disk must not crash the loader; the
    /// getters must fall back to library defaults. Pre-PR-344 review:
    /// no test exercised the malformed path.
    void testAnimationProfile_malformedBlobFallsBackToDefaults()
    {
        IsolatedConfigGuard guard;
        {
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto animations = backend->group(ConfigDefaults::animationsGroup());
            animations->writeString(ConfigDefaults::animationProfileKey(),
                                    QStringLiteral("{definitely not valid json"));
            animations.reset();
            backend->sync();
        }

        Settings settings;
        // Effective* values kick in — library defaults per Profile.h.
        QCOMPARE(settings.animationDuration(), qRound(PhosphorAnimation::Profile::DefaultDuration));
        QCOMPARE(settings.animationMinDistance(), PhosphorAnimation::Profile::DefaultMinDistance);
        QCOMPARE(settings.animationSequenceMode(), static_cast<int>(PhosphorAnimation::Profile::DefaultSequenceMode));
        QCOMPARE(settings.animationStaggerInterval(), PhosphorAnimation::Profile::DefaultStaggerInterval);
    }

    /// An absent Profile blob on disk — the PhosphorConfig schema
    /// substitutes the PROJECT-wide default built by
    /// `ConfigDefaults::animationProfile()` (which composes per-field
    /// defaults like `animationDuration() == 320` into a pre-serialised
    /// blob). Contrast with the malformed-blob path above, which falls
    /// through to LIBRARY defaults from `Profile::DefaultDuration`
    /// because the schema hand-off happens before the JSON parse gate.
    void testAnimationProfile_missingBlobUsesProjectDefault()
    {
        IsolatedConfigGuard guard;
        // No blob written — schema's animationProfile() default kicks in.
        Settings settings;
        QCOMPARE(settings.animationDuration(), ConfigDefaults::animationDuration());
        QCOMPARE(settings.animationMinDistance(), ConfigDefaults::animationMinDistance());
    }

    /// Setting the Profile blob to a byte-identical value is a no-op.
    /// Guards against the signal-storm problem reported in the PR
    /// review — a slider drag at 30 Hz writing the same value should
    /// not fire 7 signals per tick.
    void testAnimationProfile_sameBlobNoSignal()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        settings.setAnimationDuration(200);
        QSignalSpy profileSpy(&settings, &Settings::animationProfileChanged);
        QSignalSpy durationSpy(&settings, &Settings::animationDurationChanged);
        QSignalSpy curveSpy(&settings, &Settings::animationEasingCurveChanged);
        QSignalSpy settingsSpy(&settings, &Settings::settingsChanged);

        settings.setAnimationDuration(200); // same value
        QCOMPARE(profileSpy.count(), 0);
        QCOMPARE(durationSpy.count(), 0);
        QCOMPARE(curveSpy.count(), 0);
        QCOMPARE(settingsSpy.count(), 0);
    }

    /// Per-field signal discrimination: changing ONLY duration must
    /// fire animationDurationChanged but NOT the other four per-field
    /// signals. Pre-PR-344 review: setAnimationProfile fired all 7
    /// unconditionally.
    void testAnimationProfile_perFieldSignalsDiscriminate()
    {
        IsolatedConfigGuard guard;
        Settings settings;
        settings.setAnimationDuration(200);
        settings.setAnimationEasingCurve(QStringLiteral("easeOutCubic"));
        settings.setAnimationMinDistance(5);
        settings.setAnimationSequenceMode(0);
        settings.setAnimationStaggerInterval(30);

        QSignalSpy durationSpy(&settings, &Settings::animationDurationChanged);
        QSignalSpy curveSpy(&settings, &Settings::animationEasingCurveChanged);
        QSignalSpy minDistSpy(&settings, &Settings::animationMinDistanceChanged);
        QSignalSpy seqModeSpy(&settings, &Settings::animationSequenceModeChanged);
        QSignalSpy staggerSpy(&settings, &Settings::animationStaggerIntervalChanged);

        // Change ONLY duration via the per-field setter.
        settings.setAnimationDuration(400);
        QCOMPARE(durationSpy.count(), 1);
        QCOMPARE(curveSpy.count(), 0);
        QCOMPARE(minDistSpy.count(), 0);
        QCOMPARE(seqModeSpy.count(), 0);
        QCOMPARE(staggerSpy.count(), 0);
    }

    /// Save → load → save round-trip must produce a byte-identical
    /// Profile blob. Absent this guarantee, `QJsonDocument::Compact`'s
    /// hash-bucket key order can drift between writes (same values,
    /// different serialisation), producing config-file churn on every
    /// daemon restart and spurious `animationProfileChanged` emissions.
    /// Pre-PR-344-review setAnimationProfile compared serialised
    /// strings rather than semantic Profile values — this test
    /// defended against the reverse regression where the on-disk
    /// serialisation itself drifts.
    void testAnimationProfile_saveLoadSaveByteIdentical()
    {
        IsolatedConfigGuard guard;
        const QString curveWire = QStringLiteral("0.25,0.75,0.75,0.25");

        // First session: write a Profile blob, save, capture the
        // stored bytes.
        QString firstBlob;
        {
            Settings settings;
            settings.setAnimationDuration(275);
            settings.setAnimationEasingCurve(curveWire);
            settings.setAnimationMinDistance(9);
            settings.setAnimationSequenceMode(1);
            settings.setAnimationStaggerInterval(42);
            settings.save();

            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto animations = backend->group(ConfigDefaults::animationsGroup());
            firstBlob = animations->readString(ConfigDefaults::animationProfileKey());
        }
        QVERIFY(!firstBlob.isEmpty());

        // Second session: load, re-save without mutating, capture the
        // stored bytes. Must match the first session exactly.
        QString secondBlob;
        {
            Settings settings;
            settings.save();

            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto animations = backend->group(ConfigDefaults::animationsGroup());
            secondBlob = animations->readString(ConfigDefaults::animationProfileKey());
        }
        QCOMPARE(secondBlob, firstBlob);
    }

    /// An unresolved curve spec (user plugin curve not yet registered,
    /// hand-edited typo) MUST round-trip through the setter/getter —
    /// the caller's string is preserved verbatim rather than silently
    /// reverting to the default. The runtime still gracefully falls
    /// back to the library default at animation time; persisting the
    /// raw string means the user's edit survives restarts and resolves
    /// cleanly once a matching curve becomes available.
    ///
    /// Regression guard for the M3 review finding — the previous setter
    /// rejected unresolved specs outright, which broke QML two-way
    /// bindings (UI would show the user's pick, config on disk would
    /// silently revert to the prior value).
    void testAnimationEasingCurve_unresolvedSpecRoundTrips()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        // "some-plugin-curve" is not a built-in CurveRegistry typeId and
        // is not in any file the default fallback registry knows about.
        // Must still round-trip intact.
        const QString userSpec = QStringLiteral("some-plugin-curve");
        settings.setAnimationEasingCurve(userSpec);
        QCOMPARE(settings.animationEasingCurve(), userSpec);

        // Per-field mutation on a DIFFERENT field must not clobber the
        // unresolved curve spec — the numeric setters patch the blob
        // in place rather than round-tripping through Profile::toJson,
        // which would otherwise drop the null-curve field.
        settings.setAnimationDuration(275);
        QCOMPARE(settings.animationEasingCurve(), userSpec);
        QCOMPARE(settings.animationDuration(), 275);
    }

    // =========================================================================
    // PR #344 senior review: aggregate setter semantics
    // =========================================================================
    //
    // The aggregate `setAnimationProfile(const Profile&)` setter is the
    // other path to writing the Profile blob (complementing the per-field
    // setters). Four properties the byte-level equality rewrite must
    // preserve:
    //   1. No-op when the incoming Profile serialises to the same blob
    //      (zero signal emissions, zero disk writes).
    //   2. Changing only one field emits the aggregate signal + that
    //      field's signal, but NOT the other per-field signals.
    //   3. Unknown disk fields (plugin-authored extensions, future
    //      schema keys) survive a call to `setAnimationProfile` — the
    //      setter must MERGE into the stored object, not replace it.
    //   4. A round-trip where the blob on disk references an unresolved
    //      curve (plugin curve not yet registered): `animationProfile()`
    //      returns a Profile with a null curve, and calling
    //      `setAnimationProfile` with that Profile must NOT rewrite the
    //      blob — the caller wasn't trying to change anything.

    /// Calling `setAnimationProfile(p)` twice with the same Profile must
    /// fire zero signals on the second call.
    void testAnimationProfile_setSameProfileNoSignal()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        // Build a Profile via the per-field setters so the resulting
        // blob is populated with real values (a default-constructed
        // Profile serialises to `{}` which is a legitimate "same as
        // before" blob but masks the byte-compare logic).
        settings.setAnimationDuration(275);
        settings.setAnimationMinDistance(7);
        settings.setAnimationSequenceMode(1);
        settings.setAnimationStaggerInterval(42);

        const PhosphorAnimation::Profile p = settings.animationProfile();

        // First aggregate-setter call may or may not change state — it
        // is the SECOND identical call that must be the no-op.
        settings.setAnimationProfile(p);

        QSignalSpy profileSpy(&settings, &Settings::animationProfileChanged);
        QSignalSpy durationSpy(&settings, &Settings::animationDurationChanged);
        QSignalSpy curveSpy(&settings, &Settings::animationEasingCurveChanged);
        QSignalSpy minDistSpy(&settings, &Settings::animationMinDistanceChanged);
        QSignalSpy seqModeSpy(&settings, &Settings::animationSequenceModeChanged);
        QSignalSpy staggerSpy(&settings, &Settings::animationStaggerIntervalChanged);
        QSignalSpy settingsSpy(&settings, &Settings::settingsChanged);

        settings.setAnimationProfile(p);

        QCOMPARE(profileSpy.count(), 0);
        QCOMPARE(durationSpy.count(), 0);
        QCOMPARE(curveSpy.count(), 0);
        QCOMPARE(minDistSpy.count(), 0);
        QCOMPARE(seqModeSpy.count(), 0);
        QCOMPARE(staggerSpy.count(), 0);
        QCOMPARE(settingsSpy.count(), 0);
    }

    /// `setAnimationProfile` with a Profile that differs ONLY in Duration
    /// must fire `animationProfileChanged` + `animationDurationChanged`
    /// and NOT the other per-field signals.
    void testAnimationProfile_setDurationOnlyEmitsSpecificSignals()
    {
        IsolatedConfigGuard guard;
        Settings settings;

        // Seed a concrete Profile via per-field setters so we have a
        // known baseline to diff against.
        settings.setAnimationDuration(275);
        settings.setAnimationMinDistance(7);
        settings.setAnimationSequenceMode(1);
        settings.setAnimationStaggerInterval(42);

        // Capture the baseline, then construct a Profile identical
        // EXCEPT for duration.
        PhosphorAnimation::Profile p = settings.animationProfile();
        p.duration = 400.0;

        QSignalSpy profileSpy(&settings, &Settings::animationProfileChanged);
        QSignalSpy durationSpy(&settings, &Settings::animationDurationChanged);
        QSignalSpy curveSpy(&settings, &Settings::animationEasingCurveChanged);
        QSignalSpy minDistSpy(&settings, &Settings::animationMinDistanceChanged);
        QSignalSpy seqModeSpy(&settings, &Settings::animationSequenceModeChanged);
        QSignalSpy staggerSpy(&settings, &Settings::animationStaggerIntervalChanged);

        settings.setAnimationProfile(p);

        QCOMPARE(profileSpy.count(), 1);
        QCOMPARE(durationSpy.count(), 1);
        // The other per-field signals must NOT fire — they describe
        // semantic observables that did not change.
        QCOMPARE(curveSpy.count(), 0);
        QCOMPARE(minDistSpy.count(), 0);
        QCOMPARE(seqModeSpy.count(), 0);
        QCOMPARE(staggerSpy.count(), 0);
    }

    /// Unknown fields on disk (future schema extensions, plugin-authored
    /// custom Profile keys) must survive a call to `setAnimationProfile`.
    /// The aggregate setter merges into the stored blob rather than
    /// replacing it — matches the per-field setters' merge semantics.
    void testAnimationProfile_preservesUnknownDiskFields()
    {
        IsolatedConfigGuard guard;

        // Seed the disk with a blob containing BOTH a known field
        // (duration=200) and an unknown field (_pluginField).
        {
            QJsonObject seedBlob;
            seedBlob.insert(QLatin1String("duration"), 200);
            seedBlob.insert(QLatin1String("_pluginField"), QLatin1String("custom"));
            const QString seedString = QString::fromUtf8(QJsonDocument(seedBlob).toJson(QJsonDocument::Compact));

            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto animations = backend->group(ConfigDefaults::animationsGroup());
            animations->writeString(ConfigDefaults::animationProfileKey(), seedString);
            animations.reset();
            backend->sync();
        }

        Settings settings;

        // Build an incoming Profile with a different duration; the
        // unknown field must survive.
        PhosphorAnimation::Profile p;
        p.duration = 300.0;
        settings.setAnimationProfile(p);
        settings.save();

        // Read disk directly — we want to see the stored bytes, not
        // what `Profile::fromJson` chooses to surface.
        auto backend = PlasmaZones::createDefaultConfigBackend();
        auto animations = backend->group(ConfigDefaults::animationsGroup());
        const QString blobString = animations->readString(ConfigDefaults::animationProfileKey());
        QVERIFY(!blobString.isEmpty());
        const QJsonObject blob = QJsonDocument::fromJson(blobString.toUtf8()).object();

        QCOMPARE(blob.value(QLatin1String("duration")).toInt(), 300);
        QVERIFY2(blob.contains(QLatin1String("_pluginField")),
                 "setAnimationProfile wiped the unknown '_pluginField' — aggregate setter must merge, not replace");
        QCOMPARE(blob.value(QLatin1String("_pluginField")).toString(), QStringLiteral("custom"));
    }

    /// Disk blob contains an unresolved curve reference (plugin curve
    /// that hasn't been registered yet in this process). Calling
    /// `setAnimationProfile` with the Profile produced by the round-trip
    /// of that same blob must NOT rewrite the blob — the caller wasn't
    /// trying to change anything. Before the byte-compare fix, the
    /// semantic `Profile::operator==` check would compare two null
    /// curves as equal and short-circuit (data-loss hazard); or — if
    /// flipped to write-through on any inequality — fire on every call
    /// (signal storm).
    void testAnimationProfile_unresolvedCurveBlobSameNotWritten()
    {
        IsolatedConfigGuard guard;

        // Seed disk with a blob whose curve field references a curve
        // name that no registered factory in this process knows about.
        // Build via `QJsonDocument::toJson(Compact)` so the on-disk
        // byte-string matches the shape the setter emits when it
        // re-serialises (QJsonObject sorts keys alphabetically) — the
        // byte-compare short-circuit relies on round-trip stability.
        {
            QJsonObject seed;
            seed.insert(QLatin1String("duration"), 275);
            seed.insert(QLatin1String("curve"), QLatin1String("nonexistent-plugin-curve"));
            seed.insert(QLatin1String("minDistance"), 7);
            const QString seedBlob = QString::fromUtf8(QJsonDocument(seed).toJson(QJsonDocument::Compact));
            auto backend = PlasmaZones::createDefaultConfigBackend();
            auto animations = backend->group(ConfigDefaults::animationsGroup());
            animations->writeString(ConfigDefaults::animationProfileKey(), seedBlob);
            animations.reset();
            backend->sync();
        }

        Settings settings;

        // Round-trip: read the Profile via the getter, then write it
        // back unchanged. The getter resolves the curve through
        // CurveRegistry::tryCreate — which fails for the unknown name
        // and leaves `curve == nullptr` on the resulting Profile.
        // Writing that Profile back must not mutate the on-disk bytes:
        // the merge overlays only the fields the incoming Profile
        // emitted, and the null curve is NOT emitted — the stored
        // raw curve string is untouched.
        const PhosphorAnimation::Profile p = settings.animationProfile();

        QSignalSpy profileSpy(&settings, &Settings::animationProfileChanged);
        settings.setAnimationProfile(p);

        // Zero signals — merged bytes match disk bytes.
        QCOMPARE(profileSpy.count(), 0);

        // And the unresolved curve string is still on disk.
        auto backend = PlasmaZones::createDefaultConfigBackend();
        auto animations = backend->group(ConfigDefaults::animationsGroup());
        const QString readBack = animations->readString(ConfigDefaults::animationProfileKey());
        const QJsonObject readObj = QJsonDocument::fromJson(readBack.toUtf8()).object();
        QCOMPARE(readObj.value(QLatin1String("curve")).toString(), QStringLiteral("nonexistent-plugin-curve"));
        QCOMPARE(readObj.value(QLatin1String("duration")).toInt(), 275);
        QCOMPARE(readObj.value(QLatin1String("minDistance")).toInt(), 7);
    }
};

QTEST_MAIN(TestSettingsAnimationProfile)
#include "test_settings_animation_profile.moc"
