// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_page_controller.cpp
 * @brief AnimationsPageController behaviour pins: path discovery,
 *        override CRUD, the batch and scoped dirty-state announcements,
 *        the path-traversal gate, the disk-read normalisation, the
 *        shader-leg support gate, the stock-animation suppression mirror,
 *        and the spring-slider bounds.
 *
 * Pins the file-per-path persistence model: setOverride writes one JSON
 * file under `<userProfilesDir>/<path>.json`, clearOverride deletes it,
 * resolvedProfile walks the parent chain and fills library defaults.
 * How that walk stays honest against the process-wide
 * PhosphorProfileRegistry is pinned by the companion
 * test_animations_profile_store_sync.cpp.
 * The suppression-mirror slots pin `stockSuppressedEvents` (the
 * settings-side twin of the compositor's syncStockEffectSuppression
 * ownership gate) and its NOTIFY inputs.
 *
 * Uses `setUserProfilesDirOverride()` to redirect override-file I/O into
 * a tmpdir, and `IsolatedConfigGuard` where a real Settings is needed, so
 * the test never touches the real user XDG dirs.
 *
 * Companion test files:
 *   - test_animations_qml_contracts.cpp    — QML↔controller contracts, scraped
 *                                            from the QML source
 *   - test_animations_profile_store_sync.cpp — registry/disk staleness
 *   - test_animations_motion_sets.cpp      — motion-set CRUD and async discard
 *   - test_animations_presets.cpp          — the user-preset library
 *   - test_animations_shader_overrides.cpp — shader-effect overrides
 *   - test_animation_page_scope.cpp        — the page-scope root tables
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
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ShaderProfile.h>
#include <PhosphorAnimation/ShaderProfileTree.h>
#include <PhosphorAnimation/Spring.h>

#include "config/settings.h"
#include "helpers/IsolatedConfigGuard.h"
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

    /// The registry-backed slots below publish a stack-local registry as the
    /// PROCESS-WIDE default. Catch a leaked publish at its source rather than
    /// as a mystery failure in whichever slot happens to run next — the same
    /// guard libs/phosphor-animation/tests/test_phosphorprofileregistry.cpp
    /// uses.
    void init()
    {
        QCOMPARE(PhosphorAnimation::PhosphorProfileRegistry::defaultRegistry(), nullptr);
    }

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

        // Every listed path is a built-in path. Collected then asserted once,
        // like the `outOfScope` and `unreachable` checks further down: QVERIFY
        // aborts the whole slot, so asserting inside the loop would report only
        // the first unknown path and hide the rest, making a taxonomy widening
        // a one-at-a-time hunt.
        QStringList unknown;
        for (const QString& p : allListed) {
            if (!paths.contains(p)) {
                unknown.append(p);
            }
        }
        QVERIFY2(unknown.isEmpty(),
                 qPrintable(QStringLiteral("unknown paths in UI: ") + unknown.join(QStringLiteral(", "))));
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

    /// A batch clear announces its NET dirty state, never an intermediate one.
    ///
    /// The trap: the first path's staged snapshot is a phantom ("was absent"),
    /// so removing it empties the snapshot map and flips the page clean; a
    /// later path in the same batch then stages a REAL snapshot and the page
    /// ends dirty again. If the flip mid-loop is announced, the last thing the
    /// page heard is "clean" while there is still an edit to discard — the
    /// unsaved-changes footer disappears with restorable content behind it.
    /// The endpoints are equal, so no terminal signal corrects it.
    void batchClearAnnouncesTheNetDirtyStateNotAnIntermediateOne()
    {
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Staged this session, so its snapshot is "was absent" and removing it
        // drops the entry.
        QVERIFY(c.setOverride(QStringLiteral("global"), {{QStringLiteral("duration"), 300}}));
        // Placed directly on disk: pre-existing, unstaged, so clearing it
        // stages a real snapshot the user can still discard back.
        QFile prior(tmp.path() + QStringLiteral("/editor.snapIn.json"));
        QVERIFY(prior.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(prior.write(QByteArrayLiteral(R"({"name":"editor.snapIn","duration":250})")) > 0);
        prior.close();

        // pendingChangesChanged carries no payload, so record the state the
        // page would read at each emission and check the LAST one.
        QList<bool> announced;
        connect(&c, &AnimationsPageController::pendingChangesChanged, &c, [&c, &announced]() {
            announced.append(c.hasPendingChanges());
        });

        // Explicit path order: the phantom first, the real snapshot second.
        QCOMPARE(c.clearOverridesUnder(QStringList{QStringLiteral("global"), QStringLiteral("editor.snapIn")}), 2);

        QVERIFY(c.hasPendingChanges());
        // Whatever was announced, the last of it must describe where the batch
        // actually ended. Endpoints match here (dirty → dirty), so the correct
        // behaviour is to say nothing at all.
        QVERIFY2(announced.isEmpty(), "a batch that started and ended dirty announced an intermediate flip");

        // Second scenario, endpoints DIFFERING, so a signal IS owed. Without
        // this leg the assertion above would also pass if the batch stopped
        // announcing anything at all. Start from clean: discard the staged
        // edit above, then stage one fresh override whose snapshot is a
        // phantom, so clearing it empties the map and ends the batch clean.
        QVERIFY(c.revertPending());
        QVERIFY(!c.hasPendingChanges());
        QVERIFY(c.setOverride(QStringLiteral("osd.show"), {{QStringLiteral("duration"), 400}}));
        QVERIFY(c.hasPendingChanges());

        announced.clear();
        QCOMPARE(c.clearOverridesUnder(QStringList{QStringLiteral("osd.show")}), 1);
        QVERIFY(!c.hasPendingChanges());
        QCOMPARE(announced.size(), 1);
        QCOMPARE(announced.last(), false);
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

    /// The profiles directory is a filesystem boundary a user can hand-place
    /// anything at, and the disk-first resolve reads it without going through
    /// `Profile::fromJson`. Every field that fromJson would reject has to be
    /// resolved the same way HERE, or the settings app shows a value the daemon
    /// will never animate.
    ///
    /// Drop-versus-substitute is the load-bearing distinction and is asserted
    /// separately per field, because `mergeMissingFields` only fills keys that
    /// are ABSENT: dropping a key lets the ancestor's value through, while
    /// substituting the library default blocks inheritance at this level.
    void resolvedProfile_rejectsFieldsProfileFromJsonWouldReject_data()
    {
        using P = PhosphorAnimation::Profile;
        QTest::addColumn<QByteArray>("field");
        QTest::addColumn<QByteArray>("leafJson");
        QTest::addColumn<QVariant>("parentValue");
        QTest::addColumn<QVariant>("expectedResolved");
        QTest::addColumn<bool>("leafOwnsField");

        const QByteArray kDur = QByteArrayLiteral("duration");
        const QByteArray kMin = QByteArrayLiteral("minDistance");
        const QByteArray kStag = QByteArrayLiteral("staggerInterval");
        const QByteArray kCurve = QByteArrayLiteral("curve");
        const QByteArray kPreset = QByteArrayLiteral("presetName");
        const QVariant kParentCurve = QVariant(QStringLiteral("0.4,0,0.2,1"));

        // ── duration: out-of-range NUMBERS are dropped, so the parent shows ──
        QTest::newRow("duration negative")
            << kDur << QByteArrayLiteral(R"({"duration":-50})") << QVariant(123) << QVariant(123) << false;
        QTest::newRow("duration zero") << kDur << QByteArrayLiteral(R"({"duration":0})") << QVariant(123)
                                       << QVariant(123) << false;
        QTest::newRow("duration over max")
            << kDur << QByteArrayLiteral(R"({"duration":1e9})") << QVariant(123) << QVariant(123) << false;
        QTest::newRow("duration astronomical")
            << kDur << QByteArrayLiteral(R"({"duration":1e300})") << QVariant(123) << QVariant(123) << false;

        // A NON-NUMBER is dropped too, and both sides type-check to make that
        // so.
        //
        // These two duration rows do NOT pin the type check, though — they ride
        // the range check. A JSON string or bool coerces to 0, and 0 fails
        // duration's `> 0` bound, so they resolve to the parent with or without
        // the `isDouble()` guard. The rows that actually pin it in THIS
        // translation unit are `minDistance string` and `stagger string`, whose
        // bounds admit 0. The library-side guard has its own coverage in
        // libs/phosphor-animation/tests/test_profile.cpp
        // (testFromJsonLeavesNonNumericFieldsUnset). Keep these two rows: the
        // behaviour they assert is still correct, only the rationale was wrong.
        QTest::newRow("duration string") << kDur << QByteArrayLiteral(R"({"duration":"900"})") << QVariant(123)
                                         << QVariant(123) << false;
        QTest::newRow("duration bool") << kDur << QByteArrayLiteral(R"({"duration":true})") << QVariant(123)
                                       << QVariant(123) << false;
        QTest::newRow("duration valid") << kDur << QByteArrayLiteral(R"({"duration":900})") << QVariant(123)
                                        << QVariant(900) << true;
        // The accepted side of duration's shared bound, for the same reason as
        // the minDistance cap row below.
        QTest::newRow("duration at the domain cap") << kDur << QByteArrayLiteral(R"({"duration":3600000})")
                                                    << QVariant(123) << QVariant(int(P::MaxDurationMs)) << true;

        // ── minDistance: same split. The 1e300 row pins the bound-before-round
        // guard — `qRound` on an unbounded finite double is undefined
        // behaviour, so the value must be rejected by RANGE first.
        QTest::newRow("minDistance negative")
            << kMin << QByteArrayLiteral(R"({"minDistance":-5})") << QVariant(7) << QVariant(7) << false;
        // The bound both sides share. Before this row the two disagreed: the
        // library capped at MaxMinDistancePx while the settings mirror still
        // capped at INT_MAX, so a hand-placed 200000 showed in the card and
        // blocked inheritance while the daemon dropped it.
        QTest::newRow("minDistance over the domain cap")
            << kMin << QByteArrayLiteral(R"({"minDistance":200000})") << QVariant(7) << QVariant(7) << false;
        QTest::newRow("minDistance astronomical")
            << kMin << QByteArrayLiteral(R"({"minDistance":1e300})") << QVariant(7) << QVariant(7) << false;
        QTest::newRow("minDistance string")
            << kMin << QByteArrayLiteral(R"({"minDistance":"5"})") << QVariant(7) << QVariant(7) << false;
        QTest::newRow("minDistance valid")
            << kMin << QByteArrayLiteral(R"({"minDistance":5})") << QVariant(7) << QVariant(5) << true;
        // The ACCEPTED side of the shared bound. Without it, flipping either
        // implementation's `<=` to `<` rejects exactly MaxMinDistancePx on one
        // side only, and every other row still passes.
        QTest::newRow("minDistance at the domain cap") << kMin << QByteArrayLiteral(R"({"minDistance":100000})")
                                                       << QVariant(7) << QVariant(int(P::MaxMinDistancePx)) << true;

        // ── staggerInterval ─────────────────────────────────────────────────
        QTest::newRow("stagger negative")
            << kStag << QByteArrayLiteral(R"({"staggerInterval":-1})") << QVariant(40) << QVariant(40) << false;
        QTest::newRow("stagger over max")
            << kStag << QByteArrayLiteral(R"({"staggerInterval":1e9})") << QVariant(40) << QVariant(40) << false;
        QTest::newRow("stagger string") << kStag << QByteArrayLiteral(R"({"staggerInterval":"20"})") << QVariant(40)
                                        << QVariant(40) << false;
        QTest::newRow("stagger valid") << kStag << QByteArrayLiteral(R"({"staggerInterval":20})") << QVariant(40)
                                       << QVariant(20) << true;

        // ── curve ───────────────────────────────────────────────────────────
        // A non-string is dropped, matching fromJson (whose `toString()` yields
        // empty and leaves the field unset).
        QTest::newRow("curve non-string")
            << kCurve << QByteArrayLiteral(R"({"curve":42})") << kParentCurve << kParentCurve << false;
        // The one deliberate divergence from fromJson, pinned so nobody "fixes"
        // it without reading why: an unresolvable-but-string spec is KEPT here,
        // because resolving it needs a CurveRegistry this translation unit does
        // not have, and dropping every unresolvable spec would drop legitimate
        // user-authored curves too. See `sanitizedProfileMap`.
        QTest::newRow("curve unresolvable string") << kCurve << QByteArrayLiteral(R"({"curve":"srping:14,0.6"})")
                                                   << kParentCurve << QVariant(QStringLiteral("srping:14,0.6")) << true;
        QTest::newRow("curve valid") << kCurve << QByteArrayLiteral(R"({"curve":"0.1,0,0.9,1"})") << kParentCurve
                                     << QVariant(QStringLiteral("0.1,0,0.9,1")) << true;

        // ── presetName ──────────────────────────────────────────────────────
        // Guarded on isString() for fromJson's reason: `toString()` on a
        // non-string engages the optional with an EMPTY value, and
        // engaged-empty means "explicit empty override", not "inherit".
        QTest::newRow("presetName non-string")
            << kPreset << QByteArrayLiteral(R"({"presetName":42})") << QVariant(QStringLiteral("Snappy"))
            << QVariant(QStringLiteral("Snappy")) << false;
        QTest::newRow("presetName valid")
            << kPreset << QByteArrayLiteral(R"({"presetName":"Gentle"})") << QVariant(QStringLiteral("Snappy"))
            << QVariant(QStringLiteral("Gentle")) << true;
    }

    void resolvedProfile_rejectsFieldsProfileFromJsonWouldReject()
    {
        QFETCH(QByteArray, field);
        QFETCH(QByteArray, leafJson);
        QFETCH(QVariant, parentValue);
        QFETCH(QVariant, expectedResolved);
        QFETCH(bool, leafOwnsField);

        const QString key = QString::fromUtf8(field);

        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // The parent carries a value the leaf can fall back to, so "rejected"
        // and "kept" produce visibly different results.
        QVERIFY(c.setOverride(QStringLiteral("editor"), {{key, parentValue}}));

        QFile leaf(tmp.path() + QStringLiteral("/editor.snapIn.json"));
        QVERIFY(leaf.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(leaf.write(leafJson), qint64(leafJson.size()));
        leaf.close();

        const QVariant resolved = c.resolvedProfile(QStringLiteral("editor.snapIn")).value(key);
        if (expectedResolved.typeId() == QMetaType::QString) {
            QCOMPARE(resolved.toString(), expectedResolved.toString());
        } else {
            QCOMPARE(resolved.toInt(), expectedResolved.toInt());
        }

        // rawProfile is normalised on the same terms, so a card reports field
        // ownership the same way the resolve does — no live revert link beside
        // a value the event does not actually own.
        QCOMPARE(c.rawProfile(QStringLiteral("editor.snapIn")).contains(key), leafOwnsField);
    }

    /// `sequenceMode` is the one field `Profile::fromJson` SUBSTITUTES rather
    /// than leaves unset, so an unknown enumerator must block inheritance at the
    /// library default instead of falling through to the parent's mode. Getting
    /// this backwards is invisible until a cascade animation plays wrong.
    void resolvedProfile_unknownSequenceModeSubstitutesRatherThanInherits()
    {
        using P = PhosphorAnimation::Profile;
        QTemporaryDir tmp;
        QVERIFY(tmp.isValid());
        AnimationsPageController c;
        c.setUserProfilesDirOverride(tmp.path());

        // Parent asks for Cascade; the leaf's mode is nonsense.
        QVERIFY(c.setOverride(QStringLiteral("editor"),
                              {{QStringLiteral("sequenceMode"), int(PhosphorAnimation::SequenceMode::Cascade)}}));
        const QByteArray leafJson = QByteArrayLiteral(R"({"name":"editor.snapIn","sequenceMode":7})");
        QFile leaf(tmp.path() + QStringLiteral("/editor.snapIn.json"));
        QVERIFY(leaf.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(leaf.write(leafJson), qint64(leafJson.size()));
        leaf.close();

        QCOMPARE(c.resolvedProfile(QStringLiteral("editor.snapIn")).value(QStringLiteral("sequenceMode")).toInt(),
                 int(P::DefaultSequenceMode));
        QVERIFY(int(P::DefaultSequenceMode) != int(PhosphorAnimation::SequenceMode::Cascade));
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

        // Collected, not asserted one at a time. A taxonomy change can flip
        // several paths at once, and a bare QVERIFY aborts the whole slot on
        // the first, so the rest surface one rebuild apart. Same
        // collect-then-assert-once shape the other broad checks in this file
        // use.
        QStringList wrong;
        const auto expect = [&c, &wrong](const QString& path, bool supported) {
            if (c.supportsShaderLeg(path) == supported)
                return;
            wrong.append(QStringLiteral("%1 (expected %2)")
                             .arg(path.isEmpty() ? QStringLiteral("<empty>") : path,
                                  supported ? QStringLiteral("supported") : QStringLiteral("unsupported")));
        };

        // Genuine OSDs (consumed leaves).
        expect(QStringLiteral("osd.show"), true);
        expect(QStringLiteral("osd.hide"), true);

        // Popup family — leg-leaf paths (consumed leaves).
        expect(QStringLiteral("popup.layoutPicker.show"), true);
        expect(QStringLiteral("popup.layoutPicker.hide"), true);
        expect(QStringLiteral("popup.zoneSelector.show"), true);
        expect(QStringLiteral("popup.zoneSelector.hide"), true);
        expect(QStringLiteral("popup.snapAssist.show"), true);
        expect(QStringLiteral("popup.snapAssist.hide"), true);
        // The cheatsheet family, consumed by buildCheatsheetConfig. Added
        // after this slot was found to have drifted from its source of truth:
        // because the list below is a hand-maintained allowlist, an ADDED
        // supported path is structurally invisible to it, and the cheatsheet
        // leaves are the proof that happens.
        expect(QStringLiteral("popup.cheatsheet.show"), true);
        expect(QStringLiteral("popup.cheatsheet.hide"), true);

        // Window family — consumed leaves driven by the KWin effect under
        // kwin-effect/plasmazoneseffect/. The lifecycle legs go through
        // tryBeginShaderForEvent (window_lifecycle for windowAdded,
        // windowClosed, windowStartUserMovedResized for the held move,
        // windowMaximizedStateChanged and windowActivated; daemon_apply for
        // minimizedChanged), while the snap geometry legs resolve through
        // applyWindowGeometry in drag_snap.cpp. Both run the resolved shader
        // on the OffscreenEffect's redirected texture quad.
        expect(QStringLiteral("window.appearance.open"), true);
        expect(QStringLiteral("window.appearance.close"), true);
        expect(QStringLiteral("window.appearance.minimize"), true);
        expect(QStringLiteral("window.movement.maximize"), true);
        expect(QStringLiteral("window.movement.move"), true);
        expect(QStringLiteral("window.movement.snapIn"), true);
        expect(QStringLiteral("window.movement.snapOut"), true);
        expect(QStringLiteral("window.movement.layoutSwitch"), true);
        expect(QStringLiteral("window.appearance.focus"), true);
        // The resize legs were dropped from the taxonomy: the interactive
        // edge-drag has no discrete before/after for a shader to play, and
        // snapResize never had a callsite. Stale config overrides on these
        // paths must prune, so they stay unsupported.
        expect(QStringLiteral("window.movement.resize"), false);
        expect(QStringLiteral("window.movement.snapResize"), false);
        // Desktop family — the two-texture switch and the show-desktop peek
        // are consumed leaves too (the KWin effect's DesktopTransitionManager
        // resolves them in the desktopChanged / showingDesktopChanged handlers,
        // not per-window tryBeginShaderForEvent legs).
        expect(QStringLiteral("desktop.switch"), true);
        expect(QStringLiteral("desktop.peek"), true);
        // The "All Desktop Events" parent row is the desktop family root, and
        // an ancestor of the consumed switch and peek leaves. It is
        // shader-pickable too (its picker binds to this), and a pack set there
        // cascades to both legs — so it is supported for the same reason the
        // popup/osd parents below are, not merely as an ancestor of a consumed
        // leaf.
        expect(QStringLiteral("desktop"), true);

        // Ancestors of consumed leaves — supported because the
        // resolver walks them on the way to the leaf, so a
        // shader override here cascades to every descendant. Without
        // this, the user would have to set the same shader on every
        // popup leaf individually instead of once at the parent.
        expect(QStringLiteral("global"), true);
        expect(QStringLiteral("osd"), true);
        expect(QStringLiteral("popup"), true);
        expect(QStringLiteral("popup.layoutPicker"), true);
        expect(QStringLiteral("popup.zoneSelector"), true);
        expect(QStringLiteral("popup.snapAssist"), true);
        expect(QStringLiteral("popup.cheatsheet"), true);
        // `panel` is no longer a popup ancestor — popups moved to their
        // own root, leaving `panel` with only slideIn/slideOut/fadeIn/
        // fadeOut which the daemon's overlay service never consumes.
        expect(QStringLiteral("panel"), false);
        // `window` itself is now a consumable ancestor — setting a
        // shader at the family root cascades to every leaf above.
        expect(QStringLiteral("window"), true);
        // The intermediate cascade parents the parent-card UX relies on.
        expect(QStringLiteral("window.movement"), true);
        expect(QStringLiteral("window.appearance"), true);

        // Paths the resolver never walks through — any assignment would
        // be runtime-dead and silently shadow what the user thought
        // they set on a sibling. Must stay unsupported.
        expect(QStringLiteral("editor"), false);
        expect(QStringLiteral("editor.snapIn"), false);
        expect(QStringLiteral("widget"), false);
        expect(QStringLiteral("widget.fadeIn"), false);
        expect(QStringLiteral("workspace"), false);
        expect(QStringLiteral("workspace.switchIn"), false);
        expect(QStringLiteral("cursor"), false);
        // Sibling paths under `panel` and `osd` that aren't ancestors
        // of any consumed leaf.
        expect(QStringLiteral("panel.slideIn"), false);
        expect(QStringLiteral("panel.slideOut"), false);
        expect(QStringLiteral("osd.pop"), false);
        expect(QStringLiteral("osd.dim"), false);
        expect(QStringLiteral("popup.layoutPicker.popIn"), false);
        // Empty path / nonsense path.
        expect(QString(), false);
        expect(QStringLiteral("../etc/passwd"), false);

        QVERIFY2(wrong.isEmpty(),
                 qPrintable(QStringLiteral("supportsShaderLeg disagrees with the consumed-leg call sites for: %1")
                                .arg(wrong.join(QStringLiteral(", ")))));
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

        // `>=`, deliberately, unlike the tightened counts in
        // test_animations_shader_overrides.cpp. Three separate inputs feed the
        // stock-suppression gate (shaderProfileTreeChanged, effectsChanged,
        // animationsEnabledChanged), so one tree assignment may legitimately
        // notify more than once and the exact count is NOT part of the contract.
        // What matters is that the chip bindings are notified at all.
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
