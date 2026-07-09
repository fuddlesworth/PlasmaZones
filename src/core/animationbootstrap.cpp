// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "animationbootstrap.h"

#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/CurveLoader.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfileLoader.h>

#include <QDir>
#include <QObject>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <array>

namespace PlasmaZones {

constexpr QLatin1StringView kShellAnimationFamilySeedsOwnerTag{"plasmazones-shell-family-seeds"};

namespace {
// Owner-tag partition used by secondary processes' ProfileLoader. Distinct
// from the daemon's tag so the registry remains correctly partitioned even
// in a hypothetical scenario where daemon and settings/editor share a
// process — today they don't, but the narrower contract is correct.
constexpr QLatin1StringView kSecondaryProfilesOwnerTag{"plasmazones-secondary-profiles"};

QStringList discoverDataDirs(QLatin1StringView xdgRelative)
{
    // `QStandardPaths::locateAll` returns directories in priority order —
    // the writable user location FIRST, system dirs AFTER. The loader
    // iterates in caller-supplied order and lets later entries override
    // earlier on key collision, so we reverse to achieve the standard
    // "system first, user wins last" layering (decision X). Matches
    // LayoutManager::loadLayouts.
    QStringList dirs = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QString(xdgRelative),
                                                 QStandardPaths::LocateDirectory);
    std::reverse(dirs.begin(), dirs.end());

    // locateAll returns an empty list when none of the candidate dirs
    // exist yet (fresh install). Unconditionally include the writable
    // location so the loader can watch it via the parent-directory
    // fallback — once the user drops a file there, the watcher fires
    // and the loader picks it up without a daemon restart.
    const QString userDir =
        QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/') + QString(xdgRelative);
    if (!dirs.contains(userDir)) {
        dirs.append(userDir);
    }
    return dirs;
}

QString writableUserDir(QLatin1StringView xdgRelative)
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QLatin1Char('/')
        + QString(xdgRelative);
}
} // namespace

AnimationLoaderHandles constructAnimationLoaders(PhosphorAnimation::CurveRegistry& curveRegistry,
                                                 PhosphorAnimation::PhosphorProfileRegistry& profileRegistry,
                                                 QLatin1StringView ownerTag, QObject* parent)
{
    using namespace PhosphorAnimation;

    AnimationLoaderHandles handles;
    handles.dirs.curveDirs = discoverDataDirs(QLatin1StringView{"plasmazones/curves"});
    handles.dirs.profileDirs = discoverDataDirs(QLatin1StringView{"plasmazones/profiles"});

    // Materialise the user dirs eagerly so live-reload works on fresh
    // installs. `WatchedDirectorySet`'s parent-watch climb refuses to
    // attach a `QFileSystemWatcher` to forbidden ancestors (`$HOME`,
    // `$XDG_DATA_HOME`, etc.) — without these dirs existing, the climb
    // terminates at `~/.local/share` and NO watch is installed. The
    // user could then drop `~/.local/share/plasmazones/profiles/foo.json`
    // and live-reload would silently never fire until daemon restart.
    // Failures are non-fatal — the initial on-demand scan still works
    // without a watch.
    QDir().mkpath(writableUserDir(QLatin1StringView{"plasmazones/curves"}));
    QDir().mkpath(writableUserDir(QLatin1StringView{"plasmazones/profiles"}));

    // Construct loaders with NO initial load — callers run the scan
    // explicitly (via runInitialAnimationLoad) AFTER they have wired
    // any consumer-side signals so the initial scan's emits are
    // observed. The daemon needs this; secondary processes call
    // runInitialAnimationLoad immediately and have no pre-load wiring
    // to set up.
    //
    // Registry reference is captured at loader construction — this
    // prevents any later async rescan from landing on a different
    // registry than the one the caller initialized against.
    handles.curveLoader = std::make_unique<CurveLoader>(curveRegistry, parent);
    handles.profileLoader = std::make_unique<ProfileLoader>(profileRegistry, curveRegistry, ownerTag, parent);

    // Wire CurveLoader::curvesChanged → ProfileLoader rescan. A Profile
    // whose `curve` spec references a user-authored curve name that
    // wasn't yet in CurveRegistry at parse time is stored with
    // `curve = nullptr` (falls back to library default at animation
    // time). When the user drops or edits a curve JSON AFTER the
    // profile files were scanned, we need to re-parse every profile
    // so the newly-available curve gets resolved. Without this wire,
    // drop-order-matters: curves-before-profiles works, profiles-
    // before-curves silently loses the curve reference until the
    // profile file itself is touched.
    //
    // ProfileLoader::requestRescan goes through DirectoryLoader's
    // debounced rescan path, so a curve-pack edit that changes many
    // files coalesces into one profile rescan.
    QObject::connect(handles.curveLoader.get(), &CurveLoader::curvesChanged, handles.profileLoader.get(),
                     &ProfileLoader::requestRescan);

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

void runInitialProfileLoad(PhosphorAnimation::ProfileLoader& profileLoader, const AnimationLoaderDirs& dirs)
{
    using namespace PhosphorAnimation;

    profileLoader.loadLibraryBuiltins();
    profileLoader.loadFromDirectories(dirs.profileDirs, LiveReload::On);
}

void runInitialAnimationLoad(PhosphorAnimation::CurveLoader& curveLoader,
                             PhosphorAnimation::ProfileLoader& profileLoader, const AnimationLoaderDirs& dirs)
{
    // Curves first so any profile JSON referencing a user-authored curve
    // resolves on first parse rather than waiting for the curveLoader→
    // profileLoader rescan wire to fire on the second pass.
    runInitialCurveLoad(curveLoader, dirs);
    runInitialProfileLoad(profileLoader, dirs);
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
    // leaf, so a Settings edit at the leaf wins, an unset leaf
    // inherits from its family parent here, and an unseeded family
    // falls through to library defaults (150 ms OutCubic).
    //
    // Registered under the "shell-family-seeds" owner tag so the
    // ProfileLoader's reloadFromOwner correctly overwrites a seed when
    // the user authors `~/.local/share/plasmazones/profiles/<path>.json`
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
        // popup.zoneSelector.*, popup.snapAssist.*) inherit from this.
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
        // Family ease-out for open/move/resize/focus/maximize;
        // close is the notable ease-in exception.
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

        // No `workspace.*` seeds: virtual-desktop transitions are KWin's
        // compositor-level domain (Slide / Fade Desktop / etc.). PZ does
        // not run a parallel animation in that lane to avoid double
        // transforms and bypassing the user's KWin effect choice.
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
    auto handles =
        constructAnimationLoaders(*m_curveRegistry, m_profileRegistry, kSecondaryProfilesOwnerTag, /*parent=*/nullptr);
    m_curveLoader = std::move(handles.curveLoader);
    m_profileLoader = std::move(handles.profileLoader);

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
    runInitialProfileLoad(*m_profileLoader, handles.dirs);
}

AnimationBootstrap::~AnimationBootstrap() = default;

} // namespace PlasmaZones
