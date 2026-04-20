// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "configdefaults.h"

#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorAnimation/Profile.h>
#include <PhosphorAnimation/ProfilePaths.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>

namespace PlasmaZones {

static QString configDir()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    if (dir.isEmpty()) {
        dir = QDir::homePath() + QStringLiteral("/.config");
    }
    return dir;
}

QString ConfigDefaults::configFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/config.json");
}

QString ConfigDefaults::sessionFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/session.json");
}

QString ConfigDefaults::assignmentsFilePath()
{
    return configDir() + QStringLiteral("/plasmazones/assignments.json");
}

QString ConfigDefaults::legacyConfigFilePath()
{
    return configDir() + QStringLiteral("/plasmazonesrc");
}

QString ConfigDefaults::readRenderingBackendFromDisk()
{
    // Lightweight read — only needs one key, so avoid constructing a full backend
    // (which parses the entire file and tracks group state).  This runs before
    // QCoreApplication exists on the daemon startup path.
    const QString jsonPath = configFilePath();
    if (QFile::exists(jsonPath)) {
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError err;
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
            if (err.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject rendering = doc.object().value(renderingGroup()).toObject();
                if (rendering.contains(backendKey())) {
                    return normalizeRenderingBackend(rendering.value(backendKey()).toString(renderingBackend()));
                }
            }
        }
    }

    // Fallback: read from legacy INI file if JSON doesn't exist yet (migration
    // hasn't run — this function is called very early, before ensureJsonConfig()).
    const QString iniPath = legacyConfigFilePath();
    if (QFile::exists(iniPath)) {
        QSettings cfg(iniPath, QSettings::IniFormat);
        // Hardcoded v1 key name — the INI file predates the v2 rename to "Backend"
        const QString raw = cfg.value(QStringLiteral("RenderingBackend"), renderingBackend()).toString();
        return normalizeRenderingBackend(raw);
    }

    return renderingBackend();
}

QString ConfigDefaults::animationProfile()
{
    // Assemble the default animation Profile from the existing per-field
    // defaults and serialise via Profile::toJson. This preserves the
    // pre-migration feel — same duration / easing / minDistance etc.
    // — while persisting in the new Phase-4 single-blob format.
    //
    // Curve is looked up through CurveRegistry so the wire format
    // ("0.33,1.00,0.68,1.00" — cubic-bezier) resolves to the exact
    // same shared_ptr<const Curve> the AnimatedValue<T> runtime uses,
    // no divergent parse paths.
    PhosphorAnimation::Profile p;
    p.curve = PhosphorAnimation::CurveRegistry::instance().create(animationEasingCurve());
    p.duration = static_cast<qreal>(animationDuration());
    p.minDistance = animationMinDistance();
    p.sequenceMode = static_cast<PhosphorAnimation::SequenceMode>(animationSequenceMode());
    p.staggerInterval = animationStaggerInterval();
    return QString::fromUtf8(QJsonDocument(p.toJson()).toJson(QJsonDocument::Compact));
}

namespace {
/// Build a Profile from an inline duration + cubic-bezier wire string.
/// `curveWire == nullptr` leaves `curve` unset — the runtime will fall
/// back to the library default OutCubic via `Profile::withDefaults`.
PhosphorAnimation::Profile makeDefaultProfile(qreal durationMs, const char* curveWire)
{
    PhosphorAnimation::Profile p;
    p.duration = durationMs;
    if (curveWire) {
        p.curve = PhosphorAnimation::CurveRegistry::instance().create(QString::fromUtf8(curveWire));
    }
    return p;
}
} // namespace

QHash<QString, PhosphorAnimation::Profile> ConfigDefaults::animationProfilesByPath()
{
    namespace PA = PhosphorAnimation;

    // Wire-format curve strings understood by CurveRegistry:
    //   OutBack overshoot: "0.34,1.56,0.64,1.00"  (CSS ease-out-back)
    //   OutCubic:          "0.33,1.00,0.68,1.00"
    //   InCubic:           "0.32,0.00,0.67,0.00"
    //   Linear:            nullptr (library default OutCubic takes over)
    //
    // Per-path durations + curves were chosen to preserve the
    // pre-PR-344 per-site motion intent — OutBack spring on toggle
    // switches + badge pops, slow linear tint on needs-save, short
    // snappy durations on cursor feedback, etc.
    //
    // Users override any of these by dropping a JSON profile at the
    // matching path under `~/.local/share/plasmazones/profiles/`. The
    // `Global` path follows `animationProfile()` instead and is not
    // in this map.
    QHash<QString, PA::Profile> map;

    // zone.* — zone highlight + tiling motion
    map.insert(PA::ProfilePaths::ZoneHighlight, makeDefaultProfile(150.0, "0.34,1.56,0.64,1.00"));
    map.insert(PA::ProfilePaths::ZoneSnapIn, makeDefaultProfile(200.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::ZoneSnapOut, makeDefaultProfile(200.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::ZoneSnapResize, makeDefaultProfile(200.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::ZoneLayoutSwitchIn, makeDefaultProfile(250.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::ZoneLayoutSwitchOut, makeDefaultProfile(250.0, "0.33,1.00,0.68,1.00"));

    // osd.* — notifications + feedback
    map.insert(PA::ProfilePaths::OsdShow, makeDefaultProfile(200.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::OsdHide, makeDefaultProfile(150.0, "0.32,0.00,0.67,0.00"));
    map.insert(PA::ProfilePaths::OsdDim, makeDefaultProfile(300.0, nullptr));

    // panel.* — docks / sidebars / popups
    map.insert(PA::ProfilePaths::PanelSlideIn, makeDefaultProfile(200.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::PanelSlideOut, makeDefaultProfile(150.0, "0.32,0.00,0.67,0.00"));
    map.insert(PA::ProfilePaths::PanelPopup, makeDefaultProfile(150.0, "0.33,1.00,0.68,1.00"));

    // cursor.* — hover / click / drag feedback
    map.insert(PA::ProfilePaths::CursorHover, makeDefaultProfile(100.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::CursorClick, makeDefaultProfile(100.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::CursorDrag, makeDefaultProfile(50.0, nullptr));

    // widget.* — archetype defaults. These preserve the pre-PR-344
    // feel (OutBack overshoot on toggles + badges, slow linear tint
    // on needs-save, etc.) that the original single-profile migration
    // had collapsed.
    map.insert(PA::ProfilePaths::WidgetHover, makeDefaultProfile(150.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::WidgetPress, makeDefaultProfile(100.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::WidgetToggle, makeDefaultProfile(250.0, "0.34,1.56,0.64,1.00"));
    map.insert(PA::ProfilePaths::WidgetBadge, makeDefaultProfile(200.0, "0.34,1.56,0.64,1.00"));
    map.insert(PA::ProfilePaths::WidgetTint, makeDefaultProfile(300.0, nullptr));
    map.insert(PA::ProfilePaths::WidgetDim, makeDefaultProfile(200.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::WidgetFade, makeDefaultProfile(150.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::WidgetReorder, makeDefaultProfile(200.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::WidgetAccordion, makeDefaultProfile(250.0, "0.33,1.00,0.68,1.00"));
    map.insert(PA::ProfilePaths::WidgetProgress, makeDefaultProfile(200.0, "0.33,1.00,0.68,1.00"));

    return map;
}

} // namespace PlasmaZones
