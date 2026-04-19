// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAnimation/qml/PhosphorMotionAnimation.h>

#include <PhosphorAnimation/AnimatedValue.h>
#include <PhosphorAnimation/Curve.h>
#include <PhosphorAnimation/Interpolate.h>
#include <PhosphorAnimation/PhosphorProfileRegistry.h>

#include <QColor>
#include <QEasingCurve>
#include <QLoggingCategory>
#include <QPointF>
#include <QRectF>
#include <QSizeF>

namespace PhosphorAnimation {

namespace {
Q_LOGGING_CATEGORY(lcMotion, "phosphoranimation.qml.motion")
} // namespace

PhosphorMotionAnimation::PhosphorMotionAnimation(QObject* parent)
    : QVariantAnimation(parent)
{
    // Bypass Qt's built-in easing. Qt's QVariantAnimation applies
    // `easingCurve.valueForProgress(timeProgress)` before calling our
    // `interpolated()`; with Linear set, the progress we receive is the
    // raw time ratio — we then apply our Phase-3 Curve inside
    // `interpolated()`. Without this override, every PhosphorCurve
    // would be *double-eased* (Qt's OutInQuad composed with ours).
    setEasingCurve(QEasingCurve(QEasingCurve::Linear));
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

const Profile& PhosphorMotionAnimation::resolvedProfile() const
{
    return m_resolvedProfile;
}

int PhosphorMotionAnimation::duration() const
{
    // While running, return the duration snapshotted at start(). Without
    // the snapshot, a live settings edit mid-animation would change the
    // value Qt reads per-tick for progress calculation and snap the
    // animation backwards or forwards visibly.
    if (m_activeDurationMs >= 0) {
        return m_activeDurationMs;
    }
    // qreal → int ms. `QVariantAnimation` expects integer milliseconds;
    // round rather than truncate so a 149.9 ms profile doesn't silently
    // lose a frame on a 60 Hz paint cycle.
    return qRound(m_resolvedProfile.effectiveDuration());
}

void PhosphorMotionAnimation::updateState(QAbstractAnimation::State newState, QAbstractAnimation::State oldState)
{
    // Capture the resolved duration on the transition INTO the Running
    // state and hold it for the whole run. Reset on exit so the next
    // start() reads whatever profile is current at that moment.
    if (newState == QAbstractAnimation::Running && oldState != QAbstractAnimation::Running) {
        m_activeDurationMs = qRound(m_resolvedProfile.effectiveDuration());
    } else if (newState == QAbstractAnimation::Stopped) {
        m_activeDurationMs = -1;
    }
    QVariantAnimation::updateState(newState, oldState);
}

QVariant PhosphorMotionAnimation::interpolated(const QVariant& from, const QVariant& to, qreal progress) const
{
    // Curve is cached in m_resolvedProfile; fall back to the library
    // default when no curve is set (matches AnimatedValue<T>'s
    // effectiveCurve path).
    const auto curve = m_resolvedProfile.curve ? m_resolvedProfile.curve : defaultFallbackCurve();
    const qreal curvedProgress = curve->evaluate(progress);

    // Dispatch on QVariant's static type-id — Qt's Behavior machinery
    // fills startValue / endValue from the target property's type, so
    // `from` and `to` have a consistent type at call time. Every
    // supported T routes through the matching Interpolate<T>::lerp.
    switch (from.typeId()) {
    case QMetaType::Double:
    case QMetaType::Float:
        return Interpolate<qreal>::lerp(from.toDouble(), to.toDouble(), curvedProgress);
    case QMetaType::Int:
        // Integer properties (e.g., Item.x as int in old code) —
        // interpolate as qreal and round on the way out.
        return QVariant::fromValue(qRound(Interpolate<qreal>::lerp(static_cast<qreal>(from.toInt()),
                                                                   static_cast<qreal>(to.toInt()), curvedProgress)));
    case QMetaType::QPointF:
        return Interpolate<QPointF>::lerp(from.toPointF(), to.toPointF(), curvedProgress);
    case QMetaType::QSizeF:
        return Interpolate<QSizeF>::lerp(from.toSizeF(), to.toSizeF(), curvedProgress);
    case QMetaType::QRectF:
        return Interpolate<QRectF>::lerp(from.toRectF(), to.toRectF(), curvedProgress);
    case QMetaType::QColor:
        return Interpolate<QColor>::lerp(from.value<QColor>(), to.value<QColor>(), curvedProgress);
    default:
        // Unsupported type: fall back to Qt's default interpolation
        // shape (from if progress == 0 else to — no-op in practice
        // since the base implementation also can't interpolate this).
        // Log once at debug so a consumer animating, say, a QTransform
        // property sees why nothing happens.
        qCDebug(lcMotion) << "interpolated: unsupported QVariant type" << QMetaType(from.typeId()).name()
                          << "— returning endpoint";
        return progress < 0.5 ? from : to;
    }
}

void PhosphorMotionAnimation::resolveFromVariant(const QVariant& p)
{
    // Clear any prior live-bind connection — we'll re-establish below
    // if the new profile is a path string.
    disconnectRegistrySignal();
    m_boundPath.clear();

    // Accept PhosphorProfile value: install the underlying Profile
    // verbatim as the compile-time snapshot branch per decision R.
    if (p.canConvert<PhosphorProfile>()) {
        // `canConvert` is permissive — explicitly check the typeId to
        // distinguish a true PhosphorProfile value from, e.g., an
        // integer that Qt would happily convert into nonsense. The
        // typeId of PhosphorProfile is set by its Q_DECLARE_METATYPE
        // registration in the QML plugin.
        if (p.typeId() == qMetaTypeId<PhosphorProfile>()) {
            applyResolvedProfile(p.value<PhosphorProfile>().value());
            return;
        }
    }

    // Path-string branch: look up the registry, subscribe to its
    // profileChanged(path) / profilesReloaded() signals for live
    // updates.
    if (p.typeId() == QMetaType::QString) {
        rebindToRegistryPath(p.toString());
        return;
    }

    // Unrecognised shape — leave the resolved profile at defaults.
    // A setter chain that passes garbage (typo on `profile` spelled
    // `profiles`, an accidental int) falls through cleanly rather
    // than crashing.
    qCDebug(lcMotion) << "setProfile: unrecognised QVariant shape" << p << "— falling back to default profile";
    applyResolvedProfile(Profile{});
}

void PhosphorMotionAnimation::rebindToRegistryPath(const QString& path)
{
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

    // Live-rebind: per-path change signal AND bulk-reload signal.
    // Per-path emits for targeted updates (settings UI edits the
    // overlay.fade profile); reload emits when the loader rescans
    // the XDG dir after a QFileSystemWatcher fire.
    m_registryChangedConnection =
        connect(&registry, &PhosphorProfileRegistry::profileChanged, this, [this](const QString& changedPath) {
            if (changedPath != m_boundPath) {
                return;
            }
            auto resolved = PhosphorProfileRegistry::instance().resolve(m_boundPath);
            applyResolvedProfile(resolved.value_or(Profile{}));
        });
    m_registryReloadedConnection = connect(&registry, &PhosphorProfileRegistry::profilesReloaded, this, [this]() {
        auto resolved = PhosphorProfileRegistry::instance().resolve(m_boundPath);
        applyResolvedProfile(resolved.value_or(Profile{}));
    });
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
    // No explicit NOTIFY here — duration is re-read by
    // QAbstractAnimation when start() fires; interpolated() reads
    // m_resolvedProfile.curve on every tick. A mid-flight profile
    // swap picks up the new curve on the NEXT tick without explicit
    // re-trigger; the duration change applies on the next start()
    // to avoid a visible time-jump.
}

} // namespace PhosphorAnimation
