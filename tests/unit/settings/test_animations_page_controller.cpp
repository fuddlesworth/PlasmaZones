// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file test_animations_page_controller.cpp
 * @brief Path-discovery + override-CRUD tests for AnimationsPageController.
 *
 * Pins the file-per-path persistence model: setOverride writes one JSON
 * file under `<userProfilesDir>/<path>.json`, clearOverride deletes it,
 * resolvedProfile walks the parent chain and fills library defaults.
 *
 * Uses `setUserProfilesDirOverride()` to redirect file I/O into a
 * tmpdir so the test never touches the real user XDG dir.
 *
 * Companion test files (split for the <800-line guideline):
 *   - test_animations_motion_sets.cpp      — preset / motion-set / pending
 *   - test_animations_shader_overrides.cpp — shader-effect overrides
 */

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include "settings/animationspagecontroller.h"

using namespace PlasmaZones;

namespace {

QString readFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(f.readAll());
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

        // The slider band stays inside the engine's accepted clamp range.
        QVERIFY(c.springOmegaMin() >= 0.1);
        QVERIFY(c.springOmegaMax() <= 200.0);
        QVERIFY(c.springZetaMin() >= 0.0);
        QVERIFY(c.springZetaMax() <= 10.0);
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

    // ─── Effective resolution ─────────────────────────────────────────────

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

    /// Pin the shader-leg-support predicate against the daemon-side list.
    /// `supportsShaderLeg` is the predicate the QML shader-picker visibility
    /// is bound to; if the surface set in
    /// `src/core/animationshadersupportedpaths.h` drifts away from the
    /// `resolveShaderEffect` call sites in
    /// `src/daemon/overlayservice.cpp`, the QML would either expose
    /// pickers that do nothing (drift in one direction) or hide pickers
    /// for events that DO produce shader legs (drift in the other).
    void supportsShaderLeg_matchesDaemonOverlayConsumers()
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

        // Window family — consumed leaves driven by the KWin effect's
        // tryBeginShaderForEvent at kwin-effect/plasmazoneseffect.cpp.
        // Each maps to a window-lifecycle hook (windowAdded, windowClosed,
        // windowFinishUserMovedResized, maximized, minimized,
        // focusChanged) and runs the resolved shader on the
        // OffscreenEffect's redirected texture quad.
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

        // Ancestors of consumed leaves — supported because the
        // daemon's resolver walks them on the way to the leaf, so a
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
};

QTEST_MAIN(TestAnimationsPageController)
#include "test_animations_page_controller.moc"
