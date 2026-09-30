// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The surface validator's remaining metadata lints, one negative slot each.
//
// WHY A SECOND FILE. test_surface_pack_validator.cpp covers the arm's binding
// lints, its preset arm, its buffer-scale arm and its stage bakes, and adding
// these to it would push it past the file-size ceiling. The cut is by COVERAGE
// GAP rather than by subject, so this file is the answer to one question: which
// lints could be deleted with the suite still green? Each slot below existed only
// as a line of validator code until it was written.
//
// The fixture writers are shared (packvalidatortesthelpers.h) rather than copied,
// because two copies of a fixture writer is how two test files start disagreeing
// about what a valid pack looks like.
//
// EVERY SLOT IS A NEGATIVE, and they lean on the clean-pack positive control in
// the sibling file: a lint that fired on EVERYTHING would satisfy all of these
// and fail that one. Where a lint has a near-miss worth pinning the slot carries
// its own quiet control too, because the shared clean pack cannot express those: a
// value just inside the bound (the at-cap texture re-run, the in-range bufferScale),
// and an explicit `false` or empty string where the lint fires on `true`. The
// correctly-spelled-token control is NOT one of them — it needs a backdrop flag and a
// radius parameter to come out clean, so it lives in the blur-chain file instead.

#include <QtTest>

#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <PhosphorSurface/SurfaceShaderContract.h>
#include <PhosphorSurface/SurfaceShaderEffect.h>

#include "packvalidatortesthelpers.h"

using namespace PackValidatorTest;

class TestSurfacePackLints : public QObject
{
    Q_OBJECT

private:
    /// Write a 1x1 transparent PNG into the pack, for the slots that need a
    /// texture file to EXIST so the missing-texture lint is not what fires.
    static bool writeStubImage(const QString& dir, const QString& file)
    {
        QImage px(1, 1, QImage::Format_RGBA8888);
        px.fill(Qt::transparent);
        return px.save(QDir(dir).filePath(file));
    }

private Q_SLOTS:
    /// An id that is not a GLSL identifier gets no `p_` define, so the pack
    /// compiles and the parameter silently does nothing. Distinct from the
    /// duplicate-id lint the sibling file covers.
    void anInvalidParameterIdIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj =
            surfacePack(QStringLiteral("sf-badid"),
                        QJsonArray{surfaceParam(QStringLiteral("has-a-dash"), QStringLiteral("float"), 1.0, 0.0, 2.0)});
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-badid"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("invalid parameter id")), qPrintable(r.report));
    }

    /// Past the contract's user-texture slot count the surplus is dropped at
    /// load, so the pack renders with fewer textures than it declares. The
    /// count comes from the contract rather than a literal, so the slot cannot
    /// drift from the cap it is testing.
    void tooManyTexturesIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const int cap = PhosphorSurfaceShaders::SurfaceShaderContract::kMaxUserTextureSlots;
        QJsonObject obj = surfacePack(QStringLiteral("sf-manytex"), QJsonArray{});
        QJsonArray textures;
        for (int i = 0; i < cap + 1; ++i) {
            QJsonObject tex;
            tex.insert(QStringLiteral("path"), QStringLiteral("t%1.png").arg(i));
            textures.append(tex);
        }
        obj.insert(QStringLiteral("textures"), textures);
        const QString dir = tmp.filePath(QStringLiteral("sf-manytex"));
        QVERIFY(writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(obj).toJson()));
        for (int i = 0; i < cap + 1; ++i) {
            QVERIFY(writeStubImage(dir, QStringLiteral("t%1.png").arg(i)));
        }
        QVERIFY(writePackFile(dir, QStringLiteral("effect.frag"), surfaceBodyReading({}).toUtf8()));

        QString report;
        QTextStream stream(&report);
        PlasmaZones::ShaderValidate::validateSurfacePack(dir, stream);
        stream.flush();
        QVERIFY2(report.contains(QStringLiteral("too many textures")), qPrintable(report));

        // Exactly AT the cap is silent, so the lint cannot drift down by one.
        QJsonArray atCap;
        for (int i = 0; i < cap; ++i) {
            QJsonObject tex;
            tex.insert(QStringLiteral("path"), QStringLiteral("t%1.png").arg(i));
            atCap.append(tex);
        }
        obj.insert(QStringLiteral("textures"), atCap);
        QVERIFY(writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(obj).toJson()));
        QString okReport;
        QTextStream okStream(&okReport);
        PlasmaZones::ShaderValidate::validateSurfacePack(dir, okStream);
        okStream.flush();
        QVERIFY2(!okReport.contains(QStringLiteral("too many textures")), qPrintable(okReport));
    }

    /// The three ways a texture entry fails to name a usable file. Grouped
    /// because they share one fixture shape and differ only in the path.
    void theTextureEntryPathLintsEachFire()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto runWithTexturePath = [&tmp](const QString& name, const QString& path) {
            QJsonObject tex;
            tex.insert(QStringLiteral("path"), path);
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(QStringLiteral("textures"), QJsonArray{tex});
            return validateSurface(tmp, name, obj, surfaceBodyReading({}));
        };

        // Empty path: dropped at load, which also shifts every later texture
        // down one sampler slot — the consequence the message names.
        const PackResult empty = runWithTexturePath(QStringLiteral("sf-tex-empty"), QString());
        QVERIFY2(empty.report.contains(QStringLiteral("texture entry with empty `path`")), qPrintable(empty.report));

        // Escaping the pack directory: rejected outright, sampler reads
        // transparent.
        const PackResult escape =
            runWithTexturePath(QStringLiteral("sf-tex-escape"), QStringLiteral("../../../etc/passwd"));
        QVERIFY2(escape.report.contains(QStringLiteral("texture path escapes the pack directory")),
                 qPrintable(escape.report));

        // Confined but absent: a typo ships green and fails at first paint.
        const PackResult missing = runWithTexturePath(QStringLiteral("sf-tex-missing"), QStringLiteral("nope.png"));
        QVERIFY2(missing.report.contains(QStringLiteral("texture missing")), qPrintable(missing.report));
    }

    /// The buffer-entry lints that are not about a path escaping, which the
    /// sibling file covers. An empty entry and an unknown builtin token both
    /// fail the pack closed to single-pass, and a pack-local file that is simply
    /// absent does the same.
    void theBufferEntryLintsEachFire()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto runWithBuffers = [&tmp](const QString& name, const QJsonArray& buffers) {
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"), buffers);
            return validateSurface(tmp, name, obj, surfaceBodyReading({}));
        };

        const PackResult empty = runWithBuffers(QStringLiteral("sf-buf-empty"), QJsonArray{QString()});
        QVERIFY2(empty.report.contains(QStringLiteral("empty bufferShaders entry")), qPrintable(empty.report));

        const PackResult unknown =
            runWithBuffers(QStringLiteral("sf-buf-unknown"), QJsonArray{QStringLiteral("builtin:not-a-pass")});
        QVERIFY2(unknown.report.contains(QStringLiteral("unknown or unlocatable builtin buffer shader")),
                 qPrintable(unknown.report));

        const PackResult missing =
            runWithBuffers(QStringLiteral("sf-buf-missing"), QJsonArray{QStringLiteral("pass0.frag")});
        QVERIFY2(missing.report.contains(QStringLiteral("multipass buffer shader missing")),
                 qPrintable(missing.report));
    }

    /// bufferWraps and bufferFilters are POSITIONALLY aligned to bufferShaders,
    /// so a list of the wrong length does not mean "the rest are unset", it means
    /// every later pass reads a neighbour's value. Both keys, because the lint is
    /// generated per key and one could be wired and the other not.
    void aMisalignedBufferArrayLengthIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        for (const QString& key : {QStringLiteral("bufferWraps"), QStringLiteral("bufferFilters")}) {
            QJsonObject obj = surfacePack(QStringLiteral("sf-align"), QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"),
                       QJsonArray{QStringLiteral("builtin:gaussian-h"), QStringLiteral("builtin:gaussian-v")});
            obj.insert(key, QJsonArray{QStringLiteral("clamp")}); // one entry for two passes
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-align"), obj, surfaceBodyReading({}));
            QVERIFY2(r.report.contains(QStringLiteral("%1 has 1 entries for 2 buffer shaders").arg(key)),
                     qPrintable(r.report));
        }
    }

    /// An unrecognised wrap or filter token is cleared to empty at load with only
    /// a journal warning, so the pack renders at the default and the author's
    /// typo is invisible. Distinct from the daemon-only-token lint the sibling
    /// file covers, which is about a token that IS recognised.
    void anUnrecognisedBufferTokenIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-vocab"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{surfaceFillerBufferName()});
        obj.insert(QStringLiteral("bufferWrap"), QStringLiteral("wrap-around"));
        obj.insert(QStringLiteral("bufferFilter"), QStringLiteral("trilinear"));
        const PackResult r =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-vocab"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("wrap-around")), qPrintable(r.report));
        QVERIFY2(r.report.contains(QStringLiteral("trilinear")), qPrintable(r.report));
    }

    /// `bufferFeedback` is DAEMON-ONLY on the surface path, AND ONLY WITH ONE BUFFER PASS:
    /// the daemon ping-pongs a single buffer pass between two targets so it can read its own
    /// previous frame, and every line that implements that sits inside ShaderNodeRhi's
    /// single-buffer branch. The compositor's surface fold never reads the key at all. (The
    /// compositor does honour it on the POINTER path, which is why grepping the effect for
    /// the name is misleading.) So the message has to split: with one pass the two hosts
    /// genuinely diverge, and with more than one the key is inert on BOTH and there is
    /// nothing to look for. The old single message sent an author hunting a
    /// settings-preview difference that could not exist.
    ///
    /// FOUR legs, and the last two are here because of a MUTATION rather than a defect: the
    /// arm's predicate is `.toBool()`, and with only a fires/absent pair a mutation to
    /// `meta.contains(...)` passes both. An explicit false is the state that kills it.
    void aDaemonOnlyBufferFeedbackIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-feedback"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{surfaceFillerBufferName()});
        obj.insert(QStringLiteral("halfFloatBuffers"), false);
        obj.insert(QStringLiteral("bufferFeedback"), true);
        const PackResult r =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-feedback"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("honoured by the DAEMON and ignored by the compositor's "
                                                  "surface fold")),
                 qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("read by NEITHER host")), qPrintable(r.report));
        QCOMPARE(r.errors, 1);

        // TWO passes: the daemon's feedback path is the single-buffer one, so neither host
        // reads the key and the message says so, naming the count it counted. The count is
        // pinned at TWO because appending a gaussian token sets readsRadiusSlot and this
        // fixture declares no scalar parameter, so the blur-radius arm fires alongside —
        // named here rather than left as invisible collateral behind three substring tests.
        obj.insert(QStringLiteral("bufferShaders"),
                   QJsonArray{surfaceFillerBufferName(), QStringLiteral("builtin:gaussian-h")});
        obj.insert(QStringLiteral("needsBackdrop"), true);
        const PackResult many =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-feedback"), obj, surfaceBodyReading({}));
        QVERIFY2(many.report.contains(QStringLiteral("read by NEITHER host")), qPrintable(many.report));
        QVERIFY2(many.report.contains(QStringLiteral("with 2 buffer passes")), qPrintable(many.report));
        QVERIFY2(!many.report.contains(QStringLiteral("own previous frame in the settings preview")),
                 qPrintable(many.report));
        QVERIFY2(many.report.contains(QStringLiteral("blurs by 0")), qPrintable(many.report));
        QCOMPARE(many.errors, 2);

        // The CAP, which nothing pinned: a pack over the pass budget must report the BUDGET,
        // not what it declared, or the std::min could be deleted with the suite green. The
        // over-cap error rides along, hence 2.
        QJsonArray overCap;
        for (int i = 0; i < PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses + 1; ++i) {
            overCap.append(surfaceFillerBufferName());
        }
        obj.insert(QStringLiteral("bufferShaders"), overCap);
        obj.remove(QStringLiteral("needsBackdrop"));
        const PackResult capped =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-feedback"), obj, surfaceBodyReading({}));
        QVERIFY2(capped.report.contains(QStringLiteral("with 8 buffer passes")), qPrintable(capped.report));
        QVERIFY2(!capped.report.contains(QStringLiteral("with 9 buffer passes")), qPrintable(capped.report));
        QCOMPARE(capped.errors, 2);

        // An EMPTY entry beside a live one. What this leg pins is the SUPPRESSION: an empty
        // entry fail-closes the chain, so the feedback line must not print at all, and a version
        // that still printed would describe a mechanism the registry has already disabled.
        //
        // It does NOT pin the empties-in-the-count rule, and no leg can: chainResolves is cleared
        // by exactly the condition that would make the count differ, so both counting rules produce
        // the same silence here. Stated rather than left implied, because the first version of this
        // comment claimed both halves were pinned.
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{surfaceFillerBufferName(), QString()});
        const PackResult withEmpty =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-feedback"), obj, surfaceBodyReading({}));
        QVERIFY2(withEmpty.report.contains(QStringLiteral("empty bufferShaders entry")), qPrintable(withEmpty.report));
        QVERIFY2(!withEmpty.report.contains(QStringLiteral("bufferFeedback")), qPrintable(withEmpty.report));
        QCOMPARE(withEmpty.errors, 1);

        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{surfaceFillerBufferName()});

        // Absent, which is the default and the non-divergent state: silent.
        obj.remove(QStringLiteral("bufferFeedback"));
        const PackResult quiet =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-feedback"), obj, surfaceBodyReading({}));
        QVERIFY2(!quiet.report.contains(QStringLiteral("bufferFeedback")), qPrintable(quiet.report));
        QCOMPARE(quiet.errors, 0);

        // EXPLICIT false. Silent too, and this is the leg that kills the contains() mutation.
        obj.insert(QStringLiteral("bufferFeedback"), false);
        const PackResult off =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-feedback"), obj, surfaceBodyReading({}));
        QVERIFY2(!off.report.contains(QStringLiteral("bufferFeedback")), qPrintable(off.report));
        QCOMPARE(off.errors, 0);
    }

    /// halfFloatBuffers, on an EXPLICIT true only. The divergence is real — the loader reads
    /// the key with toBool(TRUE), so the daemon makes RGBA16F targets while the compositor
    /// makes every one RGBA8 — but a lint on the LOADED value fires on every pack that says
    /// nothing, which is the conforming minimal pack, so it reports a bad default rather
    /// than a bad pack. Narrowing it to a typed-out opt-in is what makes it a lint about the
    /// pack again, and it is the same argument that keeps bufferFeedback.
    ///
    /// THREE legs, because for this key the absent leg is the one that matters: it is the
    /// state a lint on the loaded value would have broken, and pinning its silence is what
    /// stops that lint coming back.
    void anExplicitHalfFloatBuffersOptInIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-halffloat"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{surfaceFillerBufferName()});
        obj.insert(QStringLiteral("halfFloatBuffers"), true);
        const PackResult on =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-halffloat"), obj, surfaceBodyReading({}));
        QVERIFY2(on.report.contains(QStringLiteral("RGBA16F")), qPrintable(on.report));
        QCOMPARE(on.errors, 1);

        obj.insert(QStringLiteral("halfFloatBuffers"), false);
        const PackResult off =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-halffloat"), obj, surfaceBodyReading({}));
        QVERIFY2(!off.report.contains(QStringLiteral("halfFloatBuffers")), qPrintable(off.report));
        QCOMPARE(off.errors, 0);

        // ABSENT. Every bundled chain pack writes the key explicitly, but a third-party pack
        // need not, and this is the leg the withdrawn lint broke all fourteen blur-chain slots on.
        obj.remove(QStringLiteral("halfFloatBuffers"));
        const PackResult absent =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-halffloat"), obj, surfaceBodyReading({}));
        QVERIFY2(!absent.report.contains(QStringLiteral("halfFloatBuffers")), qPrintable(absent.report));
        QCOMPARE(absent.errors, 0);
    }

    /// The scalar and bool buffer keys on a SINGLE-PASS pack, where the registry's
    /// coherence block drops all of them. The array keys beside them already had a slot;
    /// these did not, and a single-pass pack declaring `bufferFeedback: true` validated
    /// clean. The pointer validator lints its own equivalent, which is the precedent.
    void inertBufferKeysOnASinglePassPackAreLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto runWith = [&tmp](const QString& name, const QString& key, const QJsonValue& value) {
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(key, value);
            return validateSurface(tmp, name, obj, surfaceBodyReading({}));
        };

        for (const QString& key :
             {QStringLiteral("bufferFeedback"), QStringLiteral("depthBuffer"), QStringLiteral("halfFloatBuffers")}) {
            const PackResult on = runWith(QStringLiteral("sf-inert"), key, true);
            QVERIFY2(on.report.contains(key + QStringLiteral(" is declared true on a single-pass pack")),
                     qPrintable(on.report));
            // An explicit FALSE is the pack doing nothing wrong, so it must stay quiet — and it
            // is what stops this arm being mutated to a bare contains().
            const PackResult off = runWith(QStringLiteral("sf-inert"), key, false);
            QVERIFY2(!off.report.contains(QStringLiteral("single-pass pack")), qPrintable(off.report));
            QCOMPARE(off.errors, 0);
        }

        const PackResult scale = runWith(QStringLiteral("sf-inert"), QStringLiteral("bufferScale"), 0.5);
        QVERIFY2(scale.report.contains(QStringLiteral("bufferScale is declared on a single-pass pack")),
                 qPrintable(scale.report));
        QCOMPARE(scale.errors, 1);

        // The two SINGULAR string keys, which the array sweep does not reach and which were
        // missing from this one. THREE states, because the sweep is gated on PRESENCE and so
        // distinguishes all three — an earlier version used an empty string AS the absent case,
        // which is what made a mistyped key invisible: the sweep gated on toString(), which is
        // empty for a number or a bool, so `"bufferWrap": 5` slipped past here AND past the
        // multipass non-string arm it never reaches.
        for (const QString& key : {QStringLiteral("bufferWrap"), QStringLiteral("bufferFilter")}) {
            const PackResult on = runWith(QStringLiteral("sf-inert"), key, QStringLiteral("repeat"));
            QVERIFY2(on.report.contains(key + QStringLiteral(" is declared on a single-pass pack")),
                     qPrintable(on.report));
            QCOMPARE(on.errors, 1);

            // An EXPLICIT empty string is still a declaration the author typed, and doubly inert
            // (the loader reads empty as unset), so it reports. This is the leg that pins the
            // presence gate against a regression to a value test.
            const PackResult empty = runWith(QStringLiteral("sf-inert"), key, QString());
            QVERIFY2(empty.report.contains(key + QStringLiteral(" is declared on a single-pass pack")),
                     qPrintable(empty.report));
            QCOMPARE(empty.errors, 1);

            // A MISTYPED value reports too, which a toString() gate could not do.
            const PackResult mistyped = runWith(QStringLiteral("sf-inert"), key, 5);
            QVERIFY2(mistyped.report.contains(key + QStringLiteral(" is declared on a single-pass pack")),
                     qPrintable(mistyped.report));
            QCOMPARE(mistyped.errors, 1);
        }

        // The four keys that were left on a TYPE gate after the singular ones moved to presence.
        // A mistyped one was silent here AND silent at load (fromJson's loop never runs and emits
        // no journal line), so it reached the user with no diagnostic anywhere. One leg per key,
        // with a value of the wrong type for that key.
        for (const QString& key :
             {QStringLiteral("bufferWraps"), QStringLiteral("bufferFilters"), QStringLiteral("bufferScales")}) {
            const PackResult mistyped = runWith(QStringLiteral("sf-inert"), key, QStringLiteral("clamp"));
            QVERIFY2(mistyped.report.contains(key + QStringLiteral(" is declared on a single-pass pack")),
                     qPrintable(mistyped.report));
            QCOMPARE(mistyped.errors, 1);
        }
        const PackResult scaleMistyped =
            runWith(QStringLiteral("sf-inert"), QStringLiteral("bufferScale"), QStringLiteral("0.5"));
        QVERIFY2(scaleMistyped.report.contains(QStringLiteral("bufferScale is declared on a single-pass pack")),
                 qPrintable(scaleMistyped.report));

        // NULL stays quiet on every one of them, which is the tree's convention (the type arms and
        // the array arm both skip null) and the one state where a presence gate and
        // meta.contains() differ. Pinned so the !isNull() term cannot be dropped silently.
        for (const QString& key : {QStringLiteral("bufferWrap"), QStringLiteral("bufferWraps"),
                                   QStringLiteral("bufferScale"), QStringLiteral("bufferFeedback")}) {
            const PackResult nulled = runWith(QStringLiteral("sf-inert"), key, QJsonValue(QJsonValue::Null));
            QVERIFY2(!nulled.report.contains(QStringLiteral("single-pass pack")), qPrintable(nulled.report));
            QCOMPARE(nulled.errors, 0);
        }

        // ABSENT is the only other quiet state, and the clean pack above already covers every other
        // key, so this is the one assertion that the sweep does not fire on a pack that declared
        // nothing.
        const PackResult clean =
            validateSurface(tmp, QStringLiteral("sf-inert-clean"),
                            surfacePack(QStringLiteral("sf-inert-clean"), QJsonArray{}), surfaceBodyReading({}));
        QVERIFY2(!clean.report.contains(QStringLiteral("single-pass pack")), qPrintable(clean.report));
        QCOMPARE(clean.errors, 0);
    }

    /// The daemon-only wrap/filter arm, given the WRONG JSON SHAPE for each spelling. It used to
    /// sniff the shape from the value — string branch, array fallback — which is right for whichever
    /// key it happens to be and wrong for the other, so a plural key given a string and a singular
    /// key given an array both drew "which the DAEMON honours" for a value the loader reads as empty
    /// and honours not at all, printed beside the type lint saying it is ignored.
    void aMistypedDaemonOnlyTokenDoesNotClaimTheDaemonHonoursIt()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto runMultipass = [&tmp](const QString& name, const QString& key, const QJsonValue& value) {
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"), QJsonArray{surfaceFillerBufferName()});
            obj.insert(key, value);
            return validateSurfaceWithFillerPass(tmp, name, obj, surfaceBodyReading({}));
        };

        // SINGULAR key given an ARRAY: the type lint fires, the divergence claim must not.
        const PackResult wrapArray = runMultipass(QStringLiteral("sf-dot-a"), QStringLiteral("bufferWrap"),
                                                  QJsonArray{QStringLiteral("repeat")});
        QVERIFY2(wrapArray.report.contains(QStringLiteral("bufferWrap is not a string")), qPrintable(wrapArray.report));
        QVERIFY2(!wrapArray.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(wrapArray.report));

        // PLURAL key given a STRING: same, the other way round.
        const PackResult wrapsString =
            runMultipass(QStringLiteral("sf-dot-b"), QStringLiteral("bufferWraps"), QStringLiteral("repeat"));
        QVERIFY2(wrapsString.report.contains(QStringLiteral("bufferWraps must be an array")),
                 qPrintable(wrapsString.report));
        QVERIFY2(!wrapsString.report.contains(QStringLiteral("which the DAEMON honours")),
                 qPrintable(wrapsString.report));

        // And the CORRECTLY typed values still draw it, in both spellings, or the fix would have
        // silenced the arm rather than narrowed it.
        const PackResult wrapOk =
            runMultipass(QStringLiteral("sf-dot-c"), QStringLiteral("bufferWrap"), QStringLiteral("repeat"));
        QVERIFY2(wrapOk.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(wrapOk.report));
        const PackResult wrapsOk = runMultipass(QStringLiteral("sf-dot-d"), QStringLiteral("bufferWraps"),
                                                QJsonArray{QStringLiteral("repeat")});
        QVERIFY2(wrapsOk.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(wrapsOk.report));
    }

    /// The `builtin:` SPELLING diagnostics, all three shapes, because the arm that reports a
    /// mis-cased prefix had no test at all and could have been deleted with the suite green.
    /// The two failures land in DIFFERENT arms, which is the whole point: a bad PREFIX makes
    /// isBuiltinBufferShader answer false and falls through to the file-not-found path, while
    /// a good prefix with a bad SUFFIX answers true and fails the exact-case table lookup.
    /// The second was reading as "your install is missing a file", and it is the likelier
    /// author slip of the two.
    void builtinTokenSpellingIsDiagnosedByShape()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto runWithToken = [&tmp](const QString& name, const QString& token) {
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"), QJsonArray{token});
            return validateSurface(tmp, name, obj, surfaceBodyReading({}));
        };

        // Good prefix, mis-cased suffix: named as case, with the exact spelling to use.
        const PackResult suffix = runWithToken(QStringLiteral("sf-tok-a"), QStringLiteral("builtin:GAUSSIAN-V"));
        // The WHOLE clause, in order, because two substring tests are satisfied by a swapped
        // .arg pair that would tell the author to rename their correct token to the bad one.
        QVERIFY2(suffix.report.contains(QStringLiteral("'builtin:GAUSSIAN-V' should be 'builtin:gaussian-v'")),
                 qPrintable(suffix.report));
        QVERIFY2(suffix.report.contains(QStringLiteral("tokens are lower case")), qPrintable(suffix.report));
        QVERIFY2(!suffix.report.contains(QStringLiteral("unknown or unlocatable")), qPrintable(suffix.report));
        QCOMPARE(suffix.errors, 1);

        // Mis-cased prefix, and a space-prefixed one: both the spelling arm.
        const PackResult prefix = runWithToken(QStringLiteral("sf-tok-b"), QStringLiteral("Builtin:gaussian-h"));
        QVERIFY2(prefix.report.contains(QStringLiteral("wrong spelling")), qPrintable(prefix.report));
        QVERIFY2(!prefix.report.contains(QStringLiteral("tokens are lower case")), qPrintable(prefix.report));
        QCOMPARE(prefix.errors, 1);
        const PackResult spaced = runWithToken(QStringLiteral("sf-tok-c"), QStringLiteral(" builtin:gaussian-h"));
        QVERIFY2(spaced.report.contains(QStringLiteral("wrong spelling")), qPrintable(spaced.report));
        QCOMPARE(spaced.errors, 1);

        // A plain missing file keeps the ORIGINAL message, so neither new arm has widened to
        // swallow the ordinary case.
        // Keyed on the WHOLE message, not on the word "missing", which also appears in
        // "missing required field", "preview missing", "texture missing" and "vertex shader
        // missing" — this fixture discriminates today only because it draws nothing else.
        const PackResult missing = runWithToken(QStringLiteral("sf-tok-d"), QStringLiteral("nosuchfile.frag"));
        QVERIFY2(missing.report.contains(QStringLiteral("multipass buffer shader missing")),
                 qPrintable(missing.report));
        QVERIFY2(!missing.report.contains(QStringLiteral("tokens are lower case")), qPrintable(missing.report));
        QVERIFY2(!missing.report.contains(QStringLiteral("unknown or unlocatable")), qPrintable(missing.report));
        QCOMPARE(missing.errors, 1);

        // TRAILING whitespace on an otherwise-correct token. It reaches the case arm rather
        // than the spelling one, because the prefix is exact; a LEADING space cannot, which is
        // what the two legs above pin. Without this the token renders as correct in its own
        // diagnostic and reads as a missing install.
        const PackResult trailing = runWithToken(QStringLiteral("sf-tok-e"), QStringLiteral("builtin:gaussian-h "));
        QVERIFY2(trailing.report.contains(QStringLiteral("no surrounding whitespace")), qPrintable(trailing.report));
        QVERIFY2(!trailing.report.contains(QStringLiteral("unknown or unlocatable")), qPrintable(trailing.report));
        QCOMPARE(trailing.errors, 1);
    }

    /// The SINGLE bufferScale, out of range at both ends. The per-pass list has
    /// its own slot in the sibling file; this is the pack-wide scalar, which is
    /// what every pass falls back to.
    void aSingleBufferScaleOutOfRangeIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto runWithScale = [&tmp](const QString& name, double scale) {
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            obj.insert(QStringLiteral("bufferShaders"), QJsonArray{surfaceFillerBufferName()});
            obj.insert(QStringLiteral("bufferScale"), scale);
            return validateSurfaceWithFillerPass(tmp, name, obj, surfaceBodyReading({}));
        };

        // KEYED ON THE RANGE LINT'S OWN WORDS, not on the bare key name. A bare
        // `contains("bufferScale")` is satisfied by the NOT-A-NUMBER lint too, so
        // it would pass on a pack whose range check never ran.
        const PackResult low = runWithScale(QStringLiteral("sf-scale-low"), 0.0001);
        QVERIFY2(low.report.contains(QStringLiteral("bufferScale out of range")), qPrintable(low.report));
        const PackResult high = runWithScale(QStringLiteral("sf-scale-high"), 4.0);
        QVERIFY2(high.report.contains(QStringLiteral("bufferScale out of range")), qPrintable(high.report));

        // A legal scale draws nothing, so neither arm is firing on the key's mere
        // presence.
        //
        // THIS CONTROL WAS VACUOUS. It asserted the absence of "bufferScale value",
        // a string this tree emits NOWHERE, so it could not fail and proved none of
        // what the sentence above claims. It is the exact trap this file's header
        // warns about, which is worth leaving on the record rather than quietly
        // correcting.
        const PackResult ok = runWithScale(QStringLiteral("sf-scale-ok"), 0.5);
        QVERIFY2(!ok.report.contains(QStringLiteral("bufferScale out of range")), qPrintable(ok.report));
    }

    /// A declared stage that does not exist. The fragment is the pack's required
    /// stage and the vertex is optional, so they fail differently and both are
    /// worth pinning.
    void aDeclaredButAbsentStageIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        // FRAGMENT declared as a name nothing writes. validateSurface always
        // writes effect.frag, so the metadata names something else.
        {
            QJsonObject obj = surfacePack(QStringLiteral("sf-nofrag"), QJsonArray{});
            obj.insert(QStringLiteral("fragmentShader"), QStringLiteral("absent.frag"));
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-nofrag"), obj, surfaceBodyReading({}));
            QVERIFY2(r.errors > 0, qPrintable(r.report));
        }
        // VERTEX declared and absent: the pack is otherwise fine, so this is
        // the lint on its own rather than a cascade.
        {
            QJsonObject obj = surfacePack(QStringLiteral("sf-novert"), QJsonArray{});
            obj.insert(QStringLiteral("vertexShader"), QStringLiteral("absent.vert"));
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-novert"), obj, surfaceBodyReading({}));
            QVERIFY2(r.report.contains(QStringLiteral("vertex shader missing")), qPrintable(r.report));
        }
    }

    /// A vertexShader path escaping the pack directory takes the EARLY-EXIT
    /// branch, which returns before any other lint runs. The sibling file covers
    /// the fragment and bufferShaders early exits; this is the third.
    void aVertexPathEscapingThePackIsRefused()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-vert-escape"), QJsonArray{});
        obj.insert(QStringLiteral("vertexShader"), QStringLiteral("../../../etc/passwd"));
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-vert-escape"), obj, surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("vertexShader path escapes the pack directory")),
                 qPrintable(r.report));
        QCOMPARE(r.errors, 1);
    }

    /// A pack missing a REQUIRED field, and metadata that is not readable or not
    /// JSON at all. These are the validator's three earliest exits and none of
    /// them had a slot, so the whole entry path could have been deleted.
    void theMetadataEntryFailuresEachFire()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        // No fragmentShader key at all.
        {
            QJsonObject obj;
            obj.insert(QStringLiteral("id"), QStringLiteral("sf-noid"));
            obj.insert(QStringLiteral("name"), QStringLiteral("sf-noid"));
            const PackResult r = validateSurface(tmp, QStringLiteral("sf-noid"), obj, surfaceBodyReading({}));
            QVERIFY2(r.report.contains(QStringLiteral("missing required field")), qPrintable(r.report));
        }
        // Not JSON.
        {
            const QString dir = tmp.filePath(QStringLiteral("sf-badjson"));
            QVERIFY(writePackFile(dir, QStringLiteral("metadata.json"), "{ this is not json"));
            QVERIFY(writePackFile(dir, QStringLiteral("effect.frag"), surfaceBodyReading({}).toUtf8()));
            QString report;
            QTextStream stream(&report);
            const int errors = PlasmaZones::ShaderValidate::validateSurfacePack(dir, stream);
            stream.flush();
            QVERIFY2(report.contains(QStringLiteral("invalid JSON")), qPrintable(report));
            QCOMPARE(errors, 1);
        }
        // No metadata.json at all.
        {
            const QString dir = tmp.filePath(QStringLiteral("sf-nometa"));
            QVERIFY(QDir().mkpath(dir));
            QString report;
            QTextStream stream(&report);
            const int errors = PlasmaZones::ShaderValidate::validateSurfacePack(dir, stream);
            stream.flush();
            QVERIFY2(report.contains(QStringLiteral("cannot read metadata.json")), qPrintable(report));
            QCOMPARE(errors, 1);
        }
    }

    /// An include the tree cannot resolve fails EXPANSION rather than compilation,
    /// which is a different report line and a different exit. Without this the
    /// expansion-failure branch is unreachable from the suite.
    void anUnresolvableIncludeReportsExpansionFailure()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const QJsonObject obj = surfacePack(QStringLiteral("sf-badinc"), QJsonArray{});
        const PackResult r =
            validateSurface(tmp, QStringLiteral("sf-badinc"), obj,
                            QStringLiteral("#include <no_such_header.glsl>\n") + surfaceBodyReading({}));
        QVERIFY2(r.report.contains(QStringLiteral("include expansion failed")), qPrintable(r.report));
    }

    /// A buffer pass that does not compile. The fragment bakes are covered in the
    /// sibling file; a BUFFER pass is a separate loop with its own report label,
    /// and nothing exercised its failure path.
    void aBrokenBufferPassIsReported()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-badbuf"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("pass0.frag")});
        const QString dir = tmp.filePath(QStringLiteral("sf-badbuf"));
        QVERIFY(writePackFile(dir, QStringLiteral("metadata.json"), QJsonDocument(obj).toJson()));
        QVERIFY(writePackFile(dir, QStringLiteral("effect.frag"), surfaceBodyReading({}).toUtf8()));
        // A buffer pass ships its own main() and gets no generated preamble, so
        // it carries its own #version. The undeclared identifier is the break.
        QVERIFY(writePackFile(dir, QStringLiteral("pass0.frag"),
                              "#version 450\n"
                              "layout(location = 0) out vec4 fragColor;\n"
                              "void main() { fragColor = vec4(notDeclaredAnywhere); }\n"));

        QString report;
        QTextStream stream(&report);
        const int errors = PlasmaZones::ShaderValidate::validateSurfacePack(dir, stream);
        stream.flush();
        QVERIFY2(reportLineHas(report, QStringLiteral("pass0.frag"), QStringLiteral("ERROR")), qPrintable(report));
        QVERIFY2(errors > 0, qPrintable(report));
    }
};

QTEST_MAIN(TestSurfacePackLints)
#include "test_surface_pack_lints.moc"
