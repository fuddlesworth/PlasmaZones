// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../plasmazoneseffect.h"

#include "../autotilehandler.h"
#include "../navigationhandler.h"
#include "../screenchangehandler.h"
#include "../scrollhandler.h"
#include "../snapassisthandler.h"
#include "../windowanimator.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorIdentity/WindowId.h>
#include <PhosphorProtocol/BridgeMarshalling.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/DragMarshalling.h>
#include <PhosphorProtocol/ServiceConstants.h>
#include <PhosphorProtocol/WindowMarshalling.h>
#include <PhosphorProtocol/ZoneMarshalling.h>

#include <effect/effecthandler.h>

#include <QColor>
#include <QLoggingCategory>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace PlasmaZones {

Q_DECLARE_LOGGING_CATEGORY(lcEffect)

namespace {
// Duplicated from daemon's configkeys.h — effect cannot include daemon headers
constexpr QLatin1String TriggerModifierField("modifier");
constexpr QLatin1String TriggerMouseButtonField("mouseButton");
} // namespace

void PlasmaZonesEffect::slotSettingsChanged()
{
    qCInfo(lcEffect) << "settingsChanged: reloading settings";
    loadCachedSettings();
    // Note: loadAutotileSettings() is intentionally NOT called here.
    // Autotile screen changes are tracked via the dedicated autotileScreensChanged
    // D-Bus signal (→ slotAutotileScreensChanged), which is authoritative.
    // Calling loadAutotileSettings on every settingsChanged causes redundant
    // full window re-notification (N D-Bus windowOpened calls + retile round)
    // on every algorithm/gap/setting change — the daemon already retiles and
    // emits windowsTiled directly for those changes.
}

// Template implementation for loadSettingAsync — delegates to shared helper.
template<typename Fn>
void PlasmaZonesEffect::loadSettingAsync(const QString& name, Fn&& onValue)
{
    PhosphorProtocol::ClientHelpers::loadSettingAsync(this, name, std::forward<Fn>(onValue));
}

void PlasmaZonesEffect::loadCachedSettings()
{
    // Uses raw QDBusMessage (not QDBusInterface) to avoid synchronous introspection
    // that would block the compositor during login (see discussion #158).
    //
    // Transient exclusion and min-size are handled by the daemon. Exclusion lists are
    // cached here for drag-operation gating (shouldHandleWindow).
    m_triggersLoaded = false; // Permissive until new triggers arrive (#175)

    loadSettingAsync(QStringLiteral("excludedApplications"), [this](const QVariant& v) {
        m_excludedApplications = v.toStringList();
    });
    loadSettingAsync(QStringLiteral("excludedWindowClasses"), [this](const QVariant& v) {
        m_excludedWindowClasses = v.toStringList();
    });
    loadSettingAsync(QStringLiteral("minimumWindowWidth"), [this](const QVariant& v) {
        m_cachedMinWindowWidth = v.toInt();
    });
    loadSettingAsync(QStringLiteral("minimumWindowHeight"), [this](const QVariant& v) {
        m_cachedMinWindowHeight = v.toInt();
    });
    loadSettingAsync(QStringLiteral("snapAssistEnabled"), [this](const QVariant& v) {
        m_snapAssistHandler->setEnabled(v.toBool());
    });
    loadSettingAsync(QStringLiteral("animationsEnabled"), [this](const QVariant& v) {
        m_windowAnimator->setEnabled(v.toBool());
    });
    loadSettingAsync(QStringLiteral("animationDuration"), [this](const QVariant& v) {
        // Clamp against the canonical settings-UI bounds. The earlier
        // local 500ms cap silently clamped a 2000ms user setting down
        // to 500ms, making shader transitions like matrix run far
        // faster than the daemon path's identical setting (the daemon
        // honours the full 2000ms range via the same constants).
        const int d = qBound(PhosphorAnimation::Limits::MinAnimationDurationMs, v.toInt(),
                             PhosphorAnimation::Limits::MaxAnimationDurationMs);
        m_windowAnimator->setDuration(d);
        m_cachedAnimationDuration = d;
    });
    loadSettingAsync(QStringLiteral("animationEasingCurve"), [this](const QVariant& v) {
        // Polymorphic curve parse — handles bare bezier, named easing,
        // and "spring:..." in one path so Spring can drive snap motion
        // end-to-end without a settings-side branch.
        m_windowAnimator->setCurve(m_curveRegistry.create(v.toString()));
    });
    loadSettingAsync(QStringLiteral("animationMinDistance"), [this](const QVariant& v) {
        m_windowAnimator->setMinDistance(qBound(0, v.toInt(), 200));
    });
    loadSettingAsync(QStringLiteral("animationSequenceMode"), [this](const QVariant& v) {
        m_cachedAnimationSequenceMode = qBound(0, v.toInt(), 1);
    });
    loadSettingAsync(QStringLiteral("animationStaggerInterval"), [this](const QVariant& v) {
        m_cachedAnimationStaggerInterval = qBound(PhosphorAnimation::Limits::MinAnimationStaggerIntervalMs, v.toInt(),
                                                  PhosphorAnimation::Limits::MaxAnimationStaggerIntervalMs);
    });

    // Animation window filtering — independent of the snapping/tiling
    // exclusions cached above. Used by `shouldAnimateWindow()` to gate
    // the animation cascade; class-pattern rules override the filter
    // at the resolver layer so a targeted rule can re-enable animation
    // for an otherwise-excluded app.
    loadSettingAsync(QStringLiteral("animationExcludeTransientWindows"), [this](const QVariant& v) {
        m_animationExcludeTransientWindows = v.toBool();
    });
    // Default true (exclude). Guard on isValid() so a failed reply
    // leaves the member at its `true` init rather than `toBool()`'s
    // false-on-invalid — otherwise a missed fetch would animate
    // notifications/OSDs until the next successful settings load.
    loadSettingAsync(QStringLiteral("animationExcludeNotificationsAndOsd"), [this](const QVariant& v) {
        if (v.isValid()) {
            m_animationExcludeNotificationsAndOsd = v.toBool();
        }
    });
    // Clamp on the effect side as a defence-in-depth — the daemon's
    // schema validator already bounds these to [0, 2000], but a
    // malformed reply (`toInt()` returning 0 on a non-int variant or
    // a negative value from an out-of-spec callsite) would otherwise
    // silently disable / invert the min-size gate. Kept symmetric with
    // `animationDuration`'s `qBound` clamp above.
    loadSettingAsync(QStringLiteral("animationMinimumWindowWidth"), [this](const QVariant& v) {
        m_animationMinWindowWidth = qBound(0, v.toInt(), 2000);
    });
    loadSettingAsync(QStringLiteral("animationMinimumWindowHeight"), [this](const QVariant& v) {
        m_animationMinWindowHeight = qBound(0, v.toInt(), 2000);
    });
    loadSettingAsync(QStringLiteral("animationExcludedApplications"), [this](const QVariant& v) {
        m_animationExcludedApplications = v.toStringList();
    });
    loadSettingAsync(QStringLiteral("animationExcludedWindowClasses"), [this](const QVariant& v) {
        m_animationExcludedWindowClasses = v.toStringList();
    });

    loadShaderProfileFromDbus();
    loadAnimationAppRulesFromDbus();
    loadMotionProfileTreeFromDbus();
    loadShaderRegistryFromDbus();
    loadSettingAsync(QStringLiteral("toggleActivation"), [this](const QVariant& v) {
        m_cachedToggleActivation = v.toBool();
    });
    loadSettingAsync(QStringLiteral("autotileDragInsertToggle"), [this](const QVariant& v) {
        m_cachedAutotileDragInsertToggle = v.toBool();
    });
    loadSettingAsync(QStringLiteral("autotileDragBehavior"), [this](const QVariant& v) {
        // Clamp unknown values to the safe default (Float) rather than the
        // highest known value — an older effect build against a newer daemon
        // must not silently map e.g. a future `ReorderAcrossScreens=2` onto
        // the nearest mode it happens to recognize.
        const int raw = v.toInt();
        switch (raw) {
        case static_cast<int>(EffectAutotileDragBehavior::Float):
            m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Float;
            break;
        case static_cast<int>(EffectAutotileDragBehavior::Reorder):
            m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Reorder;
            break;
        default:
            m_cachedAutotileDragBehavior = EffectAutotileDragBehavior::Float;
            break;
        }
    });
    loadSettingAsync(QStringLiteral("zoneSelectorEnabled"), [this](const QVariant& v) {
        m_cachedZoneSelectorEnabled = v.toBool();
    });

    // autotileHideTitleBars needs extra logic when toggled off — delegate to handler
    loadSettingAsync(QStringLiteral("autotileHideTitleBars"), [this](const QVariant& v) {
        m_autotileHandler->updateHideTitleBarsSetting(v.toBool());
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileShowBorder"), [this](const QVariant& v) {
        m_autotileHandler->updateShowBorderSetting(v.toBool());
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileBorderWidth"), [this](const QVariant& v) {
        int bw = qBound(0, v.toInt(), 10);
        if (m_autotileHandler->borderWidth() != bw) {
            m_autotileHandler->setBorderWidth(bw);
            // Invalidate pending stagger timers that would use the old border width
            m_autotileHandler->invalidateStaggerGeneration();
            PhosphorProtocol::ClientHelpers::fireAndForget(this, PhosphorProtocol::Service::Interface::Autotile,
                                                           QStringLiteral("retileAllScreens"), {},
                                                           QStringLiteral("border width change retile"));
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("autotileBorderRadius"), [this](const QVariant& v) {
        int br = qBound(0, v.toInt(), 20);
        if (m_autotileHandler->borderRadius() != br) {
            m_autotileHandler->setBorderRadius(br);
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("autotileBorderColor"), [this](const QVariant& v) {
        m_autotileHandler->setBorderColor(QColor(v.toString()));
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileInactiveBorderColor"), [this](const QVariant& v) {
        m_autotileHandler->setInactiveBorderColor(QColor(v.toString()));
        updateAllBorders();
    });

    loadSettingAsync(QStringLiteral("autotileFocusFollowsMouse"), [this](const QVariant& v) {
        m_autotileHandler->setFocusFollowsMouse(v.toBool());
    });

    // ── Scroll-mode appearance ───────────────────────────────────────────────
    // Scroll columns carry their own border/decoration settings, independent
    // of autotile's. The daemon resolves scrollUseSystemBorderColors into the
    // scrollBorder*Color values, so the effect just reads the final colors.
    loadSettingAsync(QStringLiteral("scrollShowBorder"), [this](const QVariant& v) {
        // Guard on isValid(): a failed D-Bus reply must not silently disable
        // the border (toBool() returns false on an invalid variant). Only
        // rebuild borders when the value actually changed.
        if (v.isValid() && m_scrollHandler->updateShowBorderSetting(v.toBool())) {
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("scrollBorderWidth"), [this](const QVariant& v) {
        const int bw = qBound(0, v.toInt(), 10);
        if (m_scrollHandler->borderWidth() != bw) {
            m_scrollHandler->setBorderWidth(bw);
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("scrollBorderRadius"), [this](const QVariant& v) {
        const int br = qBound(0, v.toInt(), 20);
        if (m_scrollHandler->borderRadius() != br) {
            m_scrollHandler->setBorderRadius(br);
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("scrollBorderColor"), [this](const QVariant& v) {
        // A failed reply yields an empty string → invalid QColor; keep the
        // prior colour rather than dropping the border. Rebuild only on change.
        if (!v.isValid()) {
            return;
        }
        const QColor c(v.toString());
        if (c.isValid() && m_scrollHandler->borderColor() != c) {
            m_scrollHandler->setBorderColor(c);
            updateAllBorders();
        }
    });

    loadSettingAsync(QStringLiteral("scrollInactiveBorderColor"), [this](const QVariant& v) {
        if (!v.isValid()) {
            return;
        }
        const QColor c(v.toString());
        if (c.isValid() && m_scrollHandler->inactiveBorderColor() != c) {
            m_scrollHandler->setInactiveBorderColor(c);
            updateAllBorders();
        }
    });

    // scrollHideTitleBars needs extra logic when toggled off — delegate to handler.
    // Guard on isValid() so a failed reply does not un-hide title bars, and
    // rebuild borders only when the value changed.
    loadSettingAsync(QStringLiteral("scrollHideTitleBars"), [this](const QVariant& v) {
        if (v.isValid() && m_scrollHandler->updateHideTitleBarsSetting(v.toBool())) {
            updateAllBorders();
        }
    });

    // ── Scroll-mode behavior ─────────────────────────────────────────────────
    loadSettingAsync(QStringLiteral("scrollFocusFollowsMouse"), [this](const QVariant& v) {
        m_scrollHandler->setFocusFollowsMouse(v.toBool());
    });

    loadSettingAsync(QStringLiteral("scrollFocusNewWindows"), [this](const QVariant& v) {
        m_scrollHandler->setFocusNewWindows(v.toBool());
    });

    // dragActivationTriggers — uses shared TriggerParser for QDBusArgument deserialization
    {
        PhosphorProtocol::ClientHelpers::loadSettingAsync(
            this, QStringLiteral("dragActivationTriggers"), [this](const QVariant& v) {
                m_parsedTriggers = TriggerParser::parseTriggers(v, TriggerModifierField, TriggerMouseButtonField);

                qCDebug(lcEffect) << "Loaded dragActivationTriggers:" << m_parsedTriggers.size() << "triggers";
                bool anyValid =
                    std::any_of(m_parsedTriggers.cbegin(), m_parsedTriggers.cend(), [](const ParsedTrigger& pt) {
                        return pt.modifier != 0 || pt.mouseButton != 0;
                    });
                if (!m_parsedTriggers.isEmpty() && !anyValid) {
                    qCWarning(lcEffect) << "All triggers have modifier=0 mouseButton=0"
                                        << "- possible deserialization issue";
                }
                m_triggersLoaded = true;
            });
    }

    qCDebug(lcEffect) << "Loading cached settings asynchronously, using defaults until loaded";
}

} // namespace PlasmaZones
