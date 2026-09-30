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
            QVERIFY2(r.report.contains(QStringLiteral("%1 has 1 entry for 2 buffer shaders").arg(key)),
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
    /// No leg count here on purpose, for the reason the fail-closed slot below spells out: a
    /// numeral in a comment is the trap the helpers header warns about twice, and this one had
    /// already rotted once. One leg is present because of a MUTATION rather than a defect: the
    /// arm's predicate is `.toBool()`, and with only a fires/absent pair a mutation to
    /// `meta.contains(...)` passes both, so the EXPLICIT FALSE leg is the state that kills it.
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

        // bufferFilters, THE FOURTH CALL SITE, which nothing in the tree pinned: the shape is four
        // hand-written booleans and only three were covered, so flipping this one silenced the arm
        // for every correctly-typed bufferFilters array with the whole suite green.
        const PackResult filtersOk = runMultipass(QStringLiteral("sf-dot-e"), QStringLiteral("bufferFilters"),
                                                  QJsonArray{QStringLiteral("nearest")});
        QVERIFY2(filtersOk.report.contains(QStringLiteral("bufferFilters declares nearest")),
                 qPrintable(filtersOk.report));
        const PackResult filtersString =
            runMultipass(QStringLiteral("sf-dot-f"), QStringLiteral("bufferFilters"), QStringLiteral("nearest"));
        QVERIFY2(filtersString.report.contains(QStringLiteral("bufferFilters must be an array")),
                 qPrintable(filtersString.report));
        QVERIFY2(!filtersString.report.contains(QStringLiteral("which the DAEMON honours")),
                 qPrintable(filtersString.report));
    }

    /// The same arm given an INVALID token rather than the wrong shape. The loader's validatedWrap
    /// and validatedFilter CLEAR a token their vocabulary guard rejects, so the daemon falls back to
    /// the same clamp/linear the compositor uses and there is no divergence to report — but the arm
    /// collected any token that merely differed from the default, so an invalid one drew the
    /// "DAEMON honours" claim two lines under the vocabulary lint saying the value was cleared.
    void anInvalidDaemonOnlyTokenDoesNotClaimTheDaemonHonoursIt()
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

        // All four spellings, each given a token that is not in its vocabulary.
        const PackResult wrap =
            runMultipass(QStringLiteral("sf-iv-a"), QStringLiteral("bufferWrap"), QStringLiteral("wobble"));
        QVERIFY2(wrap.report.contains(QStringLiteral("not in vocabulary")), qPrintable(wrap.report));
        QVERIFY2(!wrap.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(wrap.report));

        const PackResult wraps = runMultipass(QStringLiteral("sf-iv-b"), QStringLiteral("bufferWraps"),
                                              QJsonArray{QStringLiteral("wobble")});
        QVERIFY2(wraps.report.contains(QStringLiteral("not in vocabulary")), qPrintable(wraps.report));
        QVERIFY2(!wraps.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(wraps.report));

        const PackResult filter =
            runMultipass(QStringLiteral("sf-iv-c"), QStringLiteral("bufferFilter"), QStringLiteral("trilinear"));
        QVERIFY2(filter.report.contains(QStringLiteral("not in vocabulary")), qPrintable(filter.report));
        QVERIFY2(!filter.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(filter.report));

        const PackResult filters = runMultipass(QStringLiteral("sf-iv-d"), QStringLiteral("bufferFilters"),
                                                QJsonArray{QStringLiteral("trilinear")});
        QVERIFY2(filters.report.contains(QStringLiteral("not in vocabulary")), qPrintable(filters.report));
        QVERIFY2(!filters.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(filters.report));

        // A VALID non-default token still draws it, so the fix narrowed the arm rather than
        // silencing it. `mipmap` is in the filter vocabulary and is not the compositor's default.
        const PackResult valid = runMultipass(QStringLiteral("sf-iv-e"), QStringLiteral("bufferFilters"),
                                              QJsonArray{QStringLiteral("mipmap")});
        QVERIFY2(valid.report.contains(QStringLiteral("bufferFilters declares mipmap")), qPrintable(valid.report));
    }

    /// The divergence messages on a chain that FAILS CLOSED. The registry resets every buffer key
    /// and drops the pack to single-pass when any entry does not resolve, so it creates no buffer
    /// targets at all, and every message describing what the daemon does with them is then false.
    /// Only bufferFeedback was gated; every sibling described targets that never exist. No count
    /// is given here on purpose: two rounds have moved which arms this fixture reaches, and a
    /// numeral in a comment is the trap the helpers header warns about twice.
    void aFailClosedChainDrawsNoBufferDivergenceClaims()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        QJsonObject obj = surfacePack(QStringLiteral("sf-fc"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        // A typo'd buffer filename, the commonest authoring slip, and nothing writes it.
        obj.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("typo.frag")});
        obj.insert(QStringLiteral("bufferWrap"), QStringLiteral("repeat"));
        obj.insert(QStringLiteral("bufferWraps"), QJsonArray{QStringLiteral("repeat")});
        obj.insert(QStringLiteral("bufferFilter"), QStringLiteral("nearest"));
        obj.insert(QStringLiteral("bufferFilters"), QJsonArray{QStringLiteral("nearest")});
        obj.insert(QStringLiteral("halfFloatBuffers"), true);
        // BELOW-min deliberately, not above. Only the below-min case is gated on chainResolves,
        // because there the load path clamps to kMinBufferScale and the coherence block then resets
        // to 1.0, so "clamped at load" would name a value the pack never has. Above-max is true on
        // both paths and is therefore UNGATED — the separate leg below pins that.
        obj.insert(QStringLiteral("bufferScale"), 0.001);
        // The PER-ENTRY scales, which were left ungated when their singular twin was gated.
        // KEEP EVERY BUFFER ARRAY THE SAME LENGTH AS bufferShaders. The exact count below holds
        // only because of that: lintBufferArrayLen is deliberately UNGATED (a length mismatch is
        // decidable either way), so adding a pass without extending the arrays, or an entry
        // without a pass, makes it fire and the count wrong.
        obj.insert(QStringLiteral("bufferScales"), QJsonArray{9});
        const PackResult r = validateSurface(tmp, QStringLiteral("sf-fc"), obj, surfaceBodyReading({}));

        // The louder lint that CLEARED chainResolves is the one the author needs, and it fires.
        QVERIFY2(r.report.contains(QStringLiteral("multipass buffer shader missing")), qPrintable(r.report));
        // EXACT COUNT, not just absences. Three substring negatives cannot say "nothing but the
        // louder lint", and that gap is how the ungated per-entry and depth arms stayed invisible
        // while this slot passed.
        QCOMPARE(r.errors, 1);
        // Arm-SPECIFIC needles. Plain "clamped at load" has two producers (the per-entry arm and
        // the singular one), so the generic form passed here only because the old fixture declared
        // no bufferScales at all.
        QVERIFY2(!r.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("RGBA16F")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("bufferScale out of range")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("bufferScales entry 0 out of range")), qPrintable(r.report));

        // ABOVE-max is NOT gated, because "clamped at load" is accurate on a fail-closed chain too:
        // both the load clamp and the coherence reset land on the same value. Gating the whole range
        // arm on the below-min argument once silenced this true line, so pin it firing.
        QJsonObject above = obj;
        above.insert(QStringLiteral("id"), QStringLiteral("sf-fc-max"));
        above.insert(QStringLiteral("bufferScale"), 9);
        above.remove(QStringLiteral("bufferScales"));
        const PackResult aboveMax = validateSurface(tmp, QStringLiteral("sf-fc-max"), above, surfaceBodyReading({}));
        QVERIFY2(aboveMax.report.contains(QStringLiteral("bufferScale out of range")), qPrintable(aboveMax.report));
        // Counted for the reason this slot argues above: two, the louder missing-shader lint plus
        // the range line. A substring negative alone cannot say "and nothing else".
        QCOMPARE(aboveMax.errors, 2);
        // But the TYPE arm is deliberately NOT gated: "falls back to 1.0" is true on a fail-closed
        // chain too, because fromJson's non-numeric branch and the coherence block both set 1.0.
        // Gating it once silenced a true diagnostic, so this pins the un-gating.
        QJsonObject typed = obj;
        typed.insert(QStringLiteral("id"), QStringLiteral("sf-fc-type"));
        typed.insert(QStringLiteral("bufferScale"), QStringLiteral("0.5"));
        typed.remove(QStringLiteral("bufferScales"));
        const PackResult typeArm = validateSurface(tmp, QStringLiteral("sf-fc-type"), typed, surfaceBodyReading({}));
        QVERIFY2(typeArm.report.contains(QStringLiteral("bufferScale is not a number")), qPrintable(typeArm.report));
        // Two again: the same missing-shader lint, plus the TYPE line where the leg above had the
        // RANGE line. Those two arms are separately gated and exercising each on its own is why
        // both legs exist, so this is NOT "the same pair". The `depthBuffer` removal that used to
        // sit here was dead: the base fixture no longer declares the key, so `typed` never had it.
        QCOMPARE(typeArm.errors, 2);

        // Control: the identical keys on a chain that DOES resolve still draw them, or the gate
        // would have silenced the arms rather than scoping them. The depth pairing is deliberately
        // NOT part of this fixture: that arm fires only on real divergence between the clamped
        // per-pass scales and the clamped pack-wide one, which is a different axis from being out
        // of range, so it gets its own slot below.
        QJsonObject ok = obj;
        ok.insert(QStringLiteral("id"), QStringLiteral("sf-fc-ok"));
        ok.insert(QStringLiteral("bufferShaders"), QJsonArray{surfaceFillerBufferName()});
        const PackResult live =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-fc-ok"), ok, surfaceBodyReading({}));
        QVERIFY2(live.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(live.report));
        QVERIFY2(live.report.contains(QStringLiteral("RGBA16F")), qPrintable(live.report));
        QVERIFY2(live.report.contains(QStringLiteral("bufferScale out of range")), qPrintable(live.report));
        QVERIFY2(live.report.contains(QStringLiteral("bufferScales entry 0 out of range")), qPrintable(live.report));
        QVERIFY2(!live.report.contains(QStringLiteral("alongside \"depthBuffer\": true")), qPrintable(live.report));
    }

    /// The depth+bufferScales arm fires only on REAL divergence, mirroring the runtime's own twin
    /// warning, which compares each clamped per-pass scale against the clamped pack-wide one and
    /// warns just when they differ. Gated on presence alone the arm FAILED A VALID PACK: a pack
    /// declaring the same value in both places renders identically on the daemon (which pins every
    /// pass) and on the compositor (which has no depth buffer and reads each entry), so there was
    /// no divergence to report and nothing an author could act on.
    void theDepthScalesArmFiresOnlyOnRealDivergence()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        // @p passes exists so a multi-pass leg can keep bufferScales the SAME LENGTH as
        // bufferShaders, which every leg here does EXCEPT sf-dx-f. That one is over-length on
        // purpose and its count INCLUDES the length lint, because being past the budget is the
        // whole thing it pins — do not "fix" its array length. For every other leg a mismatch
        // makes lintBufferArrayLen fire and the exact count wrong.
        const auto runWith = [&tmp](const QString& name, double packWide, const QJsonArray& scales, int passes = 1) {
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(QStringLiteral("multipass"), true);
            QJsonArray bufferShaders;
            for (int i = 0; i < passes; ++i) {
                bufferShaders.append(surfaceFillerBufferName());
            }
            obj.insert(QStringLiteral("bufferShaders"), bufferShaders);
            obj.insert(QStringLiteral("depthBuffer"), true);
            obj.insert(QStringLiteral("bufferScale"), packWide);
            obj.insert(QStringLiteral("bufferScales"), scales);
            return validateSurfaceWithFillerPass(tmp, name, obj, surfaceBodyReading({}));
        };
        const QString needle = QStringLiteral("alongside \"depthBuffer\": true");

        // AGREEING scales: no divergence, no lint, and the pack must ship.
        const PackResult agree = runWith(QStringLiteral("sf-dx-a"), 0.5, QJsonArray{0.5});
        QVERIFY2(!agree.report.contains(needle), qPrintable(agree.report));
        QCOMPARE(agree.errors, 0);

        // Agreeing across SEVERAL passes. TWO passes and two scales: an earlier version declared
        // one pass with two scales and called the surplus entry "a slot the pack does not declare",
        // which is the opposite shape, drew an unasserted length error, and left the leg a
        // duplicate of `agree`.
        const PackResult agreeMulti = runWith(QStringLiteral("sf-dx-b"), 0.25, QJsonArray{0.25, 0.25}, 2);
        QVERIFY2(!agreeMulti.report.contains(needle), qPrintable(agreeMulti.report));
        QCOMPARE(agreeMulti.errors, 0);

        // AN IN-BUDGET DIVERGENCE PAST ENTRY 0, which nothing else here pins. Every other leg
        // either agrees at entry 0, has one entry, or diverges only past the budget, so narrowing
        // the production loop to check entry 0 alone left the whole slot green. This is the leg
        // that fails on that mutation.
        const PackResult later = runWith(QStringLiteral("sf-dx-h"), 0.5, QJsonArray{0.5, 0.25}, 2);
        QVERIFY2(later.report.contains(needle), qPrintable(later.report));
        QCOMPARE(later.errors, 1);

        // Two further legs pin the arm's LOOP rather than its gates: an in-budget divergence past
        // entry 0, and more scales than passes. They live in the sibling file's
        // theDepthArmLoopIsBoundedByPassCountAndAccumulates, which had the room.

        // DIVERGING: one entry differs from the pack-wide scale, which is the case the daemon's
        // pinning actually discards, so the lint fires. One error, the depth line alone.
        const PackResult diverge = runWith(QStringLiteral("sf-dx-c"), 0.5, QJsonArray{0.25});
        QVERIFY2(diverge.report.contains(needle), qPrintable(diverge.report));
        QCOMPARE(diverge.errors, 1);

        // CLAMPED divergence does not count: both sides clamp to the same bound, so a pack whose
        // raw values differ but whose effective ones agree is not nagged. Two, both from the range
        // arms — the count is what pins that the silence is not covering a third line.
        const PackResult clamped = runWith(QStringLiteral("sf-dx-d"), 9.0, QJsonArray{5.0});
        QVERIFY2(!clamped.report.contains(needle), qPrintable(clamped.report));
        QCOMPARE(clamped.errors, 2);

        // A MISTYPED entry cannot diverge, and the isDouble() guard in the loop is what says so.
        // fromJson appends the pack-wide scale in place of a non-number, so that slot is equal by
        // construction; drop the guard and clampScale(0) reads as a divergence next to the
        // not-a-number line, which is the one line the author needs.
        const PackResult mistyped = runWith(QStringLiteral("sf-dx-e"), 0.25, QJsonArray{QStringLiteral("0.5")});
        QVERIFY2(!mistyped.report.contains(needle), qPrintable(mistyped.report));
        QCOMPARE(mistyped.errors, 1);

        // PAST THE PASS BUDGET, and this is the leg the unbounded loop failed. fromJson caps
        // bufferScales at kMaxBufferPasses, so a divergent entry past the cap reaches neither host.
        // The over-budget lint rejects the pack on its own and is the ONLY line here: the positional
        // arm is silent because capping both sides makes 9 scales and 8 passes come out aligned,
        // which is exactly what they are at load. What this pins is that the depth arm adds no
        // second claim about a divergence that provably cannot occur.
        const int depthCap = PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses;
        QJsonArray pastBudget;
        for (int i = 0; i < depthCap; ++i) {
            pastBudget.append(0.5);
        }
        pastBudget.append(0.25);
        const PackResult past = runWith(QStringLiteral("sf-dx-f"), 0.5, pastBudget, depthCap);
        QVERIFY2(!past.report.contains(needle), qPrintable(past.report));
        QCOMPARE(past.errors, 1);

        // THE chainResolves GATE, which nothing covered. On a fail-closed chain the registry's
        // coherence block sets useDepthBuffer false and clears bufferScales, so nothing is pinned
        // and the claim would be false. Plain validateSurface, so the declared pass cannot resolve.
        QJsonObject broken = surfacePack(QStringLiteral("sf-dx-g"), QJsonArray{});
        broken.insert(QStringLiteral("multipass"), true);
        broken.insert(QStringLiteral("bufferShaders"), QJsonArray{QStringLiteral("typo.frag")});
        broken.insert(QStringLiteral("depthBuffer"), true);
        broken.insert(QStringLiteral("bufferScale"), 0.5);
        broken.insert(QStringLiteral("bufferScales"), QJsonArray{0.25});
        const PackResult failClosed = validateSurface(tmp, QStringLiteral("sf-dx-g"), broken, surfaceBodyReading({}));
        QVERIFY2(!failClosed.report.contains(needle), qPrintable(failClosed.report));
        QCOMPARE(failClosed.errors, 1);
    }

    /// A texture or preview path naming a DIRECTORY. QFile::exists() answers true for one, so both
    /// existence lints passed it and the whole pack reported OK. The runtime accepts it too (its
    /// only test is confinement), so the failure landed at first paint with nothing having warned.
    void aDirectoryPathIsNotAcceptedAsAFile()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        // preview naming a directory inside the pack.
        QVERIFY(QDir().mkpath(tmp.filePath(QStringLiteral("sf-dir-a")) + QStringLiteral("/sub")));
        QJsonObject prev = surfacePack(QStringLiteral("sf-dir-a"), QJsonArray{});
        prev.insert(QStringLiteral("preview"), QStringLiteral("sub"));
        const PackResult previewResult = validateSurface(tmp, QStringLiteral("sf-dir-a"), prev, surfaceBodyReading({}));
        QVERIFY2(previewResult.report.contains(QStringLiteral("preview path is not a file")),
                 qPrintable(previewResult.report));

        // textures[].path naming a directory inside the pack.
        QVERIFY(QDir().mkpath(tmp.filePath(QStringLiteral("sf-dir-b")) + QStringLiteral("/sub")));
        QJsonObject tex = surfacePack(QStringLiteral("sf-dir-b"), QJsonArray{});
        tex.insert(QStringLiteral("textures"),
                   QJsonArray{QJsonObject{{QStringLiteral("path"), QStringLiteral("sub")}}});
        const PackResult texResult = validateSurface(tmp, QStringLiteral("sf-dir-b"), tex, surfaceBodyReading({}));
        QVERIFY2(texResult.report.contains(QStringLiteral("texture path is not a file")), qPrintable(texResult.report));

        // A genuinely ABSENT path keeps its own wording, so the two cases stay distinguishable.
        QJsonObject gone = surfacePack(QStringLiteral("sf-dir-c"), QJsonArray{});
        gone.insert(QStringLiteral("preview"), QStringLiteral("nope.png"));
        const PackResult goneResult = validateSurface(tmp, QStringLiteral("sf-dir-c"), gone, surfaceBodyReading({}));
        QVERIFY2(goneResult.report.contains(QStringLiteral("preview missing: nope.png")),
                 qPrintable(goneResult.report));
        QVERIFY2(!goneResult.report.contains(QStringLiteral("is not a file")), qPrintable(goneResult.report));
    }

    /// A NON-STRING `preview` or `paddingParam` shipped the pack GREEN with no diagnostic
    /// anywhere, in the validator or the journal: fromJson reads both with toString(), which
    /// answers empty for a number or an array, and both lint blocks were gated on !isEmpty().
    /// This is the same shape that was closed for the wrap/filter keys and the per-texture wrap;
    /// these two were what remained.
    void aNonStringPreviewOrPaddingParamIsLinted()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const auto runWith = [&tmp](const QString& name, QLatin1String key, const QJsonValue& value) {
            QJsonObject obj = surfacePack(name, QJsonArray{});
            obj.insert(QString(key), value);
            return validateSurface(tmp, name, obj, surfaceBodyReading({}));
        };

        const PackResult prevNum = runWith(QStringLiteral("sf-ns-a"), QLatin1String("preview"), 5);
        QVERIFY2(prevNum.report.contains(QStringLiteral("preview is not a string")), qPrintable(prevNum.report));
        // EXACTLY one line. Without a count, a change to toVariant().toString() would add a
        // spurious "preview missing: 5" beside this and no leg would notice.
        QCOMPARE(prevNum.errors, 1);
        const PackResult prevArr =
            runWith(QStringLiteral("sf-ns-b"), QLatin1String("preview"), QJsonArray{QStringLiteral("a.png")});
        QVERIFY2(prevArr.report.contains(QStringLiteral("preview is not a string")), qPrintable(prevArr.report));

        const PackResult padNum = runWith(QStringLiteral("sf-ns-c"), QLatin1String("paddingParam"), 5);
        QVERIFY2(padNum.report.contains(QStringLiteral("paddingParam is not a string")), qPrintable(padNum.report));
        QCOMPARE(padNum.errors, 1);
        const PackResult padArr =
            runWith(QStringLiteral("sf-ns-d"), QLatin1String("paddingParam"), QJsonArray{QStringLiteral("w")});
        QVERIFY2(padArr.report.contains(QStringLiteral("paddingParam is not a string")), qPrintable(padArr.report));

        // ABSENT and NULL stay silent on both, or the arm would fire on every idiomatic pack.
        const PackResult absent =
            validateSurface(tmp, QStringLiteral("sf-ns-e"), surfacePack(QStringLiteral("sf-ns-e"), QJsonArray{}),
                            surfaceBodyReading({}));
        QVERIFY2(!absent.report.contains(QStringLiteral("is not a string")), qPrintable(absent.report));
        QCOMPARE(absent.errors, 0);
        const PackResult nulled =
            runWith(QStringLiteral("sf-ns-f"), QLatin1String("preview"), QJsonValue(QJsonValue::Null));
        QVERIFY2(!nulled.report.contains(QStringLiteral("is not a string")), qPrintable(nulled.report));
    }

    /// The wrap/filter arrays were read UNCAPPED, so an entry past the pass budget drew a claim
    /// about a value fromJson DROPS rather than keeps: the vocabulary line for an invalid token, or
    /// the daemon-honours claim for a valid non-default one. Not both for one entry — those two
    /// arms are mutually exclusive, since the second requires the token to BE valid. The
    /// buffer-shader loop and the bufferScales loop were already bounded to what the loader keeps;
    /// these two were missed. The over-length arm still reports the surplus by count, which is the
    /// part an author can act on.
    void wrapAndFilterArraysAreBoundedToWhatTheLoaderKeeps()
    {
        QTemporaryDir tmp;
        REQUIRE_SURFACE_FIXTURE(tmp);

        const int cap = PhosphorSurfaceShaders::SurfaceShaderEffect::kMaxBufferPasses;
        QJsonArray buffers;
        for (int i = 0; i < cap; ++i) {
            buffers.append(surfaceFillerBufferName());
        }
        // cap empties, then one past-budget entry carrying a non-default token AND a bad one.
        QJsonArray wraps;
        QJsonArray filters;
        for (int i = 0; i < cap; ++i) {
            wraps.append(QStringLiteral("clamp"));
            filters.append(QStringLiteral("linear"));
        }
        wraps.append(QStringLiteral("repeat"));
        filters.append(QStringLiteral("nosuchfilter"));

        QJsonObject obj = surfacePack(QStringLiteral("sf-cap"), QJsonArray{});
        obj.insert(QStringLiteral("multipass"), true);
        obj.insert(QStringLiteral("bufferShaders"), buffers);
        obj.insert(QStringLiteral("bufferWraps"), wraps);
        obj.insert(QStringLiteral("bufferFilters"), filters);
        const PackResult r = validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-cap"), obj, surfaceBodyReading({}));

        // The surplus is still reported by COUNT, which is the actionable part.
        QVERIFY2(r.report.contains(QStringLiteral("bufferWraps has")), qPrintable(r.report));
        // But neither past-budget entry draws a claim about a value the loader threw away.
        QVERIFY2(!r.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(r.report));
        QVERIFY2(!r.report.contains(QStringLiteral("not in vocabulary")), qPrintable(r.report));
        // Counted, for the reason the probe leg below argues: two substring negatives cannot say
        // "and nothing else", and the two length lints this fixture does draw were unasserted.
        QCOMPARE(r.errors, 2);

        // Control: the same tokens INSIDE the budget still draw both, or the cap would have
        // silenced the arms rather than bounded them.
        QJsonArray inWraps;
        QJsonArray inFilters;
        for (int i = 0; i < cap; ++i) {
            inWraps.append(i == 0 ? QStringLiteral("repeat") : QStringLiteral("clamp"));
            inFilters.append(i == 0 ? QStringLiteral("nosuchfilter") : QStringLiteral("linear"));
        }
        QJsonObject ok = obj;
        ok.insert(QStringLiteral("id"), QStringLiteral("sf-cap-ok"));
        ok.insert(QStringLiteral("bufferWraps"), inWraps);
        ok.insert(QStringLiteral("bufferFilters"), inFilters);
        const PackResult live =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-cap-ok"), ok, surfaceBodyReading({}));
        QVERIFY2(live.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(live.report));
        QVERIFY2(live.report.contains(QStringLiteral("not in vocabulary")), qPrintable(live.report));
        QCOMPARE(live.errors, 2);

        // THE BOUND IS kMaxBufferPasses, NOT declaredBuffers.size(), and the two fixtures above
        // cannot tell them apart because both are `cap` there. This leg puts the same offending
        // tokens at index cap-1 with only cap-1 PASSES, so the entries are inside the loader's
        // budget while being surplus against the pass count: the claims must still print. A
        // mutation to the wrong bound passes every other leg and is caught only here.
        //
        // ITS OWN ARRAYS, with the tokens at index cap-1. An earlier version reused
        // inWraps/inFilters, which put them at index 0 — inside BOTH candidate bounds, so the leg
        // discriminated nothing and was the control above plus two unasserted length errors.
        QJsonArray fewBuffers;
        QJsonArray probeWraps;
        QJsonArray probeFilters;
        for (int i = 0; i < cap; ++i) {
            probeWraps.append(i == cap - 1 ? QStringLiteral("repeat") : QStringLiteral("clamp"));
            probeFilters.append(i == cap - 1 ? QStringLiteral("nosuchfilter") : QStringLiteral("linear"));
            if (i < cap - 1) {
                fewBuffers.append(surfaceFillerBufferName());
            }
        }
        QJsonObject inBudget = obj;
        inBudget.insert(QStringLiteral("id"), QStringLiteral("sf-cap-probe"));
        inBudget.insert(QStringLiteral("bufferShaders"), fewBuffers);
        inBudget.insert(QStringLiteral("bufferWraps"), probeWraps);
        inBudget.insert(QStringLiteral("bufferFilters"), probeFilters);
        const PackResult probe =
            validateSurfaceWithFillerPass(tmp, QStringLiteral("sf-cap-probe"), inBudget, surfaceBodyReading({}));
        QVERIFY2(probe.report.contains(QStringLiteral("which the DAEMON honours")), qPrintable(probe.report));
        QVERIFY2(probe.report.contains(QStringLiteral("not in vocabulary")), qPrintable(probe.report));
        // Four: one length lint per array (cap entries against cap-1 passes) plus the two claims.
        // Narrowing the bound to declaredBuffers.size() drops index cap-1 and takes both claims
        // with it, which is the mutation this leg exists to fail on.
        QCOMPARE(probe.errors, 4);
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
