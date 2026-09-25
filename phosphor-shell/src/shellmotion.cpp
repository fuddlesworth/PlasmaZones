// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later
#include "ShellMotion.h"

#include <PhosphorAnimation/PhosphorCurve.h>
#include <PhosphorAnimation/Profile.h>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QJsonObject>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(lcShellMotion, "phosphorshell.motion")

namespace PhosphorShellApp {

namespace {

constexpr QLatin1String kSeedOwner("phosphor-shell-seeds");
constexpr QLatin1String kPortalService("org.freedesktop.portal.Desktop");
constexpr QLatin1String kPortalPath("/org/freedesktop/portal/desktop");
constexpr QLatin1String kPortalSettings("org.freedesktop.portal.Settings");
constexpr QLatin1String kKdeNamespace("org.kde.kdeglobals.KDE");
constexpr QLatin1String kKdeKey("AnimationDurationFactor");
constexpr QLatin1String kGnomeNamespace("org.gnome.desktop.interface");
constexpr QLatin1String kGnomeKey("enable-animations");

// The portal wraps values in one or two layers of variant.
QVariant unwrap(QVariant value)
{
    while (value.canConvert<QDBusVariant>()) {
        value = value.value<QDBusVariant>().variant();
    }
    return value;
}

} // namespace

ShellMotion::ShellMotion(QObject* parent)
    : QObject(parent)
{
    registerProfiles();

    if (qEnvironmentVariableIsSet("PHOSPHOR_REDUCED_MOTION")) {
        m_reducedMotion = qEnvironmentVariableIntValue("PHOSPHOR_REDUCED_MOTION") != 0;
        qCInfo(lcShellMotion) << "reduced motion from PHOSPHOR_REDUCED_MOTION:" << m_reducedMotion;
        return;
    }

    QDBusConnection::sessionBus().connect(kPortalService, kPortalPath, kPortalSettings,
                                          QStringLiteral("SettingChanged"), this,
                                          SLOT(onSettingChanged(QString, QString, QDBusVariant)));
    readPortal();
}

ShellMotion::~ShellMotion()
{
    unpublish();
}

bool ShellMotion::reducedMotion() const
{
    return m_reducedMotion;
}

QString ShellMotion::settlePath()
{
    return QStringLiteral("shell.settle");
}

PhosphorAnimation::PhosphorProfileRegistry* ShellMotion::profileRegistry()
{
    return &m_profiles;
}

PhosphorAnimation::CurveRegistry* ShellMotion::curveRegistry()
{
    return &m_curves;
}

void ShellMotion::publish()
{
    if (m_published) {
        return;
    }
    PhosphorAnimation::PhosphorCurve::setDefaultRegistry(&m_curves);
    PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(&m_profiles);
    PhosphorAnimation::QtQuickClockManager::setDefaultManager(&m_clocks);
    m_published = true;
}

void ShellMotion::unpublish()
{
    if (!m_published) {
        return;
    }
    PhosphorAnimation::PhosphorCurve::setDefaultRegistry(nullptr);
    PhosphorAnimation::PhosphorProfileRegistry::setDefaultRegistry(nullptr);
    PhosphorAnimation::QtQuickClockManager::setDefaultManager(nullptr);
    m_published = false;
}

void ShellMotion::registerProfiles()
{
    m_profiles.setLowPrecedenceOwnerTag(QString(kSeedOwner));

    // M4 settle (A1 §3.1): spring omega 22, zeta 0.85, one 3 % overshoot,
    // at rest in about 250 ms. The duration bounds the analytic step
    // response PhosphorMotionAnimation plays; the spring's own settle
    // time is what shapes it.
    QJsonObject settle;
    settle.insert(QLatin1String(PhosphorAnimation::Profile::JsonFieldCurve), QStringLiteral("spring:22,0.85"));
    settle.insert(QLatin1String(PhosphorAnimation::Profile::JsonFieldDuration), 250);
    const auto profile = PhosphorAnimation::Profile::fromJson(settle, m_curves);
    if (!profile.curve) {
        qCWarning(lcShellMotion) << "settle spring did not resolve; positional settles use the library default";
        return;
    }
    m_profiles.registerProfile(settlePath(), profile, QString(kSeedOwner));
}

std::optional<bool> ShellMotion::reducedMotionFor(const QString& ns, const QString& key, const QVariant& value)
{
    const QVariant v = unwrap(value);
    if (ns == kKdeNamespace && key == kKdeKey) {
        bool ok = false;
        const double factor = v.toDouble(&ok);
        return ok ? std::optional<bool>(factor <= 0.0) : std::nullopt;
    }
    if (ns == kGnomeNamespace && key == kGnomeKey) {
        return v.canConvert<bool>() ? std::optional<bool>(!v.toBool()) : std::nullopt;
    }
    return std::nullopt;
}

void ShellMotion::readPortal()
{
    // One Read per key. A desktop answers only its own namespace and
    // returns an error for the other, which is silently the "no opinion"
    // case here.
    const auto ask = [this](const QString& ns, const QString& key) {
        QDBusMessage call =
            QDBusMessage::createMethodCall(kPortalService, kPortalPath, kPortalSettings, QStringLiteral("Read"));
        call << ns << key;
        auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, ns, key](QDBusPendingCallWatcher* w) {
            w->deleteLater();
            const QDBusPendingReply<QVariant> reply = *w;
            if (!reply.isValid()) {
                return;
            }
            if (const auto reduced = reducedMotionFor(ns, key, reply.value())) {
                setReducedMotion(*reduced);
            }
        });
    };
    ask(QString(kKdeNamespace), QString(kKdeKey));
    ask(QString(kGnomeNamespace), QString(kGnomeKey));
}

void ShellMotion::onSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value)
{
    if (const auto reduced = reducedMotionFor(ns, key, value.variant())) {
        setReducedMotion(*reduced);
    }
}

void ShellMotion::setReducedMotion(bool reduced)
{
    if (m_reducedMotion == reduced) {
        return;
    }
    m_reducedMotion = reduced;
    qCInfo(lcShellMotion) << "reduced motion:" << reduced;
    Q_EMIT reducedMotionChanged();
}

} // namespace PhosphorShellApp
