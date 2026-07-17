// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

// NOTE: multi-TU class. initializeRegistry() lives in
// settingsadaptor_registry.cpp; the batch getSettings/setSettings surface
// lives in settingsadaptor_batch.cpp (same class, no API change).

#include "settingsadaptor.h"
#include "../core/interfaces.h"
#include "../core/dbusvariantutils.h"
#include <PhosphorAnimation/PhosphorProfileRegistry.h>
#include "../core/logging.h"
#include "../core/shaderregistry.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QDBusVariant>
#include <functional>
#include <optional>

namespace PlasmaZones {

SettingsAdaptor::SettingsAdaptor(ISettings* settings, ShaderRegistry* shaderRegistry,
                                 PhosphorAnimation::PhosphorProfileRegistry* profileRegistry, QObject* parent)
    : QDBusAbstractAdaptor(parent)
    , m_settings(settings)
    , m_shaderRegistry(shaderRegistry)
    , m_profileRegistry(profileRegistry)
    , m_saveTimer(new QTimer(this))
    , m_motionTreeNotifyTimer(new QTimer(this))
{
    // The assert is the developer-facing half. The RELEASE half is the early return: every
    // registry lambda below closes over m_settings, and the debounced save timer
    // dereferences it outright, so a null here is a crash on the first getSetting rather
    // than a Qt warning. The class is built to tolerate a null m_settings elsewhere (detach()
    // exists precisely for that), so refusing to wire anything up is both safe and honest —
    // an adaptor with no settings behind it answers nothing, which beats answering wrongly.
    Q_ASSERT(settings);
    if (!settings) {
        qCCritical(lcDbusSettings) << "SettingsAdaptor constructed with no settings — the D-Bus settings surface will "
                                      "not answer";
        return;
    }
    initializeRegistry();

    // Configure debounced save timer (performance optimization)
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(SaveDebounceMs);
    connect(m_saveTimer, &QTimer::timeout, this, [this]() {
        m_settings->save();
        qCInfo(lcDbusSettings) << "Settings save completed";
    });

    // Configure the debounced motion-profile-tree notifier (see member doc).
    m_motionTreeNotifyTimer->setSingleShot(true);
    m_motionTreeNotifyTimer->setInterval(MotionTreeNotifyDebounceMs);
    connect(m_motionTreeNotifyTimer, &QTimer::timeout, this, &SettingsAdaptor::motionProfileTreeChanged);

    // Connect to interface signals (DIP)
    connect(m_settings, &ISettings::settingsChanged, this, &SettingsAdaptor::settingsChanged);

    // The per-event motion-profile registry is a second source of
    // settings-shaped state: editing a `window.open` duration rewrites
    // a `profiles/*.json` file, the daemon's ProfileLoader file-watch
    // rescans it into the registry, and the registry fires
    // profileChanged / profilesReloaded / ownerReloaded. The kwin-effect
    // (a separate process) must re-fetch `motionProfileTree` when that
    // happens, so bridge the registry mutations to a DEDICATED
    // `motionProfileTreeChanged` D-Bus signal — NOT the generic
    // `settingsChanged`. The Settings app listens only to
    // `settingsChanged`; routing registry mutations there made the
    // daemon echo the app's own immediate profile-file writes back
    // through onExternalSettingsChanged(), which reset its value-change
    // / save-discard detection. The effect subscribes to
    // `motionProfileTreeChanged` specifically; the app never sees it.
    //
    // The three registry signals feed the debounce timer rather than
    // motionProfileTreeChanged directly: a reloadFromOwner() batch emits
    // one profileChanged per path plus a closing ownerReloaded, so a
    // direct bridge would fan one logical change out into N+1 D-Bus
    // emissions. start() on a single-shot timer collapses the burst.
    if (m_profileRegistry) {
        connect(m_profileRegistry, &PhosphorAnimation::PhosphorProfileRegistry::profileChanged, m_motionTreeNotifyTimer,
                qOverload<>(&QTimer::start));
        connect(m_profileRegistry, &PhosphorAnimation::PhosphorProfileRegistry::profilesReloaded,
                m_motionTreeNotifyTimer, qOverload<>(&QTimer::start));
        connect(m_profileRegistry, &PhosphorAnimation::PhosphorProfileRegistry::ownerReloaded, m_motionTreeNotifyTimer,
                qOverload<>(&QTimer::start));
    }

    // Drop shader caches whenever the registry reloads from disk. The
    // registry is per-process and injected via constructor; unit tests
    // that don't pass one get nullptr and skip this connection.
    // ShaderRegistry::refresh() is always invoked from the main thread
    // (D-Bus refreshShaders slot) so a direct connection is safe; if
    // that invariant ever changes, switch to Qt::QueuedConnection here.
    if (m_shaderRegistry) {
        connect(m_shaderRegistry, &ShaderRegistry::shadersChanged, this, &SettingsAdaptor::invalidateShaderCaches);
    }
}

SettingsAdaptor::~SettingsAdaptor()
{
    // Flush any pending debounced saves before destruction so config
    // changes aren't lost on shutdown. Skipped when m_settings has already
    // been cleared via detach() — the owning Daemon performs the save
    // itself in stop(), and our borrowed pointer would dangle by the time
    // ~QObject destroys us (the owning unique_ptr has already run).
    if (m_settings && m_saveTimer->isActive()) {
        m_saveTimer->stop();
        m_settings->save();
        qCInfo(lcDbusSettings) << "Flushed pending save on destruction";
    }
}

void SettingsAdaptor::detach()
{
    // Flush once here — Daemon::stop() also calls m_settings->save()
    // explicitly, so the pending-write isn't really at risk, but keeping
    // the flush makes detach() safe to call from any shutdown path, not
    // just the daemon's happy-path. m_saveTimer is constructed in the
    // initializer list and never nulled, so no null-check is needed on
    // it here (the dtor uses the same pattern).
    if (m_settings && m_saveTimer->isActive()) {
        m_saveTimer->stop();
        m_settings->save();
    }
    if (m_settings) {
        disconnect(m_settings, nullptr, this, nullptr);
    }
    if (m_shaderRegistry) {
        disconnect(m_shaderRegistry, nullptr, this, nullptr);
    }
    if (m_profileRegistry) {
        disconnect(m_profileRegistry, nullptr, this, nullptr);
    }
    // Cancel any pending coalesced motion-tree notification — its inputs
    // are severed above and the daemon is tearing down. Constructed in
    // the initializer list and never nulled, so no null-check (same as
    // m_saveTimer).
    m_motionTreeNotifyTimer->stop();
    // Clear the registries before nulling m_settings so a queued D-Bus
    // call that slipped past unregisterObject() lands on an empty-getter
    // hash (returning an empty QVariant) instead of the registered
    // lambdas, which close over `this` and would deref a null m_settings.
    m_getters.clear();
    m_setters.clear();
    m_cachedAvailableShaders.clear();
    m_cachedAvailableShadersValid = false;
    m_cachedShaderInfo.clear();
    m_cachedShaderDefaults.clear();
    m_settings = nullptr;
    m_shaderRegistry = nullptr;
    m_profileRegistry = nullptr;
}

void SettingsAdaptor::scheduleSave()
{
    // Restart the timer on each setting change (debouncing)
    // This batches multiple rapid changes into a single save
    m_saveTimer->start();
}

void SettingsAdaptor::reloadSettings()
{
    if (!m_settings) {
        return;
    }
    m_settings->load();
}

void SettingsAdaptor::saveSettings()
{
    if (!m_settings) {
        return;
    }
    m_settings->save();
}

void SettingsAdaptor::resetToDefaults()
{
    if (!m_settings) {
        return;
    }
    m_settings->reset();
}

QString SettingsAdaptor::getAllSettings()
{
    QJsonObject settings;
    for (auto it = m_getters.constBegin(); it != m_getters.constEnd(); ++it) {
        settings[it.key()] = QJsonValue::fromVariant(it.value()());
    }
    return QString::fromUtf8(QJsonDocument(settings).toJson());
}

QDBusVariant SettingsAdaptor::getSetting(const QString& key)
{
    // An empty key is a caller bug in exactly the way an unknown one is, so it gets
    // exactly the same answer (see the long note below). Handing back a valid empty
    // string here while the unknown-key path raised would have left one of the two
    // failing silently and the other loudly, which is how the silent one survives.
    if (key.isEmpty()) {
        qCDebug(lcDbusSettings) << "getSetting: empty key";
        if (calledFromDBus()) {
            sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Empty setting key"));
        }
        return QDBusVariant(QVariant());
    }

    auto it = m_getters.find(key);
    if (it != m_getters.end()) {
        QVariant value = it.value()();
        // A REGISTERED key that answers with an invalid variant is a bug in that
        // getter, not a missing registration, and the caller asked for a real setting
        // that really exists. An empty string of the right shape keeps the reply
        // marshallable; the warning is what surfaces the getter.
        if (!value.isValid()) {
            qCWarning(lcDbusSettings) << "Setting" << key << "returned invalid variant, using empty string";
            return QDBusVariant(QVariant(QString()));
        }
        return QDBusVariant(value);
    }

    // An unknown key is an ERROR, not an empty value.
    //
    // This map is hand-maintained (the REGISTER_*_SETTING block), not derived from
    // the metaobject, so "somebody added a setting and forgot to register it" is a
    // live failure mode — it has already happened. While a miss answered with a
    // valid empty string, every caller coerced it blind: QVariant("").toBool() is
    // false, so a forgotten registration silently forced the setting off, INVERTING
    // any default-true one, with nothing but a daemon-side warning to show for it.
    //
    // A real error reply makes QDBusPendingReply::isValid() false in
    // ClientHelpers::loadSettingAsync, so the value callback never runs and the
    // caller simply keeps its own default. Guarded once here rather than pushed out
    // to ~50 call sites as a defensive type-check.
    // DEBUG, matching the batch path. An unknown key is answered with a D-Bus error, which
    // is the real signal; logging it at warning level let any process on the session bus
    // fill the daemon's log with content of its own choosing, one key at a time.
    qCDebug(lcDbusSettings) << "getSetting: unknown key" << key;
    if (calledFromDBus()) {
        sendErrorReply(QDBusError::InvalidArgs, QStringLiteral("Unknown setting key: %1").arg(key));
    }
    // The return value is discarded once sendErrorReply has run; it matters only for
    // a direct (non-D-Bus) call, where an invalid variant is the honest answer.
    return QDBusVariant(QVariant());
}

bool SettingsAdaptor::setSetting(const QString& key, const QDBusVariant& value)
{
    // DEBUG, not warning, and the same reasoning as getSetting's unknown-key path: the key
    // comes from whoever is on the session bus, the caller is already told `false`, and a
    // warning here let any process fill the daemon's log with content of its own choosing.
    if (key.isEmpty()) {
        qCDebug(lcDbusSettings) << "setSetting: empty key";
        return false;
    }

    auto it = m_setters.find(key);
    if (it == m_setters.end()) {
        qCDebug(lcDbusSettings) << "setSetting: unknown key" << key;
        return false;
    }

    const QVariant converted = DBusVariantUtils::convertDbusArgument(value.variant());

    // Value-equality guard: if the incoming value already matches the
    // currently stored value, skip the setter invocation and the debounced
    // save-timer restart. Covers scalar variant types plus the two Qt
    // composite types that the settings surface actually uses
    // (QVariantList and QVariantMap) — both compare structurally via
    // element-wise recursion in Qt's operator==, so the guard is reliable
    // for keys like dragActivationTriggers (list-of-maps) that would
    // otherwise always take the full setter path on every idle UI tick.
    //
    // The schema map's type string is NOT used as the gate because at
    // least one key (dragActivationTriggers) advertises "stringlist" while
    // actually storing a list-of-maps — the actual QVariant type is the
    // authoritative source. Types outside this allow-list (custom QObject
    // pointers, exotic Q_DECLARE_METATYPE payloads) fall through to the
    // full setter to avoid false negatives from a non-structural operator==.
    const int typeId = converted.metaType().id();
    const bool comparableType =
        (typeId == QMetaType::Bool || typeId == QMetaType::Int || typeId == QMetaType::UInt
         || typeId == QMetaType::LongLong || typeId == QMetaType::ULongLong || typeId == QMetaType::Double
         || typeId == QMetaType::Float || typeId == QMetaType::QString || typeId == QMetaType::QStringList
         || typeId == QMetaType::QUrl || typeId == QMetaType::QByteArray || typeId == QMetaType::QChar
         || typeId == QMetaType::QVariantList || typeId == QMetaType::QVariantMap);
    if (comparableType) {
        auto getterIt = m_getters.constFind(key);
        if (getterIt != m_getters.constEnd()) {
            const QVariant current = getterIt.value()();
            if (current.isValid() && current == converted) {
                // Already current — nothing to do. Return true because the
                // post-condition ("the setting now equals the supplied value")
                // holds, which is what D-Bus callers rely on.
                return true;
            }
        }
    } else {
        // Exotic-type fall-through. Debug-log once so a future caller that
        // tries to optimize same-value writes on a new custom type can
        // trace back to why their key isn't being short-circuited.
        qCDebug(lcDbusSettings) << "setSetting: value-equality guard skipped for non-comparable type"
                                << converted.metaType().name() << "key=" << key;
    }

    const bool result = it.value()(converted);
    if (result) {
        // Use debounced save instead of immediate save (performance optimization)
        // This batches multiple rapid setting changes into a single disk write
        scheduleSave();
        qCDebug(lcDbusSettings) << "Setting" << key << "updated, save scheduled";
    } else {
        qCWarning(lcDbusSettings) << "Failed to set setting:" << key;
    }
    return result;
}

QStringList SettingsAdaptor::getSettingKeys()
{
    return m_getters.keys();
}

QString SettingsAdaptor::getSettingSchema(const QString& key)
{
    QJsonObject result;

    if (key.isEmpty()) {
        qCDebug(lcDbusSettings) << "getSettingSchema: empty key";
        return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
    }

    auto it = m_schemas.find(key);
    if (it != m_schemas.end()) {
        result[QLatin1String("key")] = key;
        result[QLatin1String("type")] = it.value();
    } else {
        qCDebug(lcDbusSettings) << "getSettingSchema: unknown key" << key;
    }

    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

QString SettingsAdaptor::getAllSettingSchemas()
{
    QJsonObject result;

    for (auto it = m_schemas.constBegin(); it != m_schemas.constEnd(); ++it) {
        QJsonObject entry;
        entry[QLatin1String("type")] = it.value();
        result[it.key()] = entry;
    }

    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

// ═══════════════════════════════════════════════════════════════════════════════
// Per-Screen Settings D-Bus Methods
// ═══════════════════════════════════════════════════════════════════════════════

// Category → {get, set, clear} dispatch table. Closing over @p settings
// lets every per-screen D-Bus entry point share one dispatch definition
// instead of re-deriving the if/else ladder per call. Every method is
// on ISettings with a default no-op body, so backends that don't
// support per-screen state (test stubs) simply inherit the no-op —
// no qobject_cast required.
namespace {
struct PerScreenDispatch
{
    std::function<QVariantMap(const QString&)> get;
    std::function<void(const QString&, const QString&, const QVariant&)> set;
    std::function<void(const QString&)> clear;
    /// False for read-only categories (per-screen snapping, which is a read-only
    /// projection of the config per-monitor gaps): the getter resolves the live
    /// values, but set/clear have no backing surface of their own. Writers reject
    /// the call instead of reporting a phantom success and triggering a pointless
    /// save.
    bool writable = true;
};

std::optional<PerScreenDispatch> dispatchFor(ISettings* settings, const QString& category)
{
    if (category == QLatin1String("autotile")) {
        return PerScreenDispatch{
            [settings](const QString& id) {
                return settings->getPerScreenAutotileSettings(id);
            },
            [settings](const QString& id, const QString& k, const QVariant& v) {
                settings->setPerScreenAutotileSetting(id, k, v);
            },
            [settings](const QString& id) {
                settings->clearPerScreenAutotileSettings(id);
            },
        };
    }
    if (category == QLatin1String("snapping")) {
        // Per-screen snapping gaps are a read-only projection of the config
        // per-monitor gap overrides; there is no separate snapping writer surface.
        // Mark the category read-only so writers reject set/clear (write per-monitor
        // gaps via the "autotile" category, setPerScreenAutotileSetting, which the
        // unified per-monitor gap store lives under) rather than silently succeeding.
        return PerScreenDispatch{
            [settings](const QString& id) {
                return settings->getPerScreenSnappingSettings(id);
            },
            [](const QString&, const QString&, const QVariant&) { },
            [](const QString&) { },
            /*writable=*/false,
        };
    }
    if (category == QLatin1String("zoneSelector")) {
        return PerScreenDispatch{
            [settings](const QString& id) {
                return settings->getPerScreenZoneSelectorSettings(id);
            },
            [settings](const QString& id, const QString& k, const QVariant& v) {
                settings->setPerScreenZoneSelectorSetting(id, k, v);
            },
            [settings](const QString& id) {
                settings->clearPerScreenZoneSelectorSettings(id);
            },
        };
    }
    return std::nullopt;
}
} // namespace

namespace {

/// Is @p screenId a plausible screen identifier, as far as this BOUNDARY can tell?
///
/// Shape only, deliberately. A per-screen setting is legitimately written for a monitor that
/// is not currently connected (the config outlives the cable), so refusing an id that no live
/// QScreen matches would break saved configuration rather than protect anything. What this
/// does refuse is what a screen id can never be: empty, absurdly long, or carrying control
/// characters — which is how a hostile session-bus peer would grow the config file without
/// bound and smuggle newlines into anything that later prints one.
///
/// CLAUDE.md: "Input validation at system boundaries." This is that boundary: every one of
/// the three per-screen writers is a D-Bus slot reachable by any process on the session bus.
bool isPlausibleScreenId(const QString& screenId)
{
    constexpr int kMaxScreenIdLength = 256;
    if (screenId.isEmpty() || screenId.size() > kMaxScreenIdLength) {
        return false;
    }
    return std::none_of(screenId.cbegin(), screenId.cend(), [](QChar c) {
        return c.category() == QChar::Other_Control;
    });
}

} // namespace

void SettingsAdaptor::setPerScreenSetting(const QString& screenId, const QString& category, const QString& key,
                                          const QDBusVariant& value)
{
    if (!m_settings) {
        return;
    }
    if (!isPlausibleScreenId(screenId)) {
        qCDebug(lcDbusSettings) << "setPerScreenSetting: implausible screen id (rejected at the D-Bus boundary)";
        return;
    }

    auto dispatch = dispatchFor(m_settings, category);
    if (!dispatch) {
        qCDebug(lcDbusSettings) << "setPerScreenSetting: unknown category" << category;
        return;
    }
    if (!dispatch->writable) {
        qCWarning(lcDbusSettings)
            << "setPerScreenSetting: category" << category
            << "is read-only (a projection of config per-monitor gaps) — write via the autotile per-screen category";
        return;
    }
    dispatch->set(screenId, key, value.variant());
    scheduleSave();
}

void SettingsAdaptor::clearPerScreenSettings(const QString& screenId, const QString& category)
{
    if (!m_settings) {
        return;
    }
    if (!isPlausibleScreenId(screenId)) {
        qCDebug(lcDbusSettings) << "clearPerScreenSettings: implausible screen id (rejected at the D-Bus boundary)";
        return;
    }

    auto dispatch = dispatchFor(m_settings, category);
    if (!dispatch) {
        qCDebug(lcDbusSettings) << "clearPerScreenSettings: unknown category" << category;
        return;
    }
    if (!dispatch->writable) {
        qCWarning(lcDbusSettings)
            << "clearPerScreenSettings: category" << category
            << "is read-only (a projection of config per-monitor gaps) — clear via the autotile per-screen category";
        return;
    }
    dispatch->clear(screenId);
    scheduleSave();
}

QVariantMap SettingsAdaptor::getPerScreenSettings(const QString& screenId, const QString& category)
{
    if (!m_settings) {
        return {};
    }
    auto dispatch = dispatchFor(m_settings, category);
    if (!dispatch) {
        qCDebug(lcDbusSettings) << "getPerScreenSettings: unknown category" << category;
        return {};
    }
    return dispatch->get(screenId);
}

bool SettingsAdaptor::setPerScreenSettings(const QString& screenId, const QString& category, const QVariantMap& values)
{
    if (!m_settings) {
        return false;
    }
    if (!isPlausibleScreenId(screenId)) {
        qCDebug(lcDbusSettings) << "setPerScreenSettings: implausible screen id (rejected at the D-Bus boundary)";
        return false;
    }
    auto dispatch = dispatchFor(m_settings, category);
    if (!dispatch) {
        qCDebug(lcDbusSettings) << "setPerScreenSettings: unknown category" << category;
        return false;
    }
    if (!dispatch->writable) {
        qCWarning(lcDbusSettings)
            << "setPerScreenSettings: category" << category
            << "is read-only (a projection of config per-monitor gaps) — write via the autotile per-screen category";
        return false;
    }
    // An empty map is a valid no-op, so callers need not guard for it — but only AFTER the
    // category has been validated. Short-circuiting on it first (as this did) meant an empty
    // write to an unknown category, or to the read-only "snapping" projection, reported
    // SUCCESS. The return value is the caller's only way to learn it wrote to the wrong
    // category, and an empty batch is exactly when a caller is least likely to notice.
    if (values.isEmpty()) {
        return true;
    }

    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        // Values arriving over the wire can be QDBusArgument-wrapped when
        // they contain lists or maps; normalize to plain Qt types first.
        // (The single-key setPerScreenSetting path passes the QDBusVariant's
        // payload through raw — fine there because per-screen values are
        // scalars, which demarshal to plain QVariants; this batch path
        // normalizes defensively since a map payload arrives wrapped.)
        const QVariant converted = DBusVariantUtils::convertDbusArgument(it.value());
        dispatch->set(screenId, it.key(), converted);
    }

    // One debounced save for the whole batch. The per-screen save path
    // coalesces multiple category updates into a single disk write.
    scheduleSave();
    // DEBUG: screenId and category are caller-supplied and unvalidated, and every bus peer
    // can drive this in a loop. Same reasoning as the unknown-key paths above.
    qCDebug(lcDbusSettings) << "setPerScreenSettings: batch applied" << values.size() << "keys on screen" << screenId
                            << "category" << category;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Shader Registry D-Bus Methods
// ═══════════════════════════════════════════════════════════════════════════════

QVariantList SettingsAdaptor::availableShaders()
{
    if (m_cachedAvailableShadersValid) {
        return m_cachedAvailableShaders;
    }
    auto* registry = m_shaderRegistry;
    if (!registry) {
        return QVariantList();
    }
    m_cachedAvailableShaders = registry->availableShadersVariant();
    m_cachedAvailableShadersValid = true;
    return m_cachedAvailableShaders;
}

QVariantMap SettingsAdaptor::shaderInfo(const QString& shaderId)
{
    auto it = m_cachedShaderInfo.constFind(shaderId);
    if (it != m_cachedShaderInfo.constEnd()) {
        return it.value();
    }
    auto* registry = m_shaderRegistry;
    if (!registry) {
        return QVariantMap();
    }
    const QVariantMap info = registry->shaderInfo(shaderId);
    m_cachedShaderInfo.insert(shaderId, info);
    return info;
}

QVariantMap SettingsAdaptor::defaultShaderParams(const QString& shaderId)
{
    auto it = m_cachedShaderDefaults.constFind(shaderId);
    if (it != m_cachedShaderDefaults.constEnd()) {
        return it.value();
    }
    auto* registry = m_shaderRegistry;
    if (!registry) {
        return QVariantMap();
    }
    const QVariantMap defaults = registry->defaultParams(shaderId);
    m_cachedShaderDefaults.insert(shaderId, defaults);
    return defaults;
}

void SettingsAdaptor::invalidateShaderCaches()
{
    m_cachedAvailableShaders.clear();
    m_cachedAvailableShadersValid = false;
    m_cachedShaderInfo.clear();
    m_cachedShaderDefaults.clear();
}

QVariantMap SettingsAdaptor::translateShaderParams(const QString& shaderId, const QVariantMap& params)
{
    auto* registry = m_shaderRegistry;
    return registry ? registry->translateParamsToUniforms(shaderId, params) : QVariantMap();
}

bool SettingsAdaptor::shadersEnabled()
{
    auto* registry = m_shaderRegistry;
    return registry ? registry->shadersEnabled() : false;
}

bool SettingsAdaptor::userShadersEnabled()
{
    auto* registry = m_shaderRegistry;
    return registry ? registry->userShadersEnabled() : false;
}

QString SettingsAdaptor::userShaderDirectory()
{
    auto* registry = m_shaderRegistry;
    return registry ? registry->userShaderDirectory() : QString();
}

void SettingsAdaptor::openUserShaderDirectory()
{
    auto* registry = m_shaderRegistry;
    if (registry) {
        registry->openUserShaderDirectory();
    }
}

void SettingsAdaptor::refreshShaders()
{
    // Drop our memoized view before asking the registry to reload — if
    // the shadersChanged signal isn't connected (e.g. the registry was
    // never injected in this composition root), we still guarantee the
    // next D-Bus query hits the fresh registry.
    invalidateShaderCaches();
    auto* registry = m_shaderRegistry;
    if (registry) {
        registry->refresh();
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// Window Picker D-Bus Methods
// ═══════════════════════════════════════════════════════════════════════════════

void SettingsAdaptor::requestRunningWindows()
{
    // Fire-and-forget. Callers receive the reply asynchronously via the
    // runningWindowsAvailable signal (emitted from provideRunningWindows).
    // Safe to call while a previous request is still in flight — the
    // effect side is idempotent, and the last-arriving payload is the
    // one subscribers see.
    Q_EMIT runningWindowsRequested();
}

void SettingsAdaptor::provideRunningWindows(const QString& json)
{
    // Fan out to every client that subscribed to runningWindowsAvailable —
    // SettingsController caches the last payload on the client side, so
    // there is no server-side state to keep here.
    Q_EMIT runningWindowsAvailable(json);
}

} // namespace PlasmaZones
