// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorMotionAnimation.h>

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/Easing.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <QEasingCurve>
#include <QLoggingCategory>
#include <QPointF>

#include <QtMath>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcMotion, "phosphoranimation.qml.motion")
} // namespace

namespace {
// Run the defaultFallbackCurve() invariant check exactly once per
// process, not once per PhosphorMotionAnimation instantiation. Every
// `Behavior on X { PhosphorMotionAnimation { … } }` site in the shell's
// 28+ migrated QML files would otherwise re-pay the dynamic_cast on
// every scene instantiation. A Meyers-local static gives us the "fail
// hard in release builds on a broken library" property with no per-
// instance cost. The lambda body matches the original invariant: the
// fallback MUST be a cubic-bezier Easing so the ctor fast-path in
// applyResolvedEasing (single addCubicBezierSegment) stays below Qt
// 6.11's ≥11-segment BezierSpline heap-corruption boundary.
bool verifyDefaultFallbackCurveIsCubicBezier()
{
    const auto fallback = defaultFallbackCurve();
    const auto* easing = dynamic_cast<const Easing*>(fallback.get());
    if (!easing || easing->type != Easing::Type::CubicBezier) {
        qFatal(
            "PhosphorMotionAnimation: defaultFallbackCurve() must be a cubic-bezier Easing "
            "for the ctor fast path — see invariant comment in PhosphorMotionAnimation.cpp");
    }
    return true;
}
} // namespace

PhosphorMotionAnimation::PhosphorMotionAnimation(QObject* parent)
    : QQuickPropertyAnimation(parent)
{
    // One-time invariant check (see verifyDefaultFallbackCurveIsCubicBezier
    // above). The static-init guard runs the check on first instantiation
    // only; `[[maybe_unused]]` silences the unused-variable warning since
    // the returned bool is only held to anchor the static.
    [[maybe_unused]] static const bool sVerified = verifyDefaultFallbackCurveIsCubicBezier();

    // Apply the default profile's easing and duration so a
    // PhosphorMotionAnimation with no profile set still animates
    // with the library-default OutCubic curve rather than Qt Quick's
    // default InOutQuad.
    applyResolvedEasing();
}

PhosphorMotionAnimation::~PhosphorMotionAnimation()
{
    disconnectRegistrySignal();
}

QVariant PhosphorMotionAnimation::profile() const
{
    return m_profile;
}

void PhosphorMotionAnimation::setProfile(const QVariant& p)
{
    if (m_profile == p) {
        return;
    }
    m_profile = p;
    resolveFromVariant(p);
    Q_EMIT profileChanged();
}

int PhosphorMotionAnimation::durationOverride() const
{
    return m_durationOverride;
}

void PhosphorMotionAnimation::setDurationOverride(int ms)
{
    if (m_durationOverride == ms) {
        return;
    }
    m_durationOverride = ms;
    // Install the new effective duration directly. applyResolvedEasing()
    // would also rebuild the BezierSpline approximation for parametric
    // curves (Spring / Elastic / Bounce / user-authored), which is
    // independent of the duration — a 30 Hz durationOverride slider drag
    // bound to a spring profile would otherwise re-sample the curve
    // kBezierSplineSegments × 2 control-point calls × 30 Hz per tick on
    // every bound animation. Duration only feeds QQuickPropertyAnimation's
    // timing machinery, not the easing curve shape, so the direct
    // setDuration is both correct and cheaper.
    const int durationMs = m_durationOverride > 0 ? m_durationOverride : qRound(m_resolvedProfile.effectiveDuration());
    QQuickPropertyAnimation::setDuration(durationMs);
    Q_EMIT durationOverrideChanged();
}

const Profile& PhosphorMotionAnimation::resolvedProfile() const
{
    return m_resolvedProfile;
}

void PhosphorMotionAnimation::applyResolvedEasing()
{
    // Duration: override wins when > 0, otherwise the profile's
    // effective duration. The override exists so QML authors can bind
    // `durationOverride: Kirigami.Units.longDuration` onto a shared
    // profile JSON — the profile provides the curve shape while the
    // caller's theme-scaled value drives the timing (Plasma's system
    // animation-speed preference still applies). A zero / negative
    // override means "use the profile's duration" — this is the
    // default and the common case.
    const int durationMs = m_durationOverride > 0 ? m_durationOverride : qRound(m_resolvedProfile.effectiveDuration());
    QQuickPropertyAnimation::setDuration(durationMs);

    // If no curve is set, fall back to the library default (OutCubic).
    const auto curve = m_resolvedProfile.curve ? m_resolvedProfile.curve : defaultFallbackCurve();

    QEasingCurve ec(QEasingCurve::BezierSpline);

    // Fast path: a plain cubic-bezier Easing round-trips exactly as a
    // single canonical BezierSpline segment with the two original
    // control points — no sampling, no approximation. Handles OutCubic,
    // OutBack, InOutCubic, and every other CSS-style cubic we use.
    if (const auto* easing = dynamic_cast<const Easing*>(curve.get());
        easing && easing->type == Easing::Type::CubicBezier) {
        ec.addCubicBezierSegment(QPointF(easing->x1, easing->y1), QPointF(easing->x2, easing->y2), QPointF(1.0, 1.0));
        QQuickPropertyAnimation::setEasing(ec);
        return;
    }

    // Parametric / stateful curves (Spring, Elastic, Bounce, user-authored):
    // sample into piecewise cubic Bezier segments.
    //
    // Segment count is capped by `kBezierSplineSegments` because Qt
    // 6.11's QQuickPropertyAnimation::setEasing heap-corrupts on
    // BezierSpline curves with >= 11 segments — observed as "malloc():
    // corrupted top size" / "unaligned tcache chunk detected" during
    // downstream allocations. The constant lives on the public class
    // surface so the regression test can assert the installed easing
    // stays under the Qt boundary.
    constexpr int kSegments = kBezierSplineSegments;
    static_assert(kSegments < 11, "Qt 6.11 setEasing heap-corrupts at >=11 BezierSpline segments");
    for (int i = 0; i < kSegments; ++i) {
        const qreal t0 = static_cast<qreal>(i) / kSegments;
        const qreal t1 = static_cast<qreal>(i + 1) / kSegments;
        const qreal tMid1 = t0 + (t1 - t0) / 3.0;
        const qreal tMid2 = t0 + 2.0 * (t1 - t0) / 3.0;

        const QPointF c1(tMid1, curve->evaluate(tMid1));
        const QPointF c2(tMid2, curve->evaluate(tMid2));
        // Force the terminal endpoint to exactly (1,1) so the spline
        // terminates canonically even when the source curve overshoots
        // (springs/elastics often return != 1 at t=1).
        const QPointF end(t1, (i == kSegments - 1) ? qreal(1.0) : curve->evaluate(t1));
        ec.addCubicBezierSegment(c1, c2, end);
    }

    QQuickPropertyAnimation::setEasing(ec);
}

void PhosphorMotionAnimation::resolveFromVariant(const QVariant& p)
{
    // Capture the previous bound path before clearing — used below to
    // distinguish "unbound property just evaluated to empty" (silent —
    // common during startup) from "author explicitly cleared a
    // previously-bound path" (warn — almost always a typo).
    const QString priorPath = m_boundPath;

    // Clear any prior live-bind connection — we'll re-establish below
    // if the new profile is a path string.
    disconnectRegistrySignal();
    m_boundPath.clear();

    // Accept PhosphorProfile value: install the underlying Profile
    // verbatim as the compile-time snapshot branch per decision R.
    //
    // Check typeId directly — QVariant::canConvert<PhosphorProfile>
    // is permissive enough that an int or other primitive can satisfy
    // it via Qt's converter machinery, then fall through into this
    // branch and explode in value<PhosphorProfile>(). The typeId
    // equality is the authoritative "this QVariant truly holds a
    // PhosphorProfile" check.
    if (p.typeId() == qMetaTypeId<PhosphorProfile>()) {
        applyResolvedProfile(p.value<PhosphorProfile>().value());
        return;
    }

    // Path-string branch: look up the registry, subscribe to its
    // profileChanged(path) / profilesReloaded() signals for live
    // updates.
    if (p.typeId() == QMetaType::QString) {
        const QString path = p.toString();
        if (path.isEmpty() && !priorPath.isEmpty()) {
            // A previously-bound path being explicitly replaced with
            // "" — almost always an author mistake (conditional
            // expression evaluating to the wrong branch, stale
            // property reference, typo). Without this warning the
            // animation silently reverts to library defaults with no
            // feedback that anything went wrong.
            qCWarning(lcMotion).nospace()
                << "setProfile: empty path replaces previous binding '" << priorPath << "' — using library defaults";
        }
        rebindToRegistryPath(path);
        return;
    }

    // Unrecognised shape — leave the resolved profile at defaults.
    // A setter chain that passes garbage (typo on `profile` spelled
    // `profiles`, an accidental int) falls through cleanly rather
    // than crashing. Warn rather than debug: this is almost always a
    // QML authoring bug and the silent fallback to library defaults
    // looks like the animation is just "off" — turning this into a
    // warning surfaces the typo the first time a QML file with the
    // mistake is loaded.
    qCWarning(lcMotion) << "setProfile: unrecognised QVariant shape" << p << "— falling back to default profile";
    applyResolvedProfile(Profile{});
}

void PhosphorMotionAnimation::rebindToRegistryPath(const QString& path)
{
    // Empty path: a common result of an unresolved QML binding
    // (`profile: foo` where `foo` hasn't evaluated yet). Subscribing
    // on the empty-string registry key would otherwise install a
    // live-rebind hook that fires for every registry event with an
    // empty `changedPath` — noise at best, silent cross-wiring at
    // worst. Apply defaults and bail.
    if (path.isEmpty()) {
        applyResolvedProfile(Profile{});
        return;
    }

    m_boundPath = path;

    // Resolve now (may return nullopt if the path isn't registered yet
    // — startup race where the loader hasn't scanned XDG dirs).
    auto& registry = PhosphorProfileRegistry::instance();
    if (auto resolved = registry.resolve(path)) {
        applyResolvedProfile(*resolved);
    } else {
        // Keep the animation running at library-default profile until
        // a later registerProfile fires profileChanged(path). The
        // registry-connected lambda below will pick it up.
        applyResolvedProfile(Profile{});
    }

    // Live-rebind: per-path change signal handles every targeted
    // update (settings UI edits, ProfileLoader rescans — both go
    // through registerProfile / reloadFromOwner which emit per-path
    // profileChanged). `profilesReloaded` fires only on wholesale
    // ops (reloadAll / clear) — those don't enumerate paths so the
    // bulk subscription is the only way to catch a wipe that just
    // evicted our bound path.
    //
    // Queued connection guards against registry signals emitted from
    // worker threads per PhosphorProfileRegistry thread-safety
    // contract (registerProfile / reloadFromOwner / clear are documented
    // as callable from any thread). `applyResolvedProfile` lands in
    // QQuickPropertyAnimation::setEasing — the exact API that heap-
    // corrupted in commit 1f09962d when called off the GUI thread.
    // Forcing delivery through the receiver's event loop ensures the
    // setEasing call always runs on the thread that owns this object,
    // regardless of which thread publishes the registry update.
    m_registryChangedConnection = connect(
        &registry, &PhosphorProfileRegistry::profileChanged, this,
        [this](const QString& changedPath) {
            if (changedPath != m_boundPath) {
                return;
            }
            auto resolved = PhosphorProfileRegistry::instance().resolve(m_boundPath);
            applyResolvedProfile(resolved.value_or(Profile{}));
        },
        Qt::QueuedConnection);
    m_registryReloadedConnection = connect(
        &registry, &PhosphorProfileRegistry::profilesReloaded, this,
        [this]() {
            auto resolved = PhosphorProfileRegistry::instance().resolve(m_boundPath);
            applyResolvedProfile(resolved.value_or(Profile{}));
        },
        Qt::QueuedConnection);
}

void PhosphorMotionAnimation::disconnectRegistrySignal()
{
    if (m_registryChangedConnection) {
        QObject::disconnect(m_registryChangedConnection);
        m_registryChangedConnection = {};
    }
    if (m_registryReloadedConnection) {
        QObject::disconnect(m_registryReloadedConnection);
        m_registryReloadedConnection = {};
    }
}

void PhosphorMotionAnimation::applyResolvedProfile(const Profile& p)
{
    m_resolvedProfile = p;
    applyResolvedEasing();
}

} // namespace PhosphorAnimation
