// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "plasmazoneseffect.h"

#include "autotilehandler/autotilehandler.h"
#include "handlers/snapassisthandler.h"
#include "handlers/snaphandler.h"
#include "compositor/windowanimator.h"

#include <PhosphorAnimation/AnimationLimits.h>
#include <PhosphorAnimation/CurveRegistry.h>
#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorProtocol/ClientHelpers.h>
#include <PhosphorProtocol/ServiceConstants.h>

#include <effect/effecthandler.h>

#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

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

    // excludedApplications / excludedWindowClasses are GONE — the v4
    // migration folded those lists into the unified Rule store, and
    // the effect's drag-gate exclusion rule set is now derived from the
    // store-side Exclude rules pulled via Rules.rulesChanged →
    // loadRuleAnimationsFromDbus. No D-Bus settings fetch needed.
    // &ok gate + two-sided clamp: a failed reply OR an older daemon's
    // valid-empty-string reply for an unknown key would otherwise toInt() to 0
    // and silently disable the min-size gate the protective member defaults
    // exist to keep active across the startup race, and an out-of-spec large
    // reply would silently reject every window from eligibility (same
    // hardening as the animation/decoration min-size loaders below).
    loadSettingAsync(QStringLiteral("minimumWindowWidth"), [this](const QVariant& v) {
        bool ok = false;
        const int i = v.toInt(&ok);
        if (ok) {
            m_cachedMinWindowWidth = qBound(0, i, 2000);
        }
    });
    loadSettingAsync(QStringLiteral("minimumWindowHeight"), [this](const QVariant& v) {
        bool ok = false;
        const int i = v.toInt(&ok);
        if (ok) {
            m_cachedMinWindowHeight = qBound(0, i, 2000);
        }
    });
    // System colours for window-border rules: the zone highlight / inactive
    // colours track the Plasma colour scheme (when "use system colours" is on the
    // daemon keeps them in sync), and they are what a border-colour `accent`
    // sentinel resolves to in updateWindowDecoration — highlight for the focused
    // (active) slot, inactive for the unfocused (inactive) slot, mirroring the
    // distinct active/inactive system border colours the per-mode appearance
    // settings used before they folded into rules. Both are re-fetched on every
    // settingsChanged, so an accent / colour-scheme change repaints accent-
    // following borders without a relog.
    loadSettingAsync(QStringLiteral("highlightColor"), [this](const QVariant& v) {
        const QColor c(v.toString());
        if (m_borderAccentColor != c) {
            m_borderAccentColor = c;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("inactiveColor"), [this](const QVariant& v) {
        const QColor c(v.toString());
        if (m_borderInactiveColor != c) {
            m_borderInactiveColor = c;
            scheduleBorderSweep();
        }
    });
    // Config-backed window-decoration appearance default. Each key updates one
    // slot of m_windowAppearanceDefault; a change re-sweeps every window so the
    // default border / hidden title bar reapplies live (mirroring the accent /
    // inactive colour loaders above). Re-fetched on every settingsChanged, so a
    // Window Appearance page edit takes effect without a relog. Guarded on an
    // actual value change to avoid a redundant full border rebuild per fetch.
    // Decorations.Performance. An animated pack repaints every window carrying it
    // on every vsync, which holds the GPU in its top performance state whatever
    // the per-frame cost is, so these gate WHEN the chain animates. Flipping
    // either one has to wake the paused windows back up, or a window frozen under
    // the old setting would stay frozen until it happened to damage.
    //
    // Both check the variant TYPE before reading it, and that guard still earns its
    // keep even though an unknown key now answers with a D-Bus ERROR (which skips the
    // callback entirely, leaving our own default in place). What it defends against is
    // a reply that ARRIVES but is not a bool: an older daemon on the other end of the
    // bus, a mid-restart half-answer, a getter returning the invalid-variant fallback.
    // QVariant("").toBool() is false, so an unguarded read there would force these off,
    // which is merely redundant for a default-false setting but INVERTS the
    // default-true PauseWhenIdle. Same guard the audio loaders below use.
    loadSettingAsync(QStringLiteral("decorationAnimateFocusedOnly"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        const bool b = v.toBool();
        if (m_animateFocusedOnly != b) {
            m_animateFocusedOnly = b;
            repaintAllDecorations();
        }
    });
    loadSettingAsync(QStringLiteral("decorationPauseWhenIdle"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        const bool b = v.toBool();
        if (m_pauseAnimationWhenIdle != b) {
            m_pauseAnimationWhenIdle = b;
            if (!b) {
                // Drop any stale idle latch. The daemon publishes false on this very change
                // (the value really moves, so its change-check passes), so this is belt and
                // braces for a daemon that is dead or restarting — in which case nothing
                // else would ever clear the latch and every decorated window would stay
                // frozen. It does NOT force-publish here, and it does not clear the ladder:
                // the ladder stays armed by design.
                //
                // Only ever written in the OFF direction. Writing our own mirror of the
                // daemon's state in the ON direction would be guessing: it is the daemon
                // that knows whether the seat is idle, and it tells us.
                m_sessionIdle = false;
            }
            repaintAllDecorations();
        }
    });

    loadSettingAsync(QStringLiteral("showWindowBorder"), [this](const QVariant& v) {
        const bool b = v.toBool();
        if (m_windowAppearanceDefault.showBorder != b) {
            m_windowAppearanceDefault.showBorder = b;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowBorderScope"), [this](const QVariant& v) {
        const QString s = v.toString();
        // Reject the empty string (an older daemon answers unknown keys with a
        // valid empty reply): scopes carry a seeded non-empty default, and an
        // empty scope silently contributes nothing to the appearance match.
        if (!s.isEmpty() && m_windowAppearanceDefault.borderScope != s) {
            m_windowAppearanceDefault.borderScope = s;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowBorderWidth"), [this](const QVariant& v) {
        // Clamp at the D-Bus boundary like every sibling int loader — the
        // daemon is a separate process and must not be trusted with the range.
        // DecorationDefaults is the SSOT the daemon's own schema clamps from.
        // The &ok gate keeps an older daemon's valid-empty reply for an
        // unknown key from coercing to 0 and clobbering the seeded default.
        bool ok = false;
        const int raw = v.toInt(&ok);
        if (!ok) {
            return;
        }
        namespace DD = PhosphorCompositor::DecorationDefaults;
        const int i = qBound(DD::BorderWidthMin, raw, DD::BorderWidthMax);
        if (m_windowAppearanceDefault.borderWidth != i) {
            m_windowAppearanceDefault.borderWidth = i;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowBorderRadius"), [this](const QVariant& v) {
        // Same boundary clamp and &ok gate as windowBorderWidth above.
        bool ok = false;
        const int raw = v.toInt(&ok);
        if (!ok) {
            return;
        }
        namespace DD = PhosphorCompositor::DecorationDefaults;
        const int i = qBound(DD::BorderRadiusMin, raw, DD::BorderRadiusMax);
        if (m_windowAppearanceDefault.borderRadius != i) {
            m_windowAppearanceDefault.borderRadius = i;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowBorderColorActive"), [this](const QVariant& v) {
        const QString s = v.toString();
        if (m_windowAppearanceDefault.activeColor != s) {
            m_windowAppearanceDefault.activeColor = s;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowBorderColorInactive"), [this](const QVariant& v) {
        const QString s = v.toString();
        if (m_windowAppearanceDefault.inactiveColor != s) {
            m_windowAppearanceDefault.inactiveColor = s;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("hideWindowTitleBars"), [this](const QVariant& v) {
        const bool b = v.toBool();
        if (m_windowAppearanceDefault.hideTitleBar != b) {
            m_windowAppearanceDefault.hideTitleBar = b;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowTitleBarScope"), [this](const QVariant& v) {
        const QString s = v.toString();
        // Empty-reply guard — see windowBorderScope above.
        if (!s.isEmpty() && m_windowAppearanceDefault.titleBarScope != s) {
            m_windowAppearanceDefault.titleBarScope = s;
            scheduleBorderSweep();
        }
    });
    // Plain opacity+tint layer (the border's opacity analogue) — same
    // change-detect + sweep pattern as the border keys above.
    loadSettingAsync(QStringLiteral("showWindowOpacityTint"), [this](const QVariant& v) {
        const bool b = v.toBool();
        if (m_windowAppearanceDefault.showOpacityTint != b) {
            m_windowAppearanceDefault.showOpacityTint = b;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowOpacityTintScope"), [this](const QVariant& v) {
        const QString s = v.toString();
        // Empty-reply guard — see windowBorderScope above.
        if (!s.isEmpty() && m_windowAppearanceDefault.opacityTintScope != s) {
            m_windowAppearanceDefault.opacityTintScope = s;
            scheduleBorderSweep();
        }
    });
    // Both unit-range values are clamped at this D-Bus boundary: the settings
    // side validates its own writes, but the daemon is a separate process and
    // this effect must not trust the wire (a hand-edited config or an older
    // daemon can answer out of range). The ok-gate mirrors focusFadeDuration
    // below: an older daemon answers an UNKNOWN key with a valid empty-string
    // variant, and an unguarded toDouble() would coerce that to 0.0 — for
    // opacity, a fully INVISIBLE window on version skew. Keeping the seeded
    // default is the safe fallback.
    loadSettingAsync(QStringLiteral("windowOpacity"), [this](const QVariant& v) {
        bool ok = false;
        const double d = qBound(0.0, v.toDouble(&ok), 1.0);
        if (ok && !qFuzzyCompare(m_windowAppearanceDefault.opacity + 1.0, d + 1.0)) {
            m_windowAppearanceDefault.opacity = d;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowTintStrength"), [this](const QVariant& v) {
        bool ok = false;
        const double d = qBound(0.0, v.toDouble(&ok), 1.0);
        if (ok && !qFuzzyCompare(m_windowAppearanceDefault.tintStrength + 1.0, d + 1.0)) {
            m_windowAppearanceDefault.tintStrength = d;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("windowTintColor"), [this](const QVariant& v) {
        const QString s = v.toString();
        if (m_windowAppearanceDefault.tintColor != s) {
            m_windowAppearanceDefault.tintColor = s;
            scheduleBorderSweep();
        }
    });
    // Decoration focus cross-fade (uSurfaceFocused ramp). A standalone
    // decoration setting, deliberately independent of animationsEnabled /
    // animationDuration / the window.focus motion node: the fade is a
    // decoration cross-fade, not a window animation. 0 = instant switch.
    // No repaint needed on change — an idle window picks the new duration up
    // on its next focus change, and an in-flight ramp re-times to it on its
    // next frame (the step divisor reads the live value).
    // Reject a non-numeric reply instead of coercing it: getSetting answers an
    // UNKNOWN key with a valid empty-string variant (an older daemon without
    // this key), and toInt() would silently turn that into 0 — forcing instant
    // mode on version skew. Keeping the seeded default is the safe fallback.
    loadSettingAsync(QStringLiteral("focusFadeDuration"), [this](const QVariant& v) {
        bool ok = false;
        const int ms = v.toInt(&ok);
        if (ok) {
            m_focusFadeDurationMs = qBound(PhosphorCompositor::DecorationDefaults::FocusFadeMsMin, ms,
                                           PhosphorCompositor::DecorationDefaults::FocusFadeMsMax);
        }
    });
    loadSettingAsync(QStringLiteral("snapAssistEnabled"), [this](const QVariant& v) {
        m_snapAssistHandler->setEnabled(v.toBool());
    });
    // Audio-reactive surface decorations and animation packs: the same daemon
    // audio-viz toggle + parameter set that drive the daemon's overlay audio
    // also drive the effect's own cava instance (syncEffectAudioState ANDs the
    // toggle with an audio decoration or an audio animation pack being
    // present). scheduleEffectAudioSync (deferred + coalesced) so the burst of
    // independent async replies collapses to ONE sync — otherwise an early
    // enable-reply could start cava on defaults and each later parameter
    // reply would immediately restart it.
    loadSettingAsync(QStringLiteral("enableAudioVisualizer"), [this](const QVariant& v) {
        m_enableAudioVisualizer = v.toBool();
        scheduleEffectAudioSync();
    });
    // The full CAVA parameter set (Shaders.Audio), mirrored into
    // m_audioOptions field by field. getSetting answers an UNKNOWN key with a
    // valid empty-string variant (an older daemon without the key), so each
    // loader type-checks the reply instead of coercing it — a zero/false/
    // empty coercion would clobber the seeded default (the focusFadeDuration
    // loader above documents the same trap). Range clamping is the provider's
    // job (setOptions normalizes against the same PhosphorAudio::Defaults
    // bounds the daemon schema uses).
    const auto loadAudioInt = [this](const QString& name, int PhosphorAudio::SpectrumOptions::* field) {
        loadSettingAsync(name, [this, field](const QVariant& v) {
            bool ok = false;
            const int value = v.toInt(&ok);
            if (ok) {
                m_audioOptions.*field = value;
            }
            scheduleEffectAudioSync();
        });
    };
    const auto loadAudioBool = [this](const QString& name, bool PhosphorAudio::SpectrumOptions::* field) {
        loadSettingAsync(name, [this, field](const QVariant& v) {
            if (v.typeId() == QMetaType::Bool) {
                m_audioOptions.*field = v.toBool();
            }
            scheduleEffectAudioSync();
        });
    };
    loadAudioInt(QStringLiteral("audioSpectrumBarCount"), &PhosphorAudio::SpectrumOptions::barCount);
    loadAudioInt(QStringLiteral("shaderFrameRate"), &PhosphorAudio::SpectrumOptions::framerate);
    loadAudioInt(QStringLiteral("audioSensitivity"), &PhosphorAudio::SpectrumOptions::sensitivity);
    loadAudioInt(QStringLiteral("audioNoiseReduction"), &PhosphorAudio::SpectrumOptions::noiseReduction);
    loadAudioInt(QStringLiteral("audioLowerCutoffHz"), &PhosphorAudio::SpectrumOptions::lowerCutoffHz);
    loadAudioInt(QStringLiteral("audioHigherCutoffHz"), &PhosphorAudio::SpectrumOptions::higherCutoffHz);
    loadAudioBool(QStringLiteral("audioAutosens"), &PhosphorAudio::SpectrumOptions::autosens);
    loadAudioBool(QStringLiteral("audioMonstercat"), &PhosphorAudio::SpectrumOptions::monstercat);
    loadAudioBool(QStringLiteral("audioWaves"), &PhosphorAudio::SpectrumOptions::waves);
    loadAudioBool(QStringLiteral("audioReverse"), &PhosphorAudio::SpectrumOptions::reverse);
    // The string-backed fields reject an empty reply: legitimate values are
    // never empty (the schema normalizes them to non-empty canonical forms),
    // so empty means unknown-key skew and the seeded default stands.
    loadSettingAsync(QStringLiteral("audioChannelMode"), [this](const QVariant& v) {
        const QString mode = v.toString();
        if (!mode.isEmpty()) {
            m_audioOptions.channelMode = PhosphorAudio::channelModeFromString(mode);
        }
        scheduleEffectAudioSync();
    });
    loadSettingAsync(QStringLiteral("audioExtraSmoothing"), [this](const QVariant& v) {
        bool ok = false;
        const int percent = v.toInt(&ok);
        if (ok) {
            m_audioOptions.extraSmoothing = PhosphorAudio::extraSmoothingFromPercent(percent);
        }
        scheduleEffectAudioSync();
    });
    loadSettingAsync(QStringLiteral("audioInputMethod"), [this](const QVariant& v) {
        const QString method = v.toString();
        if (!method.isEmpty()) {
            m_audioOptions.inputMethod = PhosphorAudio::inputMethodFromSetting(method);
        }
        scheduleEffectAudioSync();
    });
    loadSettingAsync(QStringLiteral("audioInputSource"), [this](const QVariant& v) {
        const QString source = v.toString();
        if (!source.isEmpty()) {
            m_audioOptions.inputSource = source;
        }
        scheduleEffectAudioSync();
    });
    loadSettingAsync(QStringLiteral("animationsEnabled"), [this](const QVariant& v) {
        // Type-guard before reading, for exactly the reason the decoration
        // loaders above spell out: a reply that ARRIVES but is not a bool (an
        // older daemon, a mid-restart half-answer, a getter's invalid-variant
        // fallback) coerces through toBool() to false, and m_enabled defaults
        // to TRUE (windowanimator.h) — so an unguarded read INVERTS the default
        // and silently disables every animation. It would drag the suppression
        // sync below with it too, reloading KWin's stock minimize / maximize /
        // show-desktop effects as a side effect of a malformed reply.
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        m_windowAnimator->setEnabled(v.toBool());
        // The animations master toggle is part of the suppression predicate
        // for every group: with animations off none of our packs run, so
        // KWin's own minimize / maximize / show-desktop effects must come
        // back rather than leave the user with no animation at all.
        syncStockEffectSuppression();
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
    // the animation cascade; rules whose match expression resolves for
    // the window override the filter at the resolver layer so a targeted
    // rule can re-enable animation for an otherwise-excluded app.
    // Type-guard like the decoration bool loaders above. This member happens to
    // init `false`, which is also what toBool() yields for a non-bool reply —
    // but that agreement is a coincidence of the default's polarity that
    // nothing records, and flipping the default later would silently turn a
    // bad reply into the wrong filter. isValid() is NOT enough: a valid
    // empty-string reply (an older daemon answering an unknown key) passes it
    // and then reads as false.
    loadSettingAsync(QStringLiteral("animationExcludeTransientWindows"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        m_animationExcludeTransientWindows = v.toBool();
    });
    // Default true (exclude). Type-guard, not isValid(): an error reply never
    // reaches this callback at all (ClientHelpers gates on reply.isValid() and
    // leaves our default in place), so what has to be defended against is a
    // reply that ARRIVES and is not a bool — an older daemon's valid
    // empty-string answer for an unknown key. That passes isValid() and
    // toBool()s to false, INVERTING this default-true setting and animating
    // notifications/OSDs until the next successful load.
    loadSettingAsync(QStringLiteral("animationExcludeNotificationsAndOsd"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        m_animationExcludeNotificationsAndOsd = v.toBool();
    });
    // Clamp on the effect side as defence-in-depth — the daemon's schema
    // validator already bounds these to [0, 2000], but the daemon is a separate
    // process and this effect must not trust the wire. The &ok gate + two-sided
    // clamp is the same hardening the snapping/decoration min-size loaders
    // apply: a non-int variant (an older daemon's valid-empty reply for an
    // unknown key) is rejected outright rather than coerced to 0, and a
    // negative or absurd value from an out-of-spec callsite is clamped, so the
    // min-size gate cannot be silently disabled.
    loadSettingAsync(QStringLiteral("animationMinimumWindowWidth"), [this](const QVariant& v) {
        bool ok = false;
        const int i = v.toInt(&ok);
        if (ok) {
            m_animationMinWindowWidth = qBound(0, i, 2000);
        }
    });
    loadSettingAsync(QStringLiteral("animationMinimumWindowHeight"), [this](const QVariant& v) {
        bool ok = false;
        const int i = v.toInt(&ok);
        if (ok) {
            m_animationMinWindowHeight = qBound(0, i, 2000);
        }
    });

    // Decoration window filtering — independent of the snapping/tiling and
    // animation filters cached above. Used by `shouldDecorateWindow()` to gate
    // the border / decoration pass. Re-fetched on every settingsChanged, and a
    // value change schedules a full border sweep so a Decorations page edit
    // adds/removes borders on open windows live (mirroring the appearance
    // loaders above) — unlike the animation filter, decorations are persistent
    // state and won't self-correct on the next window event.
    // Default true (exclude transients). Type-guard, not isValid(), for the
    // reason spelled out on the decoration bool loaders above: an error reply
    // never reaches this callback, so the hazard is a reply that ARRIVES and is
    // not a bool (an older daemon's valid empty-string answer), which passes
    // isValid() and toBool()s to false — inverting this default-true setting
    // and drawing borders onto dialogs/popups until the next successful load.
    loadSettingAsync(QStringLiteral("decorationExcludeTransientWindows"), [this](const QVariant& v) {
        if (v.typeId() != QMetaType::Bool) {
            return;
        }
        const bool b = v.toBool();
        if (m_decorationExcludeTransientWindows != b) {
            m_decorationExcludeTransientWindows = b;
            scheduleBorderSweep();
        }
    });
    // Clamp on the effect side as defence-in-depth, symmetric with the
    // animation min-size fetches above — the daemon schema already bounds
    // these to [0, 2000].
    loadSettingAsync(QStringLiteral("decorationMinimumWindowWidth"), [this](const QVariant& v) {
        bool ok = false;
        const int raw = v.toInt(&ok);
        if (!ok) {
            return;
        }
        const int i = qBound(0, raw, 2000);
        if (m_decorationMinWindowWidth != i) {
            m_decorationMinWindowWidth = i;
            scheduleBorderSweep();
        }
    });
    loadSettingAsync(QStringLiteral("decorationMinimumWindowHeight"), [this](const QVariant& v) {
        bool ok = false;
        const int raw = v.toInt(&ok);
        if (!ok) {
            return;
        }
        const int i = qBound(0, raw, 2000);
        if (m_decorationMinWindowHeight != i) {
            m_decorationMinWindowHeight = i;
            scheduleBorderSweep();
        }
    });
    // animationExcludedApplications / animationExcludedWindowClasses are
    // GONE — the v4 migration folded those lists into the unified
    // Rule store as `ExcludeAnimations`-action rules, and
    // loadRuleAnimationsFromDbus's parse step rebuilds the effect's
    // m_animationExclusionRuleSet from the same rule-set push that drives
    // the OverrideAnimation* pipeline. No D-Bus settings fetch needed.

    loadShaderProfileFromDbus();
    loadMotionProfileTreeFromDbus();
    loadShaderRegistryFromDbus();
    // Unified Rule store — pull in any rules carrying an
    // OverrideAnimation* action. The subscription below refreshes whenever
    // the daemon broadcasts `rulesChanged`, so an edit in the settings UI
    // lands without restarting the effect.
    loadRuleAnimationsFromDbus();
    // Subscription to the daemon's rulesChanged broadcast is installed once from
    // continueDaemonReadySetup() — installing it here would re-subscribe on every
    // slotSettingsChanged callback (QDBusConnection::connect silently accepts
    // duplicates, so the connection set would grow unbounded over the effect's
    // lifetime).
    loadSettingAsync(QStringLiteral("toggleActivation"), [this](const QVariant& v) {
        m_cachedToggleActivation = v.toBool();
    });
    loadSettingAsync(QStringLiteral("autotileDragInsertToggle"), [this](const QVariant& v) {
        m_cachedAutotileDragInsertToggle = v.toBool();
    });
    loadSettingAsync(QStringLiteral("zoneSpanToggleMode"), [this](const QVariant& v) {
        m_cachedZoneSpanToggleMode = v.toBool();
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

    // Window border / title-bar appearance is pushed as unified config defaults
    // (the window-appearance loaders above). Each slot is resolved as that config
    // default, scope-gated, with per-window rule overrides layered on top inside
    // updateWindowDecoration / reconcileRuleHiddenTitleBar (resolveEffectiveWindowAppearance).

    // Per-surface decoration profile tree (Stage 2a): the SSOT for each surface's
    // USER shader-pack chain (e.g. glow) plus each pack's parameter overrides,
    // keyed by surface path (window.tiled / window.snapped / window.floating).
    // The appearance-owned "border" base pack is prepended by updateWindowDecoration
    // from the config/rule resolution above; this tree contributes the packs the
    // user chained on top. The autotile/snap BorderState is still maintained (it
    // drives MEMBERSHIP — which windows are tiled/snapped — and the daemon's
    // retile insets), but does not feed appearance.
    //
    // On change: drop every compiled pack (a chain edit may reference a new pack,
    // and per-pack param VALUES are baked at compile time so they must recompile)
    // and rebuild all borders against the new tree, then repaint.
    loadSettingAsync(PhosphorProtocol::Service::SettingProperty::DecorationProfileTree, [this](const QVariant& v) {
        const QJsonDocument doc = QJsonDocument::fromJson(v.toString().toUtf8());
        if (!doc.isObject()) {
            qCWarning(lcEffect) << "decorationProfileTreeJson is not a JSON object — keeping current tree";
            return;
        }
        PhosphorSurfaceShaders::DecorationProfileTree tree =
            PhosphorSurfaceShaders::DecorationProfileTree::fromJson(doc.object());
        if (tree == m_decorationTree) {
            return;
        }
        m_decorationTree = std::move(tree);
        // Per-pack param values are baked at first compile, so a tree change that
        // alters parameters[packId] requires a recompile of that pack — clear the
        // whole compiled-pack cache (it lazily recompiles on the next paint).
        // This D-Bus reply lands between frames where the compositor GL context is not
        // guaranteed current, and the cached packs own GLShaders plus user GLTextures
        // whose destruction issues glDelete* — make the context current first, same
        // discipline as the effectsChanged clears in lifecycle.cpp.
        //
        // The result is CAPTURED, not discarded. The only false case is compositor
        // teardown, where GL is going away and the driver reclaims the objects whatever
        // we do, so the clear is safe either way — but a guard whose answer is thrown
        // away is not a guard, and the comment above used to claim a discipline the code
        // did not implement.
        const bool haveContext = KWin::effects && KWin::effects->makeOpenGLContextCurrent();
        if (!haveContext) {
            qCWarning(lcEffect) << "Decoration pack cache cleared without a current GL context (compositor teardown?)";
        }
        m_compiledPacks.clear();
        m_anyCompiledPackReadsCursor = false; // re-derived as packs recompile
        // Recompiling the packs invalidates every CACHED FOLD, and updateAllDecorations
        // is not a sufficient net for that: it skips windows with a live shader
        // transition, and only re-resolves windows on the current desktop — so a
        // decorated window that is both would keep compositeValid/prefixValid set and
        // its next fold would early-return a composite baked with the OLD shader.
        // Invalidate the folds directly. The textures stay (they are keyed on size and
        // chain, neither of which a recompile changes) and so does the capture, which
        // is window content and has nothing to do with the pack source.
        for (auto& [id, surfaceState] : m_surfaceMultipass) {
            surfaceState.compositeValid = false;
            surfaceState.prefixValid = false;
            surfaceState.prefixChainEnd = -1;
        }
        m_opacityTintFallbackWarned = false; // re-arm the capture-fallback warning with the fresh compiles
        updateAllDecorations();
        if (KWin::effects) {
            KWin::effects->addRepaintFull();
        }
    });

    loadSettingAsync(QStringLiteral("autotileFocusFollowsMouse"), [this](const QVariant& v) {
        m_autotileHandler->setFocusFollowsMouse(v.toBool());
    });

    loadSettingAsync(QStringLiteral("snappingFocusFollowsMouse"), [this](const QVariant& v) {
        m_snapHandler->setFocusFollowsMouse(v.toBool());
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
