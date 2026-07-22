// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_page_controller.cpp
 * @brief AnimationsPageController behaviour pins: path discovery,
 *        override CRUD, the shader-leg support gate, and the
 *        stock-animation suppression mirror.
 *
 * Pins the file-per-path persistence model: setOverride writes one JSON
 * file under `<userProfilesDir>/<path>.json`, clearOverride deletes it,
 * resolvedProfile walks the parent chain and fills library defaults.
 * The suppression-mirror slots pin `stockSuppressedEvents` (the
 * settings-side twin of the compositor's syncStockEffectSuppression
 * ownership gate) and its NOTIFY inputs.
 *
 * Uses `setUserProfilesDirOverride()` to redirect override-file I/O into
 * a tmpdir, and `IsolatedConfigGuard` where a real Settings is needed, so
 * the test never touches the real user XDG dirs.
 *
 * Companion test files:
 *   - test_animations_motion_sets.cpp      — preset / motion-set / pending
 *   - test_animations_shader_overrides.cpp — shader-effect overrides
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <PhosphorAnimation/AnimationShaderRegistry.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorAnimation/Spring.h>

#include <QRegularExpression>

#include "helpers/IsolatedConfigGuard.h"
#include "config/settings.h"
#include "settings/pages/animationpagescope.h"
#include "settings/pages/animationspagecontroller.h"

using namespace PlasmaZones;
using PlasmaZones::TestHelpers::IsolatedConfigGuard;

namespace {

QString readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
}

/// Author a minimal animation pack `<root>/<subdir>/metadata.json` plus an
/// effect.frag stub so the registry's scan accepts it (mirrors the
/// decoration-page test fixture). An empty @p appliesTo omits the field,
/// which loads the pack as universal (applies to every single-surface
/// class); a non-empty array constrains it to the listed event classes.
bool writeAnimationPack(const QString& root, const QString& subdir, const QJsonArray& appliesTo)
{
    const QString packDir = root + QLatin1Char('/') + subdir;
    if (!QDir().mkpath(packDir))
        return false;
    QJsonObject metadata{{QLatin1String("id"), subdir},
                         {QLatin1String("name"), subdir},
                         {QLatin1String("fragmentShader"), QStringLiteral("effect.frag")},
                         {QLatin1String("parameters"), QJsonArray{}}};
    if (!appliesTo.isEmpty())
        metadata.insert(QLatin1String("appliesTo"), appliesTo);
    QFile meta(packDir + QStringLiteral("/metadata.json"));
    if (!meta.open(QIODevice::WriteOnly | QIODevice::Truncate) || meta.write(QJsonDocument(metadata).toJson()) < 0)
        return false;
    QFile frag(packDir + QStringLiteral("/effect.frag"));
    return frag.open(QIODevice::WriteOnly | QIODevice::Truncate) && frag.write(QByteArrayLiteral("// stub\n")) > 0;
}

} // namespace

class TestAnimationsPageController : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ─── Slider bounds ────────────────────────────────────────────────────

    void springBounds_areUsableSubsetOfEngineClamp()
    {
        AnimationsPageController c;
        // The engine clamps to omega ∈ [0.1, 200], zeta ∈ [0, 10]
        // (PhosphorAnimation/Spring.h). The slider exposes a deliberately
        // narrower, usable band within that clamp: above omega ~40 the spring
        // settles visually instantly, and zeta > ~4 is a barely-moving crawl;
        // zeta is floored at 0.1 so the edited spring always settles.
        QCOMPARE(c.springOmegaMin(), 1.0);
        QCOMPARE(c.springOmegaMax(), 40.0);
        QCOMPARE(c.springZetaMin(), 0.1);
        QCOMPARE(c.springZetaMax(), 4.0);

        // The slider band stays inside the engine's accepted clamp range. The
        // clamp bounds are not exported as named constants — they are literals
        // inside Spring::fromString — so they are read back OUT of the engine
        // by parsing values far outside any plausible band and seeing where it
        // pins them. Comparing against hand-copied 0.1 / 200 / 0 / 10 literals
        // would have compared the slider band to nothing at all, since the
        // QCOMPAREs above already pin the same four accessors to literals.
        using PhosphorAnimation::Spring;
        const Spring floors = Spring::fromString(QStringLiteral("spring:-1e9,-1e9"));
        const Spring ceilings = Spring::fromString(QStringLiteral("spring:1e9,1e9"));
        QVERIFY2(c.springOmegaMin() >= floors.omega, "slider omega floor is below the engine clamp");
        QVERIFY2(c.springOmegaMax() <= ceilings.omega, "slider omega ceiling is above the engine clamp");
        QVERIFY2(c.springZetaMin() >= floors.zeta, "slider zeta floor is below the engine clamp");
        QVERIFY2(c.springZetaMax() <= ceilings.zeta, "slider zeta ceiling is above the engine clamp");
    }

    // ─── Path discovery ───────────────────────────────────────────────────

    void sectionForPath_mapsTopLevelToUiSection()
    {
        AnimationsPageController c;
        // sectionForPath maps a path's top level to its UI section — which is NOT
        // always the first segment: osd/popup/panel all collapse into "overlays".
        QCOMPARE(c.sectionForPath(QStringLiteral("global")), QStringLiteral("global"));
        QCOMPARE(c.sectionForPath(QStringLiteral("editor")), QStringLiteral("editor"));
        QCOMPARE(c.sectionForPath(QStringLiteral("editor.snapIn")), QStringLiteral("editor"));
        QCOMPARE(c.sectionForPath(QStringLiteral("popup.layoutPicker.show")), QStringLiteral("overlays"));
        QCOMPARE(c.sectionForPath(QString()), QString());
    }

    void eventLabel_titleCasesCamelLeaf()
    {
        AnimationsPageController c;
        QCOMPARE(c.eventLabel(QStringLiteral("global")), QStringLiteral("Global"));
        QCOMPARE(c.eventLabel(QStringLiteral("editor.snapIn")), QStringLiteral("Snap In"));
        QCOMPARE(c.eventLabel(QStringLiteral("popup.layoutPicker")), QStringLiteral("Layout Picker"));
        QCOMPARE(c.eventLabel(QStringLiteral("popup.layoutPicker.popIn")), QStringLiteral("Pop In"));
    }

    void parentChain_walksToGlobal()
    {
        AnimationsPageController c;
        const auto chain = c.parentChain(QStringLiteral("editor.snapIn"));
        QCOMPARE(chain,
                 (QStringList{QStringLiteral("editor.snapIn"), QStringLiteral("editor"), QStringLiteral("global")}));
    }

    void parentChain_globalIsRoot()
    {
        AnimationsPageController c;
        const auto chain = c.parentChain(QStringLiteral("global"));
        QCOMPARE(chain, QStringList{QStringLiteral("global")});
    }

    void eventSections_groupsAllBuiltInPaths()
    {
        AnimationsPageController c;
        const QVariantList sections = c.eventSections();

        // Every built-in path must land in some section (no orphans).
        const QStringList paths = PhosphorAnimation::ProfilePaths::allBuiltInPaths();
        int totalListed = 0;
        QStringList allListed;
        for (const QVariant& sectionVar : sections) {
            const QVariantMap section = sectionVar.toMap();
            const QVariantList sPaths = section.value(QStringLiteral("paths")).toList();
            totalListed += sPaths.size();
            for (const QVariant& entry : sPaths) {
                allListed.append(entry.toMap().value(QStringLiteral("path")).toString());
            }
        }
        QCOMPARE(totalListed, paths.size());

        // Every listed path is a built-in path.
        for (const QString& p : allListed) {
            QVERIFY2(paths.contains(p), qPrintable(QStringLiteral("unknown path in UI: ") + p));
        }
    }

    void eventSections_categoryFlagSetForParents()
    {
        AnimationsPageController c;
        const QVariantList sections = c.eventSections();

        // Find the "editor" section's "editor" entry — it should be flagged
        // isCategory=true because editor.snapIn etc. live under it.
        bool foundEditorCategory = false;
        bool editorCategoryFlag = false;
        bool foundEditorSnapIn = false;
        bool editorSnapInCategoryFlag = true; // pessimistic default
        for (const QVariant& sectionVar : sections) {
            const QVariantMap section = sectionVar.toMap();
            if (section.value(QStringLiteral("section")).toString() != QLatin1String("editor"))
                continue;
            for (const QVariant& entry : section.value(QStringLiteral("paths")).toList()) {
                const QVariantMap m = entry.toMap();
                const QString path = m.value(QStringLiteral("path")).toString();
                if (path == QLatin1String("editor")) {
                    foundEditorCategory = true;
                    editorCategoryFlag = m.value(QStringLiteral("isCategory")).toBool();
                } else if (path == QLatin1String("editor.snapIn")) {
                    foundEditorSnapIn = true;
                    editorSnapInCategoryFlag = m.value(QStringLiteral("isCategory")).toBool();
                }
            }
        }
        QVERIFY(foundEditorCategory);
        QVERIFY(foundEditorSnapIn);
        QVERIFY2(editorCategoryFlag, "'editor' should be flagged as a category: it has children");
        QVERIFY2(!editorSnapInCategoryFlag, "'editor.snapIn' is a leaf: not a category");
    }

    // ─── Simple-page scope contract ───────────────────────────────────────

    /// Every event card on AnimationsSimplePage.qml must fall inside the
    /// `animations-simple` scope in animationpagescope.cpp. That scope drives
    /// the page's Reset, Discard and dirty walk, so a card whose path is not
    /// under one of the scope's roots is silently exempt from all three: the
    /// user edits it, the page reports itself clean, and Reset leaves it set.
    ///
    /// Mirror paths are held to the same contract. A mirror receives every
    /// write the primary does (AnimationEventCard's group writers loop
    /// `_writePaths`), so it is exactly as dependent on the scope's roots: a
    /// mirror declared outside them would be edited by the page, reported clean
    /// by it, and survive its Reset.
    ///
    /// The two lists were previously held together by a comment alone. This
    /// reads the QML as TEXT (no Qt Quick engine, no page construction) and
    /// pulls the eventPath and mirrorPaths literals straight out of the
    /// eventModel, so adding a card or a mirror there without widening the
    /// scope fails here.
    ///
    /// Scope limit, stated plainly: the parse sees `"eventPath": "…"` and
    /// `"mirrorPaths": [ … ]` string literals only. A path that came from a JS
    /// expression or a property reference would not be seen, and the page has
    /// never used one. The count guards below are what stop a rewrite in that
    /// style from turning this into a test that asserts nothing.
    void simpleScopeCoversEverySimplePageCard()
    {
        const QString qmlPath =
            QStringLiteral(P_SOURCE_DIR "/src/settings/qml/pages/animations/AnimationsSimplePage.qml");
        const QString src = readFile(qmlPath);
        QVERIFY2(!src.isEmpty(), qPrintable(QStringLiteral("could not read ") + qmlPath));

        static const QRegularExpression re(QStringLiteral("\"eventPath\"\\s*:\\s*\"([^\"]+)\""));
        QStringList cardPaths;
        auto it = re.globalMatch(src);
        while (it.hasNext())
            cardPaths.append(it.next().captured(1));

        // The page hosts five grouped cards today. A drop below that means the
        // regex or the file stopped matching rather than that a card was
        // removed, and the loop below would then pass vacuously.
        QVERIFY2(cardPaths.size() >= 5,
                 qPrintable(QStringLiteral("parsed only %1 eventPath literals from AnimationsSimplePage.qml")
                                .arg(cardPaths.size())));

        // Mirrors: pull each `"mirrorPaths": [...]` array body, then the string
        // literals inside it. Checked against the same scope and reported
        // through the same list as the primaries, since the consequence of an
        // out-of-scope mirror is identical.
        static const QRegularExpression mirrorArrayRe(QStringLiteral("\"mirrorPaths\"\\s*:\\s*\\[([^\\]]*)\\]"));
        static const QRegularExpression mirrorEntryRe(QStringLiteral("\"([^\"]+)\""));
        QStringList mirrorPaths;
        auto arrayIt = mirrorArrayRe.globalMatch(src);
        while (arrayIt.hasNext()) {
            const QString body = arrayIt.next().captured(1);
            auto entryIt = mirrorEntryRe.globalMatch(body);
            while (entryIt.hasNext())
                mirrorPaths.append(entryIt.next().captured(1));
        }

        // One mirror on the page today (the combined opened & closed card). Its
        // own non-vacuity floor, so a rewrite that stopped matching mirrors
        // fails here rather than silently checking the primaries alone.
        QVERIFY2(mirrorPaths.size() >= 1,
                 qPrintable(QStringLiteral("parsed only %1 mirrorPaths literals from AnimationsSimplePage.qml")
                                .arg(mirrorPaths.size())));
        cardPaths.append(mirrorPaths);

        const AnimationPageScope scope = animationPageScope(QStringLiteral("animations-simple"));
        QCOMPARE(scope.kind, AnimationPageScope::EventSubtree);
        // Collected rather than QVERIFY'd per row: a QVERIFY failure aborts the
        // slot, so the first out-of-scope card would hide every other one and
        // widening the scope would turn into a one-at-a-time hunt.
        QStringList outOfScope;
        for (const QString& path : cardPaths) {
            if (!animationPathInScope(path, scope))
                outOfScope.append(path);
        }
        QVERIFY2(outOfScope.isEmpty(),
                 qPrintable(QStringLiteral("simple-page cards outside the animations-simple scope: ")
                            + outOfScope.join(QLatin1String(", "))));
    }

    // ─── Override CRUD ────────────────────────────────────────────────────

    void setOverride_writesFileWithNameField()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);

        QVariantMap profile;
        profile.insert(QStringLiteral("duration"), 250);
        profile.insert(QStringLiteral("curve"), QStringLiteral("0.33,1,0.68,1"));
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), profile));

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("editor.snapIn"));

        const QString filePath = tmp.path() + QStringLiteral("/editor.snapIn.json");
        QVERIFY(QFileInfo::exists(filePath));

        // Verify the on-disk shape: name field present, Profile fields preserved.
        const auto doc = QJsonDocument::fromJson(readFile(filePath).toUtf8());
        QVERIFY(doc.isObject());
        const QJsonObject obj = doc.object();
        QCOMPARE(obj.value(QStringLiteral("name")).toString(), QStringLiteral("editor.snapIn"));
        QCOMPARE(obj.value(QStringLiteral("duration")).toInt(), 250);
        QCOMPARE(obj.value(QStringLiteral("curve")).toString(), QStringLiteral("0.33,1,0.68,1"));
    }

    void hasOverride_reflectsFileExistence()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(!c.hasOverride(QStringLiteral("editor.snapIn")));
        c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 200}});
        QVERIFY(c.hasOverride(QStringLiteral("editor.snapIn")));
    }

    void rawProfile_roundTripsProfileFieldsStripsName()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVariantMap input;
        input.insert(QStringLiteral("duration"), 175);
        input.insert(QStringLiteral("minDistance"), 8);
        // Caller-supplied `name` must be overwritten — rawProfile strips
        // `name` on read, so the only way this round-trips is if the
        // controller stamps the path on write.
        input.insert(QStringLiteral("name"), QStringLiteral("garbage"));
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), input));

        const QVariantMap raw = c.rawProfile(QStringLiteral("osd.show"));
        QVERIFY(!raw.contains(QStringLiteral("name")));
        QCOMPARE(raw.value(QStringLiteral("duration")).toInt(), 175);
        QCOMPARE(raw.value(QStringLiteral("minDistance")).toInt(), 8);
    }

    void clearOverride_removesFileAndEmits()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 200}});
        QVERIFY(c.hasOverride(QStringLiteral("osd.show")));

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(c.clearOverride(QStringLiteral("osd.show")));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("osd.show"));
        QVERIFY(!c.hasOverride(QStringLiteral("osd.show")));
    }

    void clearOverride_noFileReturnsFalseNoSignal()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(!c.clearOverride(QStringLiteral("editor.snapIn")));
        QCOMPARE(spy.count(), 0);
    }

    void setOverride_emptyPathRejected()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);
        QVERIFY(!c.setOverride(QString(), {{QStringLiteral("duration"), 100}}));
        QCOMPARE(spy.count(), 0);
    }

    // Backs the per-page "Reset to defaults" on the animation pages: clears
    // every per-event override file, returning them to built-in defaults.
    void clearAllOverrides_removesEveryOverrideFile()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        const QVariantMap profile{{QStringLiteral("duration"), 250},
                                  {QStringLiteral("curve"), QStringLiteral("0.33,1,0.68,1")}};
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), profile));
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), profile));
        QVERIFY(c.hasOverride(QStringLiteral("editor.snapIn")));
        QVERIFY(c.hasOverride(QStringLiteral("osd.show")));

        QCOMPARE(c.clearAllOverrides(), 2);
        QVERIFY(!c.hasOverride(QStringLiteral("editor.snapIn")));
        QVERIFY(!c.hasOverride(QStringLiteral("osd.show")));
        // Nothing left — a second sweep removes zero files.
        QCOMPARE(c.clearAllOverrides(), 0);
    }

    // Per-page kebab Reset: clearing ONE surface's scope must leave every other
    // surface's override files standing (the cross-page-isolation bug fix).
    void clearOverridesUnder_clearsOnlyScopedFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        const QVariantMap profile{{QStringLiteral("duration"), 250}};
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), profile));
        QVERIFY(c.setOverride(QStringLiteral("window.appearance.open"), profile));

        // Scope = the OSDs page (osd + its leaves). window.* must survive.
        const QStringList osdScope{QStringLiteral("osd"), QStringLiteral("osd.show"), QStringLiteral("osd.pop"),
                                   QStringLiteral("osd.hide")};
        QCOMPARE(c.clearOverridesUnder(osdScope), 1);
        QVERIFY(!c.hasOverride(QStringLiteral("osd.show")));
        QVERIFY(c.hasOverride(QStringLiteral("window.appearance.open")));
        // Re-running over the same scope now clears nothing.
        QCOMPARE(c.clearOverridesUnder(osdScope), 0);
    }

    // Per-page kebab Discard: reverting ONE surface's scope restores only that
    // surface's files and leaves the others staged (still pending).
    void revertPendingUnder_restoresOnlyScopedFiles()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        const QVariantMap profile{{QStringLiteral("duration"), 250}};
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), profile));
        QVERIFY(c.setOverride(QStringLiteral("window.appearance.open"), profile));
        QVERIFY(c.hasPendingChanges());

        const QStringList osdScope{QStringLiteral("osd"), QStringLiteral("osd.show"), QStringLiteral("osd.pop"),
                                   QStringLiteral("osd.hide")};
        const QStringList windowScope{QStringLiteral("window.appearance"), QStringLiteral("window.appearance.open")};

        // The OSD file did not exist before this session, so reverting removes it;
        // the window edit stays pending and on disk.
        QVERIFY(c.revertPendingUnder(osdScope));
        QVERIFY(!c.hasOverride(QStringLiteral("osd.show")));
        QVERIFY(!c.hasScopedPendingFiles(osdScope));
        QVERIFY(c.hasOverride(QStringLiteral("window.appearance.open")));
        QVERIFY(c.hasScopedPendingFiles(windowScope));
        QVERIFY(c.hasPendingChanges());
    }

    // hasScopedPendingFiles reports the file half of a per-page dirty check and
    // must ignore edits outside the queried scope.
    void hasScopedPendingFiles_reflectsOnlyScope()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        const QStringList osdScope{QStringLiteral("osd"), QStringLiteral("osd.show")};
        const QStringList editorScope{QStringLiteral("editor"), QStringLiteral("editor.snapIn")};

        QVERIFY(!c.hasScopedPendingFiles(osdScope));
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 100}}));
        QVERIFY(c.hasScopedPendingFiles(osdScope));
        QVERIFY(!c.hasScopedPendingFiles(editorScope));
    }

    // ─── Effective resolution ─────────────────────────────────────────────

    void resolvedProfileSeedsTheRootFromGlobalSettings()
    {
        // The Global settings seed sits at the ROOT of the inheritance chain,
        // below every user-authored profile. Every other resolvedProfile test
        // constructs the controller with no ISettings, so the seed is skipped
        // entirely and the suite stayed green through the whole of it.
        //
        // All FIVE seeded fields are asserted, not just minDistance. The seed
        // was written once for duration+curve alone and had to be widened when
        // the other three were found resolving to library defaults while the
        // daemon animated with the user's value; a test that pinned one field
        // would let any of the other four be dropped again silently. duration
        // and curve in particular are exactly what the simple page's lead
        // GlobalTimingDefaultsCard drives.
        IsolatedConfigGuard guard;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        Settings s;
        s.setAnimationDuration(275);
        s.setAnimationEasingCurve(QStringLiteral("0.10,0.20,0.30,0.40"));
        s.setAnimationMinDistance(123);
        s.setAnimationSequenceMode(int(PhosphorAnimation::SequenceMode::Cascade));
        s.setAnimationStaggerInterval(55);
        AnimationsPageController c(nullptr, &s);
        c.setUserProfilesDirOverride(tmp.path());

        // Every value above must differ from the library default it would fall
        // back to, or the assertions below would pass with the seed deleted.
        // Asserted rather than assumed: a later change to a Default* constant
        // could quietly turn one of these rows into a tautology.
        using P = PhosphorAnimation::Profile;
        QVERIFY(s.animationDuration() != int(P::DefaultDuration));
        QVERIFY(s.animationEasingCurve() != PhosphorAnimation::Easing().toString());
        QVERIFY(s.animationMinDistance() != P::DefaultMinDistance);
        QVERIFY(s.animationSequenceMode() != int(P::DefaultSequenceMode));
        QVERIFY(s.animationStaggerInterval() != P::DefaultStaggerInterval);

        // No override anywhere on the chain: each settings value reaches the
        // leaf instead of the library default. Compared against the Settings
        // getter rather than the literal written above so a setter-side clamp
        // or canonicalisation cannot turn a real mismatch into a test failure
        // that says nothing about the seed.
        const QVariantMap seeded = c.resolvedProfile(QStringLiteral("editor.snapIn"));
        QCOMPARE(seeded.value(QStringLiteral("duration")).toInt(), s.animationDuration());
        QCOMPARE(seeded.value(QStringLiteral("curve")).toString(), s.animationEasingCurve());
        QCOMPARE(seeded.value(QStringLiteral("minDistance")).toInt(), s.animationMinDistance());
        QCOMPARE(seeded.value(QStringLiteral("sequenceMode")).toInt(), s.animationSequenceMode());
        QCOMPARE(seeded.value(QStringLiteral("staggerInterval")).toInt(), s.animationStaggerInterval());

        // A real override at the leaf still wins for EVERY seeded field: the
        // seed is LOWEST precedence, so mergeMissingFields must never
        // overwrite it. Each value differs from both the settings value above
        // and the library default.
        c.setOverride(QStringLiteral("editor.snapIn"),
                      QVariantMap{{QStringLiteral("duration"), 611},
                                  {QStringLiteral("curve"), QStringLiteral("spring:14.0,0.6")},
                                  {QStringLiteral("minDistance"), 7},
                                  {QStringLiteral("sequenceMode"), int(PhosphorAnimation::SequenceMode::AllAtOnce)},
                                  {QStringLiteral("staggerInterval"), 91}});
        const QVariantMap overridden = c.resolvedProfile(QStringLiteral("editor.snapIn"));
        QCOMPARE(overridden.value(QStringLiteral("duration")).toInt(), 611);
        QCOMPARE(overridden.value(QStringLiteral("curve")).toString(), QStringLiteral("spring:14.0,0.6"));
        QCOMPARE(overridden.value(QStringLiteral("minDistance")).toInt(), 7);
        QCOMPARE(overridden.value(QStringLiteral("sequenceMode")).toInt(),
                 int(PhosphorAnimation::SequenceMode::AllAtOnce));
        QCOMPARE(overridden.value(QStringLiteral("staggerInterval")).toInt(), 91);
    }

    void resolvedProfile_unsetReturnsLibraryDefaults()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // No registry, no files. Walk falls through to library defaults.
        const QVariantMap resolved = c.resolvedProfile(QStringLiteral("editor.snapIn"));
        using P = PhosphorAnimation::Profile;
        QCOMPARE(resolved.value(QStringLiteral("duration")).toDouble(), P::DefaultDuration);
        QCOMPARE(resolved.value(QStringLiteral("minDistance")).toInt(), P::DefaultMinDistance);
        QCOMPARE(resolved.value(QStringLiteral("sequenceMode")).toInt(), int(P::DefaultSequenceMode));
        QCOMPARE(resolved.value(QStringLiteral("staggerInterval")).toInt(), P::DefaultStaggerInterval);
    }

    void resolvedProfile_walksParentChain()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Override the parent (editor) but not the leaf (editor.snapIn). The
        // leaf must inherit the parent's duration via walk-up.
        QVERIFY(c.setOverride(QStringLiteral("editor"), {{QStringLiteral("duration"), 222}}));

        const QVariantMap resolved = c.resolvedProfile(QStringLiteral("editor.snapIn"));
        QCOMPARE(resolved.value(QStringLiteral("duration")).toInt(), 222);
    }

    void resolvedProfile_leafOverridesParent()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(c.setOverride(QStringLiteral("editor"), {{QStringLiteral("duration"), 100}}));
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 333}}));

        const QVariantMap resolved = c.resolvedProfile(QStringLiteral("editor.snapIn"));
        QCOMPARE(resolved.value(QStringLiteral("duration")).toInt(), 333);
    }

    void resolvedProfile_partialLeafFillsFromParentAndDefaults()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Parent supplies curve only; leaf supplies duration only.
        // Resolved should have BOTH from their respective sources, plus
        // library defaults for everything else.
        QVERIFY(
            c.setOverride(QStringLiteral("editor"), {{QStringLiteral("curve"), QStringLiteral("spring:14.0,0.6")}}));
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), {{QStringLiteral("duration"), 444}}));

        const QVariantMap resolved = c.resolvedProfile(QStringLiteral("editor.snapIn"));
        QCOMPARE(resolved.value(QStringLiteral("duration")).toInt(), 444);
        QCOMPARE(resolved.value(QStringLiteral("curve")).toString(), QStringLiteral("spring:14.0,0.6"));
        // Library default for the unspecified field:
        QCOMPARE(resolved.value(QStringLiteral("minDistance")).toInt(), PhosphorAnimation::Profile::DefaultMinDistance);
    }

    // ─── Path traversal hardening (security) ──────────────────────────────

    /// `setOverride` MUST reject any path that isn't a built-in event
    /// path. A crafted `"../etc/passwd"` would otherwise let a hostile
    /// QML caller write outside `userProfilesDir()`.
    void setOverride_rejectsTraversalPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QSignalSpy spy(&c, &AnimationsPageController::overrideChanged);

        QVERIFY(!c.setOverride(QStringLiteral("../etc/passwd"), {{QStringLiteral("duration"), 100}}));
        QVERIFY(!c.setOverride(QStringLiteral("../../bad"), {{QStringLiteral("duration"), 100}}));
        QVERIFY(!c.setOverride(QStringLiteral("..\\windows-path"), {{QStringLiteral("duration"), 100}}));
        QVERIFY(!c.setOverride(QStringLiteral("editor/../../etc"), {{QStringLiteral("duration"), 100}}));
        // Plausible-looking but unknown paths are also rejected
        // (membership in allBuiltInPaths is the gate, not just lexical
        // shape).
        QVERIFY(!c.setOverride(QStringLiteral("not.a.real.path"), {{QStringLiteral("duration"), 100}}));

        QCOMPARE(spy.count(), 0);

        // No file landed anywhere under the tmp dir.
        QDir scan(tmp.path());
        const auto entries = scan.entryList(QDir::Files | QDir::NoDotAndDotDot);
        QVERIFY2(entries.isEmpty(),
                 qPrintable(QStringLiteral("traversal write leaked: ") + entries.join(QLatin1Char(','))));
    }

    /// `clearOverride` and `hasOverride` MUST also reject non-event paths
    /// — they share the same path → file-path mapping as setOverride.
    void clearAndHasOverride_rejectTraversalPaths()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        QVERIFY(!c.hasOverride(QStringLiteral("../etc/passwd")));
        QVERIFY(!c.clearOverride(QStringLiteral("../etc/passwd")));
        QVERIFY(!c.hasOverride(QStringLiteral("not.a.real.path")));
        QVERIFY(!c.clearOverride(QStringLiteral("not.a.real.path")));
    }

    // ─── Pass-1 behaviour pins ────────────────────────────────────────────

    /// `setOverride` compare-and-skip: writing the same Profile twice
    /// must not fire a second `overrideChanged` / `pendingChangesChanged`.
    /// Pre-fix the second write rewrote an identical file and lit Save.
    void setOverride_repeatedWriteIsNoOp()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        const auto profile = QVariantMap{{QStringLiteral("duration"), 250},
                                         {QStringLiteral("curve"), QStringLiteral("0.33,1.00,0.68,1.00")}};

        QSignalSpy overrideSpy(&c, &AnimationsPageController::overrideChanged);
        QSignalSpy pendingSpy(&c, &AnimationsPageController::pendingChangesChanged);

        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), profile));
        QCOMPARE(overrideSpy.count(), 1);
        QCOMPARE(pendingSpy.count(), 1);

        // Second identical write — must short-circuit, no extra signals.
        QVERIFY(c.setOverride(QStringLiteral("editor.snapIn"), profile));
        QCOMPARE(overrideSpy.count(), 1);
        QCOMPARE(pendingSpy.count(), 1);
    }

    // ─── Shader-leg support gate ──────────────────────────────────────────

    /// Pin the shader-leg-support predicate against the call sites that
    /// actually consume a leg. `supportsShaderLeg` is the predicate the QML
    /// shader-picker visibility is bound to; if the surface set in
    /// `src/core/animationshadersupportedpaths.h` drifts away from those call
    /// sites, the QML would either expose pickers that do nothing (drift in one
    /// direction) or hide pickers for events that DO produce shader legs (drift
    /// in the other). Per that header, consumption runs through three
    /// mechanisms, not just the daemon's: `resolveShaderEffect` from
    /// `OverlayService::buildOsdConfig` / `buildLayoutPickerConfig` /
    /// `buildZoneSelectorConfig` / `buildSnapAssistConfig` (osd + popup
    /// families), `tryBeginShaderForEvent` under
    /// `kwin-effect/plasmazoneseffect/` (window_lifecycle for
    /// open/close/move/maximize/focus, daemon_apply for minimize), and
    /// `resolveShaderWithDefault`, driving both DesktopTransitionManager from
    /// `kwin-effect/plasmazoneseffect/lifecycle.cpp` (the screen-level desktop
    /// switch and peek legs) and the snap geometry legs through
    /// `applyWindowGeometry` in `kwin-effect/plasmazoneseffect/drag_snap.cpp`.
    void supportsShaderLeg_matchesConsumedLegCallSites()
    {
        AnimationsPageController c;

        // Genuine OSDs (consumed leaves).
        QVERIFY(c.supportsShaderLeg(QStringLiteral("osd.show")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("osd.hide")));

        // Popup family — leg-leaf paths (consumed leaves).
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.layoutPicker.show")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.layoutPicker.hide")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.zoneSelector.show")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.zoneSelector.hide")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.snapAssist.show")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.snapAssist.hide")));

        // Window family — consumed leaves driven by the KWin effect under
        // kwin-effect/plasmazoneseffect/. The lifecycle legs go through
        // tryBeginShaderForEvent (window_lifecycle for windowAdded,
        // windowClosed, windowStartUserMovedResized for the held move,
        // windowMaximizedStateChanged and windowActivated; daemon_apply for
        // minimizedChanged), while the snap geometry legs resolve through
        // applyWindowGeometry in drag_snap.cpp. Both run the resolved shader
        // on the OffscreenEffect's redirected texture quad.
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.appearance.open")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.appearance.close")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.appearance.minimize")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.movement.maximize")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.movement.move")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.movement.snapIn")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.movement.snapOut")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.movement.layoutSwitch")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.appearance.focus")));
        // The resize legs were dropped from the taxonomy: the interactive
        // edge-drag has no discrete before/after for a shader to play, and
        // snapResize never had a callsite. Stale config overrides on these
        // paths must prune, so they stay unsupported.
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("window.movement.resize")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("window.movement.snapResize")));
        // Desktop family — the two-texture switch and the show-desktop peek
        // are consumed leaves too (the KWin effect's DesktopTransitionManager
        // resolves them in the desktopChanged / showingDesktopChanged handlers,
        // not per-window tryBeginShaderForEvent legs).
        QVERIFY(c.supportsShaderLeg(QStringLiteral("desktop.switch")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("desktop.peek")));
        // The "All Desktop Events" parent row is the desktop family root, and
        // an ancestor of the consumed switch and peek leaves. It is
        // shader-pickable too (its picker binds to this), and a pack set there
        // cascades to both legs — so it is supported for the same reason the
        // popup/osd parents below are, not merely as an ancestor of a consumed
        // leaf.
        QVERIFY(c.supportsShaderLeg(QStringLiteral("desktop")));

        // Ancestors of consumed leaves — supported because the
        // resolver walks them on the way to the leaf, so a
        // shader override here cascades to every descendant. Without
        // this, the user would have to set the same shader on every
        // popup leaf individually instead of once at the parent.
        QVERIFY(c.supportsShaderLeg(QStringLiteral("global")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("osd")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.layoutPicker")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.zoneSelector")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("popup.snapAssist")));
        // `panel` is no longer a popup ancestor — popups moved to their
        // own root, leaving `panel` with only slideIn/slideOut/fadeIn/
        // fadeOut which the daemon's overlay service never consumes.
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("panel")));
        // `window` itself is now a consumable ancestor — setting a
        // shader at the family root cascades to every leaf above.
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window")));
        // The intermediate cascade parents the parent-card UX relies on.
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.movement")));
        QVERIFY(c.supportsShaderLeg(QStringLiteral("window.appearance")));

        // Paths the resolver never walks through — any assignment would
        // be runtime-dead and silently shadow what the user thought
        // they set on a sibling. Must stay unsupported.
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("editor")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("editor.snapIn")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("widget")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("widget.fadeIn")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("workspace")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("workspace.switchIn")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("cursor")));
        // Sibling paths under `panel` and `osd` that aren't ancestors
        // of any consumed leaf.
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("panel.slideIn")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("panel.slideOut")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("osd.pop")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("osd.dim")));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("popup.layoutPicker.popIn")));
        // Empty path / nonsense path.
        QVERIFY(!c.supportsShaderLeg(QString()));
        QVERIFY(!c.supportsShaderLeg(QStringLiteral("../etc/passwd")));
    }

    // ─── Stock-animation suppression mirror ───────────────────────────────

    /// `stockSuppressedEvents` is the settings-side mirror of the
    /// compositor's syncStockEffectSuppression ownership gate; the rule
    /// editor's stock-animation conflict chip hides for listed events. Pin
    /// the gate's three inputs: tree assignment (a tree-resolved effectId
    /// on the minimize/maximize path lists the event; with a null registry
    /// the unknown id gets the warm-up grace and counts as owned), the
    /// animations master toggle (off collapses the list to empty), and the
    /// NOTIFY wiring (both inputs fire stockSuppressedEventsChanged so the
    /// chip bindings re-evaluate).
    void stockSuppressedEvents_reflectsTreeAndMasterToggle()
    {
        IsolatedConfigGuard guard;
        Settings s;
        s.setAnimationsEnabled(true);
        AnimationsPageController c(nullptr, &s);

        // No tree assignment: nothing suppressed, no conflict-free events.
        QVERIFY(c.stockSuppressedEvents().isEmpty());

        QSignalSpy spy(&c, &AnimationsPageController::stockSuppressedEventsChanged);

        // Tree-assign a minimize pack. The controller was built with a null
        // registry, so the effectId is unknown and the warm-up grace counts
        // it as owned (mirroring resolvedShaderProfile's grace).
        PhosphorAnimationShaders::ShaderProfileTree tree;
        PhosphorAnimationShaders::ShaderProfile profile;
        profile.effectId = QStringLiteral("genie");
        tree.setOverride(PhosphorAnimation::ProfilePaths::WindowMinimize, profile);
        s.setShaderProfileTree(tree);

        QVERIFY2(spy.count() >= 1, "tree change must notify the chip bindings");
        // Minimize is owned; maximize (unassigned) is not.
        QCOMPARE(c.stockSuppressedEvents(), QStringList{PhosphorAnimation::ProfilePaths::WindowMinimize});

        // Assigning the maximize path too lists both events.
        tree.setOverride(PhosphorAnimation::ProfilePaths::WindowMaximize, profile);
        s.setShaderProfileTree(tree);
        QCOMPARE(c.stockSuppressedEvents(),
                 (QStringList{PhosphorAnimation::ProfilePaths::WindowMinimize,
                              PhosphorAnimation::ProfilePaths::WindowMaximize}));

        // Master toggle off gates the whole predicate: nothing is owned and
        // the change is notified, so every chip comes back.
        const int beforeToggle = spy.count();
        s.setAnimationsEnabled(false);
        QVERIFY2(spy.count() > beforeToggle, "master-toggle flip must notify the chip bindings");
        QVERIFY(c.stockSuppressedEvents().isEmpty());

        // Toggle back on: ownership returns.
        s.setAnimationsEnabled(true);
        QCOMPARE(c.stockSuppressedEvents(),
                 (QStringList{PhosphorAnimation::ProfilePaths::WindowMinimize,
                              PhosphorAnimation::ProfilePaths::WindowMaximize}));
    }

    /// Registry-backed half of the gate: a KNOWN pack is owned only when its
    /// contract class applies to the event (the compositor refuses to run a
    /// mismatched pack, so the stock effect stays loaded and the conflict
    /// chip must show), and a committed registry rescan fires the third
    /// NOTIFY input so the chip bindings re-evaluate.
    void stockSuppressedEvents_registryContractGateAndNotify()
    {
        IsolatedConfigGuard guard;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        // Universal pack (no appliesTo) applies to the appearance-class
        // minimize path; the desktop-only pack is a two-texture contract
        // that never runs on a single-surface window event.
        QVERIFY(writeAnimationPack(tmp.path(), QStringLiteral("universal-pack"), {}));
        QVERIFY(writeAnimationPack(tmp.path(), QStringLiteral("desktop-pack"), QJsonArray{QStringLiteral("desktop")}));
        PhosphorAnimationShaders::AnimationShaderRegistry registry;
        registry.addSearchPaths(QStringList{tmp.path()}, PhosphorFsLoader::LiveReload::Off);
        QVERIFY(registry.hasEffect(QStringLiteral("universal-pack")));
        QVERIFY(registry.hasEffect(QStringLiteral("desktop-pack")));

        Settings s;
        s.setAnimationsEnabled(true);
        AnimationsPageController c(&registry, &s);
        QSignalSpy spy(&c, &AnimationsPageController::stockSuppressedEventsChanged);

        // Known, class-compatible pack: owned.
        PhosphorAnimationShaders::ShaderProfileTree tree;
        PhosphorAnimationShaders::ShaderProfile profile;
        profile.effectId = QStringLiteral("universal-pack");
        tree.setOverride(PhosphorAnimation::ProfilePaths::WindowMinimize, profile);
        s.setShaderProfileTree(tree);
        QCOMPARE(c.stockSuppressedEvents(), QStringList{PhosphorAnimation::ProfilePaths::WindowMinimize});

        // Known, class-INCOMPATIBLE pack: not owned, chip stays visible.
        profile.effectId = QStringLiteral("desktop-pack");
        tree.setOverride(PhosphorAnimation::ProfilePaths::WindowMinimize, profile);
        s.setShaderProfileTree(tree);
        QVERIFY(c.stockSuppressedEvents().isEmpty());

        // Third NOTIFY input: a committed rescan (a new pack changes the
        // scan fingerprint) re-fires the chip rebind signal.
        const int beforeRescan = spy.count();
        QVERIFY(writeAnimationPack(tmp.path(), QStringLiteral("late-pack"), {}));
        registry.refresh();
        QTRY_VERIFY2(spy.count() > beforeRescan, "registry rescan must notify the chip bindings");
    }
};

QTEST_MAIN(TestAnimationsPageController)
#include "test_animations_page_controller.moc"
