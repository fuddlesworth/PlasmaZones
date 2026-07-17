// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// Batch read/write surface for SettingsAdaptor:
//   * getSettings — batch read (omit-missing contract)
//   * setSettings — batch write with signal suppression, single save, and the
//     post-commit snapshot/diff/re-emit machinery (metaobject NOTIFY replay,
//     the JSON-facade alias table, the per-mode disable replay table)
//
// Same class as settingsadaptor.cpp, separate TU, no API change (mirrors the
// SettingsController multi-TU split, e.g. settingscontroller_pagestate.cpp).

#include "settingsadaptor.h"
#include "../core/interfaces.h"
#include "../core/dbusvariantutils.h"
#include "../core/logging.h"
#include <PhosphorProtocol/ServiceConstants.h>
#include <QDBusVariant>
#include <QMetaMethod>
#include <QMetaProperty>

namespace PlasmaZones {

QVariantMap SettingsAdaptor::getSettings(const QStringList& keys)
{
    QVariantMap result;
    if (keys.isEmpty()) {
        return result;
    }

    for (const QString& key : keys) {
        if (key.isEmpty()) {
            continue;
        }
        auto it = m_getters.find(key);
        if (it == m_getters.end()) {
            // Unknown keys are expected when callers probe for optional
            // keys; the batch contract is "omit missing, caller uses its
            // own default". Log at debug so production logs stay quiet.
            qCDebug(lcDbusSettings) << "getSettings: unknown key" << key;
            continue;
        }
        QVariant value = it.value()();
        if (!value.isValid()) {
            qCWarning(lcDbusSettings) << "getSettings: setting" << key << "returned invalid variant, omitting";
            continue;
        }
        result.insert(key, value);
    }
    return result;
}

bool SettingsAdaptor::setSettings(const QVariantMap& settings)
{
    if (settings.isEmpty()) {
        qCDebug(lcDbusSettings) << "setSettings: empty map";
        return false;
    }
    if (!m_settings) {
        return false;
    }

    // Stop any pending debounced save — we will save synchronously below
    m_saveTimer->stop();

    // Block all m_settings signals during the batch — each setter emits its own
    // NOTIFY plus the aggregate settingsChanged, which would trigger N daemon
    // handler invocations (autotile transitions, KWin effect reloads) mid-batch
    // with partially-applied state. QSignalBlocker suppresses emissions outright
    // (it does not queue them), so everything the batch changed must be re-emitted
    // explicitly after the blocker scope closes — see below. (This used to say the
    // KCM's notifyReload() drives the reload; it does not — the KCM writes config
    // in-process. Nothing in this tree calls setSettings at all; it is a published
    // D-Bus surface for external clients.)
    //
    // Snapshot every registered getter first so the post-batch re-emit can fire
    // only for values that actually changed (emit-on-change rule), including
    // cross-property side effects (e.g. setZoneSpanModifier rewrites the trigger
    // list). setSettings is a rare external batch entry point, so reading all
    // getters twice is acceptable.
    QVariantMap beforeValues;
    for (auto it = m_getters.constBegin(); it != m_getters.constEnd(); ++it) {
        beforeValues.insert(it.key(), it.value()());
    }

    bool allOk = true;
    {
        QSignalBlocker blocker(m_settings);
        for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
            const QString& key = it.key();
            auto setter = m_setters.find(key);
            if (setter == m_setters.end()) {
                // A key with a getter but no setter is read-only (e.g.
                // motionProfileTree, animationShaderSearchPaths). getAllSettings
                // serializes those, so a getAllSettings -> setSettings round-trip
                // legitimately carries them back; skip them silently rather than
                // failing the whole batch. Only a key unknown to BOTH maps is a
                // genuine error.
                if (!m_getters.contains(key)) {
                    qCDebug(lcDbusSettings) << "setSettings: unknown key" << key;
                    allOk = false;
                }
                continue;
            }
            // Convert QDBusArgument types to plain Qt types before passing to setters.
            // Complex types (QVariantList of QVariantMaps, e.g. dragActivationTriggers)
            // arrive from D-Bus as QDBusArgument objects; without conversion, toList()/toMap()
            // return empty containers, silently zeroing trigger settings.
            QVariant converted = DBusVariantUtils::convertDbusArgument(it.value());
            if (!setter.value()(converted)) {
                qCWarning(lcDbusSettings) << "setSettings: setter failed for key" << key;
                allOk = false;
            }
        }
    }

    // Save once with all values applied
    m_settings->save();

    // Re-emit what the blocker suppressed, now that the whole batch is committed.
    //
    // Per-property NOTIFYs are needed, not just the aggregate: the adaptor runs
    // in the daemon process, and daemon components connect to individual NOTIFY
    // signals (e.g. OverlayService wires audio*Changed / shaderFrameRateChanged
    // to syncCavaState and overlayDisplayModeChanged to overlay recreation), so
    // a lone settingsChanged would leave those consumers stale. Registry keys
    // mirror Q_PROPERTY names, so resolve each changed key to its property's
    // NOTIFY signal via the metaobject. Parameterless signals only — every
    // Q_PROPERTY NOTIFY on Settings is parameterless; keys without a matching
    // property (compound registry-only keys) are covered by the aggregate.
    const QMetaObject* metaObject = m_settings->metaObject();
    bool anyChanged = false;
    for (auto it = m_getters.constBegin(); it != m_getters.constEnd(); ++it) {
        if (it.value()() == beforeValues.value(it.key())) {
            continue;
        }
        anyChanged = true;
        int propertyIndex = metaObject->indexOfProperty(it.key().toUtf8().constData());
        if (propertyIndex < 0) {
            // JSON-facade aliases: these registry keys are registered under the
            // protocol names ("shaderProfileTree"/"decorationProfileTree") but the
            // Settings Q_PROPERTYs carry a Json suffix, so indexOfProperty misses.
            // Daemon consumers (OverlayService) connect to the facade NOTIFYs and
            // do NOT refresh from the aggregate settingsChanged, so skipping them
            // here would leave the overlay stale after a D-Bus batch write.
            static const QHash<QString, QByteArray> jsonFacadeAliases = {
                {QString(PhosphorProtocol::Service::SettingProperty::ShaderProfileTree),
                 QByteArrayLiteral("shaderProfileTreeJson")},
                {QString(PhosphorProtocol::Service::SettingProperty::DecorationProfileTree),
                 QByteArrayLiteral("decorationProfileTreeJson")},
            };
            const auto aliasIt = jsonFacadeAliases.constFind(it.key());
            if (aliasIt == jsonFacadeAliases.constEnd()) {
                continue;
            }
            propertyIndex = metaObject->indexOfProperty(aliasIt.value().constData());
            if (propertyIndex < 0) {
                continue;
            }
        }
        const QMetaMethod notify = metaObject->property(propertyIndex).notifySignal();
        if (notify.isValid() && notify.parameterCount() == 0) {
            notify.invoke(m_settings, Qt::DirectConnection);
        }
    }
    // Per-mode disable lists are the other registry-only surface with dedicated
    // NOTIFYs: their six keys have no Q_PROPERTY (the signals carry a Mode
    // argument, so the parameterless metaobject replay above can never reach
    // them). No daemon component connects to these signals today — the one
    // in-tree consumer is the settings app's ScreenHelper path, which documents
    // routing disable toggles via ISettings::disabledMonitorsChanged(Mode)
    // (src/settings/screenhelper.{h,cpp}) — but the replay keeps parity with
    // Settings::load()'s post-reload contract (settings.cpp): one signal per
    // (axis, mode) pair whose list changed, resolved from the same beforeValues
    // snapshot the property loop uses.
    struct PerModeDisableReplay
    {
        QString key;
        void (ISettings::*signal)(PhosphorZones::AssignmentEntry::Mode);
        PhosphorZones::AssignmentEntry::Mode mode;
    };
    static const PerModeDisableReplay disableReplays[] = {
        {QStringLiteral("snappingDisabledMonitors"), &ISettings::disabledMonitorsChanged,
         PhosphorZones::AssignmentEntry::Snapping},
        {QStringLiteral("autotileDisabledMonitors"), &ISettings::disabledMonitorsChanged,
         PhosphorZones::AssignmentEntry::Autotile},
        {QStringLiteral("snappingDisabledDesktops"), &ISettings::disabledDesktopsChanged,
         PhosphorZones::AssignmentEntry::Snapping},
        {QStringLiteral("autotileDisabledDesktops"), &ISettings::disabledDesktopsChanged,
         PhosphorZones::AssignmentEntry::Autotile},
        {QStringLiteral("snappingDisabledActivities"), &ISettings::disabledActivitiesChanged,
         PhosphorZones::AssignmentEntry::Snapping},
        {QStringLiteral("autotileDisabledActivities"), &ISettings::disabledActivitiesChanged,
         PhosphorZones::AssignmentEntry::Autotile},
    };
    for (const auto& replay : disableReplays) {
        const auto getter = m_getters.constFind(replay.key);
        if (getter == m_getters.constEnd()) {
            continue;
        }
        if (getter.value()() != beforeValues.value(replay.key)) {
            anyChanged = true;
            Q_EMIT(m_settings->*replay.signal)(replay.mode);
        }
    }
    // One committed settingsChanged for the whole batch. Emitting on m_settings
    // (not on the adaptor) reaches BOTH in-process listeners and the D-Bus bus:
    // the constructor relays ISettings::settingsChanged to the adaptor's own
    // D-Bus settingsChanged signal.
    if (anyChanged) {
        QMetaObject::invokeMethod(m_settings, &ISettings::settingsChanged);
    }
    qCInfo(lcDbusSettings) << "setSettings: batch applied" << settings.size() << "keys, allOk:" << allOk;

    return allOk;
}

} // namespace PlasmaZones
