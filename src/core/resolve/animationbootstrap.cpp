// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationbootstrap.h"

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
// ProfilePaths::Global — the chain root the global profile registers at.
#include <PhosphorAnimation/ProfilePaths.h>
#include <PhosphorAnimation/ProfileTree.h>

#include <QDir>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <array>

namespace PlasmaZones {

constexpr QLatin1StringView kShellAnimationFamilySeedsOwnerTag{"plasmazones-shell-family-seeds"};

namespace {
// Owner-tag partition the secondary processes (settings, editor) install the
// config-backed timing tree under. Distinct from the daemon's tag so the
// registry remains correctly partitioned even in a hypothetical scenario where
// daemon and settings/editor share a process — today they don't, but the
// narrower contract is correct. No resolver distinguishes the two tags; only
// the seed tag is special.
constexpr QLatin1StringView kSecondaryProfilesOwnerTag{"plasmazones-secondary-profiles"};

QString writableUserDir(QLatin1StringView xdgRelative)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/')
        + QString(xdgRelative);
}

QStringList discoverDataDirs(QLatin1StringView xdgRelative)
{
    // `QStandardPaths::locateAll` returns directories in priority order —
    // the writable user location FIRST, system dirs AFTER. Reverse it so the
    // list reads `sys-lowest, ..., sys-highest, user`, which is the shape the
    // loader documents as its input: it reverse-iterates that and applies
    // FIRST-registration-wins, so the user dir claims its keys before any
    // system dir gets to. Matches LayoutManager::loadLayouts.
    QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QString(xdgRelative),
                                                 QStandardPaths::LocateDirectory);
    std::reverse(dirs.begin(), dirs.end());

    // locateAll returns an empty list when none of the candidate dirs
    // exist yet (fresh install). Unconditionally include the writable
    // location so the loader can watch it via the parent-directory
    // fallback — once the user drops a file there, the watcher fires
    // and the loader picks it up without a daemon restart.
    // cleanPath on both sides of the dedupe: locateAll output is already
    // clean, but a trailing slash in XDG_DATA_HOME would make the hand-built
    // spelling miss the contains() and append the user dir twice (benign
    // under first-registration-wins, but a double scan per rescan). A
    // symlinked user data root can still evade this — accepted; canonical
    // resolution would stat on every discovery call for a case with no
    // in-tree occurrence.
    const QString userDir = QDir::cleanPath(writableUserDir(xdgRelative));
    QStringList cleaned;
    cleaned.reserve(dirs.size());
    for (const QString& dir : std::as_const(dirs)) {
        cleaned.append(QDir::cleanPath(dir));
    }
    if (!cleaned.contains(userDir)) {
        cleaned.append(userDir);
    }
    return cleaned;
}
} // namespace

AnimationLoaderHandles constructAnimationLoaders(PhosphorAnimation::CurveRegistry& curveRegistry, QObject* parent)
{
    using namespace PhosphorAnimation;

    AnimationLoaderHandles handles;
    handles.dirs.curveDirs = discoverDataDirs(QLatin1StringView{"plasmazones/curves"});

    // Materialise the curves dir eagerly so live-reload works on fresh
    // installs. `WatchedDirectorySet`'s parent-watch climb refuses to
    // attach a `QFileSystemWatcher` to forbidden ancestors (`$HOME`,
    // `$XDG_DATA_HOME`, etc.) — without the dir existing, the climb
    // terminates at `~/.local/share` and NO watch is installed, so a curve
    // the user drops in later would not be picked up until a restart.
    // Failures are non-fatal — the initial on-demand scan still works
    // without a watch.
    //
    // The profiles dir is materialised too, but for an unrelated reason and
    // NOT for live reload: nothing watches it since schema v8. It holds the
    // user's saved-preset library, which the animations page creates on
    // demand, and the v8 migration reads pre-v8 override files out of it.
    QDir().mkpath(writableUserDir(QLatin1StringView{"plasmazones/curves"}));
    QDir().mkpath(writableUserDir(QLatin1StringView{"plasmazones/profiles"}));

    // Construct the loader with NO initial load — callers run the scan
    // explicitly (runInitialCurveLoad → seedShellAnimationFamilies →
    // installMotionProfileTree) AFTER they have wired any consumer-side
    // signals so the initial scan's emits are observed.
    //
    // Registry reference is captured at loader construction — this
    // prevents any later async rescan from landing on a different
    // registry than the one the caller initialized against.
    handles.curveLoader = std::make_unique<CurveLoader>(curveRegistry, parent);

    return handles;
}

void runInitialCurveLoad(PhosphorAnimation::CurveLoader& curveLoader, const AnimationLoaderDirs& dirs)
{
    using namespace PhosphorAnimation;

    // Library-level pack first (today a no-op — the library ships no
    // bundled curves — but kept for future curve-pack additions).
    curveLoader.loadLibraryBuiltins();
    curveLoader.loadFromDirectories(dirs.curveDirs, LiveReload::On);
}

void installMotionProfileTree(PhosphorAnimation::PhosphorProfileRegistry& registry,
                              const PhosphorAnimation::CurveRegistry& curves, const QVariantMap& treeJson,
                              const QString& ownerTag)
{
    using namespace PhosphorAnimation;

    const ProfileTree tree = ProfileTree::fromJson(QJsonObject::fromVariantMap(treeJson), curves);

    QHash<QString, Profile> profiles;
    const QStringList paths = tree.overriddenPaths();
    profiles.reserve(paths.size());
    for (const QString& path : paths) {
        profiles.insert(path, tree.directOverride(path));
    }

    // reloadFromOwner, not a loop of registerProfile: it REPLACES this owner's
    // whole partition, so a path the user just cleared disappears from the
    // registry instead of lingering at its last value. Entries owned by other
    // tags (the family seeds, the settings-driven global) are untouched.
    // Called with an empty map on a config that carries no overrides, which is
    // exactly the clear-everything case and must not be short-circuited.
    registry.reloadFromOwner(ownerTag, profiles);
}

void seedShellAnimationFamilies(PhosphorAnimation::PhosphorProfileRegistry& registry,
                                const PhosphorAnimation::CurveRegistry& curves)
{
    using namespace PhosphorAnimation;

    // Family-level defaults — one entry per top-level surface family
    // (plus the asymmetric show/hide leaves where the prior tuning
    // mattered enough to preserve). These re-create the prior bundled-
    // JSON character WITHOUT shadowing leaf-level Settings overrides:
    // PhosphorProfileRegistry::resolveWithInheritance walks up from each
    // leaf, so a per-event override at the leaf wins, an unset leaf
    // inherits from its family parent here, and an unseeded family
    // falls through to library defaults (150 ms OutCubic).
    //
    // Registered under the "shell-family-seeds" owner tag, which the registry
    // stores as a SEPARATE layer. That is what lets a per-event override sit
    // on top of a seed without destroying it, and what makes clearing that
    // override reveal the seed again rather than dropping to library defaults.
    // The global profile joins this same layer while the user has not set it
    // for the same path (loader's "direct-owner-wins" check only
    // protects empty-owner entries, not other tagged ones). Settings
    // publishes via direct-owner registerProfile, which always wins
    // because it overwrites unconditionally and direct-owner is
    // protected from the loader on subsequent rescans.
    //
    // MUST be called AFTER curves are loaded (so `curves.tryCreate`
    // can resolve curve names like "widget-out") and BEFORE the
    // profile loader's initial scan (so a user JSON at a seeded path
    // can overwrite the seed in the same setup pass).
    struct FamilySeed
    {
        QLatin1StringView path;
        QLatin1StringView curveSpec;
        qreal durationMs;
    };
    constexpr std::array<FamilySeed, 29> seeds{{
        // ── Popups ────────────────────────────────────────────────
        // Family parent — leaves (popup.layoutPicker.*,
        // popup.zoneSelector.*, popup.snapAssist.*, popup.cheatsheet.*)
        // inherit from this.
        {QLatin1StringView{"popup"}, QLatin1StringView{"widget-out"}, 150.0},

        // ── Panels ────────────────────────────────────────────────
        // In-app side surfaces (settings nav rail, editor property panel).
        // Slide is size/translate motion; fade is opacity. Asymmetric
        // pairs with separate seeds since family parent carries one curve.
        {QLatin1StringView{"panel.slideIn"}, QLatin1StringView{"widget-out"}, 200.0},
        {QLatin1StringView{"panel.slideOut"}, QLatin1StringView{"cubic-in"}, 180.0},
        // Bespoke bezier curves — formerly the sidebar.* root.
        {QLatin1StringView{"panel.fadeIn"}, QLatin1StringView{"cubic-bezier:0.25,0.46,0.45,0.94"}, 120.0},
        {QLatin1StringView{"panel.fadeOut"}, QLatin1StringView{"cubic-bezier:0.55,0.085,0.68,0.53"}, 80.0},

        // ── OSDs ──────────────────────────────────────────────────
        // Asymmetric show / pop / hide curves — seeded individually
        // because the family parent can carry only one curve.
        {QLatin1StringView{"osd.show"}, QLatin1StringView{"cubic-out"}, 150.0},
        {QLatin1StringView{"osd.pop"}, QLatin1StringView{"widget-pop"}, 250.0},
        {QLatin1StringView{"osd.hide"}, QLatin1StringView{"cubic-in"}, 200.0},

        // ── Widgets ───────────────────────────────────────────────
        // Family parent — covers hover, reorder, progress and the rest
        // of the 150 ms ease-out shape.
        {QLatin1StringView{"widget"}, QLatin1StringView{"widget-out"}, 150.0},
        // Distinctive leaves whose prior tuning materially differed
        // from the family default. Two-layer resolveWithInheritance
        // means a user edit at `widget` still cascades to these
        // leaves — the seeds form the lowest precedence layer.
        {QLatin1StringView{"widget.press"}, QLatin1StringView{"widget-out"}, 100.0},
        {QLatin1StringView{"widget.dim"}, QLatin1StringView{"widget-out"}, 200.0},
        // Tint family — root + fast variant inherits from root.
        {QLatin1StringView{"widget.tint"}, QLatin1StringView{"widget-out"}, 300.0},
        {QLatin1StringView{"widget.tint.fast"}, QLatin1StringView{"widget-out"}, 120.0},
        // Toggle — bistable spring-pop both directions; symmetric defaults.
        {QLatin1StringView{"widget.toggleOn"}, QLatin1StringView{"widget-pop"}, 250.0},
        {QLatin1StringView{"widget.toggleOff"}, QLatin1StringView{"widget-pop"}, 250.0},
        // Badge — show is overshoot, hide is fast ease-in, pulse is count-change attention.
        {QLatin1StringView{"widget.badgeShow"}, QLatin1StringView{"widget-pop"}, 200.0},
        {QLatin1StringView{"widget.badgeHide"}, QLatin1StringView{"cubic-in"}, 150.0},
        {QLatin1StringView{"widget.badgePulse"}, QLatin1StringView{"cubic-bezier:0.45,0.0,0.55,1.0"}, 400.0},
        // Accordion — collapse snaps faster than expand.
        {QLatin1StringView{"widget.accordionExpand"}, QLatin1StringView{"widget-out"}, 250.0},
        {QLatin1StringView{"widget.accordionCollapse"}, QLatin1StringView{"cubic-in"}, 180.0},
        // Asymmetric fade pair.
        {QLatin1StringView{"widget.fadeIn"}, QLatin1StringView{"widget-out"}, 200.0},
        {QLatin1StringView{"widget.fadeOut"}, QLatin1StringView{"cubic-in"}, 400.0},
        // Pulse cluster — sinusoidal ease for the looped pulse feel.
        // Family root + .fast / .slow leaves; user override at `widget.pulse`
        // cascades to both variants via two-layer resolveWithInheritance.
        {QLatin1StringView{"widget.pulse"}, QLatin1StringView{"cubic-bezier:0.45,0.0,0.55,1.0"}, 1000.0},
        {QLatin1StringView{"widget.pulse.fast"}, QLatin1StringView{"cubic-bezier:0.45,0.0,0.55,1.0"}, 500.0},
        {QLatin1StringView{"widget.pulse.slow"}, QLatin1StringView{"cubic-bezier:0.45,0.0,0.55,1.0"}, 1500.0},

        // ── Windows ───────────────────────────────────────────────
        // Family ease-out for open/move/focus/maximize;
        // close is the notable ease-in exception. (No resize legs exist —
        // they were dropped from the taxonomy, see profilepaths.cpp.)
        {QLatin1StringView{"window"}, QLatin1StringView{"widget-out"}, 200.0},
        {QLatin1StringView{"window.appearance.close"}, QLatin1StringView{"cubic-in"}, 150.0},

        // ── Editor ────────────────────────────────────────────────
        // Layout-editor fill-preview / snap-resize animations on the
        // editor's zone-rect outlines. NOT runtime window snapping —
        // that's KWin's domain. ~200 ms ease-out family root.
        {QLatin1StringView{"editor"}, QLatin1StringView{"widget-out"}, 200.0},

        // ── Widget zone-rect ──────────────────────────────────────
        // Reusable Zone widget (ZoneItem.qml et al.) embedded across
        // overlay surfaces, settings dialogs, layout thumbnails. The
        // highlight family root inherits the widget OutCubic feel.
        {QLatin1StringView{"widget.zoneHighlight"}, QLatin1StringView{"widget-out"}, 200.0},

        // No `desktop.*` motion seeds. A seed here would no longer shadow the
        // user's global slider on the effect side — settingsadaptor's
        // `motionProfileTree` getter uses `snapshotExcludingLowPrecedence()`,
        // and both composition roots install this function's owner tag as the
        // registry's low-precedence tag, so seed-owned entries are excluded
        // from the published tree by construction (that filter is exactly why
        // the `window` and `window.appearance.close` seeds above are safe).
        // The surviving reasons are simpler: the desktop transitions are
        // designed to INHERIT the animator's global profile so the global
        // slider retimes them (see the desktopChanged and
        // showingDesktopChanged handlers in lifecycle_wiring.cpp), and seeds
        // exist to preserve prior bundled-JSON character — these transitions
        // are new, so there is no prior tuning to preserve.
    }};

    for (const auto& seed : seeds) {
        Profile profile;
        profile.curve = curves.tryCreate(QString(seed.curveSpec));
        profile.duration = seed.durationMs;
        // Curve `nullptr` is acceptable — the consumer falls through
        // to the library default (outCubic). That's the expected
        // outcome when a curve JSON is missing on a portable build,
        // and emits no warning on the daemon's hot path.
        registry.registerProfile(QString(seed.path), profile, QString(kShellAnimationFamilySeedsOwnerTag));
    }
}

AnimationBootstrap::AnimationBootstrap()
    : m_curveRegistry(std::make_unique<PhosphorAnimation::CurveRegistry>())
{
    auto handles = constructAnimationLoaders(*m_curveRegistry, /*parent=*/nullptr);
    m_curveLoader = std::move(handles.curveLoader);

    // Configure the registry's two-layer resolveWithInheritance so
    // seed entries form the lowest-precedence layer — a user edit at
    // any depth still cascades past leaf seeds. MUST be set before
    // any QML binding evaluates resolveWithInheritance, hence here in
    // the bootstrap ctor.
    m_profileRegistry.setLowPrecedenceOwnerTag(QString(kShellAnimationFamilySeedsOwnerTag));

    // Three-step load: curves first (so seedShellAnimationFamilies can
    // resolve named curves like "widget-out"), then seeds (so the
    // profile loader's reloadFromOwner correctly overwrites a seed
    // when the user authored a JSON at the same path), then profiles.
    runInitialCurveLoad(*m_curveLoader, handles.dirs);
    seedShellAnimationFamilies(m_profileRegistry, *m_curveRegistry);
    // The per-event timing tree is NOT installed here: it is config, and the
    // composition root has no Settings instance yet at bootstrap-construction
    // time. The root calls applyMotionProfileTree once it does, and again on
    // every change.
}

void AnimationBootstrap::applyMotionProfileTree(const QVariantMap& treeJson)
{
    installMotionProfileTree(m_profileRegistry, *m_curveRegistry, treeJson, QString(kSecondaryProfilesOwnerTag));
}

void AnimationBootstrap::applyGlobalProfile(const PhosphorAnimation::Profile& profile, bool explicitlySet)
{
    // Same layer choice the daemon makes in publishActiveAnimationProfile, so
    // a preview resolves through the same precedence the compositor will.
    if (explicitlySet) {
        m_profileRegistry.registerProfile(PhosphorAnimation::ProfilePaths::Global, profile);
    } else {
        m_profileRegistry.registerProfile(PhosphorAnimation::ProfilePaths::Global, profile,
                                          QString(kShellAnimationFamilySeedsOwnerTag));
    }
}

AnimationBootstrap::~AnimationBootstrap() = default;

} // namespace PlasmaZones
