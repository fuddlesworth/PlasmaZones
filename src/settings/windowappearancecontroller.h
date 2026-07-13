// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../config/configdefaults.h"

#include <PhosphorCompositor/DecorationDefaults.h>
#include <PhosphorControl/PageController.h>
#include <QObject>
#include <QString>
#include <QVariant>

namespace PlasmaZones {

class ISettings;

/// Q_PROPERTY surface for the "Appearance → Windows" settings page.
///
/// Exposed as a child Q_PROPERTY on SettingsController; QML reads
/// `settingsController.windowAppearancePage.showWindowBorder` etc. The page's
/// window border / title bar values and the shared inner/outer gap model are
/// plain config settings on ISettings (Windows.* and Gaps.*), so this controller
/// forwards each value's READ/WRITE to the matching ISettings getter/setter and
/// re-emits the ISettings::*Changed NOTIFY to QML. It also carries the CONSTANT
/// slider bounds (border width/radius, focus fade duration, the decoration idle
/// timeout, the shared gap range, and the opacity / tint-strength unit ranges)
/// sourced from the same defaults the schema clamps against so the UI range can
/// never drift.
///
/// Dirty tracking: the underlying values ARE Q_PROPERTY on Settings, so
/// SettingsController's meta-object loop already wires their NOTIFY to
/// `onSettingsPropertyChanged()`; this class holds no editable state of its own,
/// so it is never dirty and its apply/discard are no-ops. The page's per-page
/// Reset / Discard runs through the config manifest (pageOwnedConfigKeys), not
/// through this controller.
class WindowAppearanceController : public PhosphorControl::PageController
{
    Q_OBJECT

    // ── Window border / title bar (Windows.*) ─────────────────────────────────
    Q_PROPERTY(bool showWindowBorder READ showWindowBorder WRITE setShowWindowBorder NOTIFY showWindowBorderChanged)
    // Scope tokens the "Apply to" pickers speak: "tiled", "normal", "all".
    Q_PROPERTY(QString borderScope READ windowBorderScope WRITE setWindowBorderScope NOTIFY windowBorderScopeChanged)
    Q_PROPERTY(int windowBorderWidth READ windowBorderWidth WRITE setWindowBorderWidth NOTIFY windowBorderWidthChanged)
    Q_PROPERTY(
        int windowBorderRadius READ windowBorderRadius WRITE setWindowBorderRadius NOTIFY windowBorderRadiusChanged)
    // Border colours: a concrete "#AARRGGBB" hex, or the sentinel "accent" that
    // the effect resolves to the live system accent at paint time.
    Q_PROPERTY(QString windowBorderColorActive READ windowBorderColorActive WRITE setWindowBorderColorActive NOTIFY
                   windowBorderColorActiveChanged)
    Q_PROPERTY(QString windowBorderColorInactive READ windowBorderColorInactive WRITE setWindowBorderColorInactive
                   NOTIFY windowBorderColorInactiveChanged)
    Q_PROPERTY(bool hideWindowTitleBars READ hideWindowTitleBars WRITE setHideWindowTitleBars NOTIFY
                   hideWindowTitleBarsChanged)
    Q_PROPERTY(
        QString titleBarScope READ windowTitleBarScope WRITE setWindowTitleBarScope NOTIFY windowTitleBarScopeChanged)
    // Decoration focus cross-fade duration (ms); 0 switches instantly.
    Q_PROPERTY(int focusFadeDuration READ focusFadeDuration WRITE setFocusFadeDuration NOTIFY focusFadeDurationChanged)
    // ── Plain opacity+tint layer (Windows.*) ──────────────────────────────────
    // Opacity/strength are [0.0, 1.0]; the tint colour shares the border
    // colours' "#AARRGGBB" or "accent" sentinel shape.
    Q_PROPERTY(bool showWindowOpacityTint READ showWindowOpacityTint WRITE setShowWindowOpacityTint NOTIFY
                   showWindowOpacityTintChanged)
    Q_PROPERTY(QString opacityTintScope READ windowOpacityTintScope WRITE setWindowOpacityTintScope NOTIFY
                   windowOpacityTintScopeChanged)
    Q_PROPERTY(double windowOpacity READ windowOpacity WRITE setWindowOpacity NOTIFY windowOpacityChanged)
    Q_PROPERTY(
        double windowTintStrength READ windowTintStrength WRITE setWindowTintStrength NOTIFY windowTintStrengthChanged)
    Q_PROPERTY(QString windowTintColor READ windowTintColor WRITE setWindowTintColor NOTIFY windowTintColorChanged)

    // ── Shared inner/outer gap model (Gaps.*) ─────────────────────────────────
    Q_PROPERTY(int innerGap READ innerGap WRITE setInnerGap NOTIFY innerGapChanged)
    Q_PROPERTY(int outerGap READ outerGap WRITE setOuterGap NOTIFY outerGapChanged)
    Q_PROPERTY(
        bool usePerSideOuterGap READ usePerSideOuterGap WRITE setUsePerSideOuterGap NOTIFY usePerSideOuterGapChanged)
    Q_PROPERTY(int outerGapTop READ outerGapTop WRITE setOuterGapTop NOTIFY outerGapTopChanged)
    Q_PROPERTY(int outerGapBottom READ outerGapBottom WRITE setOuterGapBottom NOTIFY outerGapBottomChanged)
    Q_PROPERTY(int outerGapLeft READ outerGapLeft WRITE setOuterGapLeft NOTIFY outerGapLeftChanged)
    Q_PROPERTY(int outerGapRight READ outerGapRight WRITE setOuterGapRight NOTIFY outerGapRightChanged)

    // ── "Apply to" scope tokens (single source: PhosphorCompositor::WindowAppearanceScope) ──
    // The QML "Apply to" pickers pair each token with an i18n label; exposing the
    // tokens here keeps them in lockstep with the schema validator and the effect.
    Q_PROPERTY(QString scopeTokenTiled READ scopeTokenTiled CONSTANT)
    Q_PROPERTY(QString scopeTokenNormal READ scopeTokenNormal CONSTANT)
    Q_PROPERTY(QString scopeTokenAll READ scopeTokenAll CONSTANT)

    // ── Border colour sentinel + fallback (single source: ConfigDefaults) ──
    // The "follow the system accent" sentinel and the concrete colour the page
    // seeds when the user leaves accent mode. Exposed here so QML never hardcodes
    // either literal, mirroring the scope-token treatment above.
    Q_PROPERTY(QString accentColorToken READ accentColorToken CONSTANT)
    Q_PROPERTY(QString defaultBorderColorHex READ defaultBorderColorHex CONSTANT)

    // ── CONSTANT slider bounds ────────────────────────────────────────────────
    Q_PROPERTY(int borderWidthMin READ borderWidthMin CONSTANT)
    Q_PROPERTY(int borderWidthMax READ borderWidthMax CONSTANT)
    Q_PROPERTY(int borderRadiusMin READ borderRadiusMin CONSTANT)
    Q_PROPERTY(int borderRadiusMax READ borderRadiusMax CONSTANT)
    Q_PROPERTY(int focusFadeDurationMin READ focusFadeDurationMin CONSTANT)
    Q_PROPERTY(int focusFadeDurationMax READ focusFadeDurationMax CONSTANT)
    Q_PROPERTY(int decorationIdleTimeoutSecMin READ decorationIdleTimeoutSecMin CONSTANT)
    Q_PROPERTY(int decorationIdleTimeoutSecMax READ decorationIdleTimeoutSecMax CONSTANT)
    Q_PROPERTY(int innerGapMin READ innerGapMin CONSTANT)
    Q_PROPERTY(int innerGapMax READ innerGapMax CONSTANT)
    Q_PROPERTY(int outerGapMin READ outerGapMin CONSTANT)
    Q_PROPERTY(int outerGapMax READ outerGapMax CONSTANT)
    // Opacity / tint strength bounds on the wire [0.0, 1.0] scale; the page's
    // percent sliders scale them by 100, mirroring the value bindings.
    Q_PROPERTY(double windowOpacityMin READ windowOpacityMin CONSTANT)
    Q_PROPERTY(double windowOpacityMax READ windowOpacityMax CONSTANT)
    Q_PROPERTY(double windowTintStrengthMin READ windowTintStrengthMin CONSTANT)
    Q_PROPERTY(double windowTintStrengthMax READ windowTintStrengthMax CONSTANT)

public:
    explicit WindowAppearanceController(ISettings& settings, QObject* parent = nullptr);

    bool isDirty() const override
    {
        return false;
    }
    void apply() override
    {
    }
    void discard() override
    {
    }

    // Window border / title bar — forward to ISettings.
    bool showWindowBorder() const;
    QString windowBorderScope() const;
    int windowBorderWidth() const;
    int windowBorderRadius() const;
    QString windowBorderColorActive() const;
    QString windowBorderColorInactive() const;
    bool hideWindowTitleBars() const;
    QString windowTitleBarScope() const;
    int focusFadeDuration() const;

    void setShowWindowBorder(bool show);
    void setWindowBorderScope(const QString& scope);
    void setWindowBorderWidth(int width);
    void setWindowBorderRadius(int radius);
    void setWindowBorderColorActive(const QString& color);
    void setWindowBorderColorInactive(const QString& color);
    void setHideWindowTitleBars(bool hide);
    void setWindowTitleBarScope(const QString& scope);
    void setFocusFadeDuration(int ms);

    // Plain opacity+tint layer — forward to ISettings.
    bool showWindowOpacityTint() const;
    QString windowOpacityTintScope() const;
    double windowOpacity() const;
    double windowTintStrength() const;
    QString windowTintColor() const;
    void setShowWindowOpacityTint(bool show);
    void setWindowOpacityTintScope(const QString& scope);
    void setWindowOpacity(double opacity);
    void setWindowTintStrength(double strength);
    void setWindowTintColor(const QString& color);

    // Shared inner/outer gaps — forward to ISettings.
    int innerGap() const;
    int outerGap() const;
    bool usePerSideOuterGap() const;
    int outerGapTop() const;
    int outerGapBottom() const;
    int outerGapLeft() const;
    int outerGapRight() const;

    void setInnerGap(int gap);
    void setOuterGap(int gap);
    void setUsePerSideOuterGap(bool enabled);
    void setOuterGapTop(int gap);
    void setOuterGapBottom(int gap);
    void setOuterGapLeft(int gap);
    void setOuterGapRight(int gap);

    // Scope-aware gap read/write for the Gaps card's monitor scope chip. @p key is
    // one of the PerScreenSnappingKey gap names (InnerGap / OuterGap /
    // UsePerSideOuterGap / OuterGap{Top,Bottom,Left,Right}). When @p screenName is
    // empty the GLOBAL config value is read/written; otherwise the per-monitor
    // config override is read (falling back to the global value when the monitor
    // has no override) and written to the per-screen autotile store. The Q_PROPERTY
    // getters/setters above stay as the global-scope surface the page's dirty
    // tracking rides; these invokables are the per-monitor-aware path.
    Q_INVOKABLE QVariant gapValue(const QString& screenName, const QString& key) const;
    Q_INVOKABLE void writeGap(const QString& screenName, const QString& key, const QVariant& value);

    QString scopeTokenTiled() const
    {
        return QString(::PhosphorCompositor::WindowAppearanceScope::Tiled);
    }
    QString scopeTokenNormal() const
    {
        return QString(::PhosphorCompositor::WindowAppearanceScope::Normal);
    }
    QString scopeTokenAll() const
    {
        return QString(::PhosphorCompositor::WindowAppearanceScope::All);
    }

    QString accentColorToken() const
    {
        return ConfigDefaults::windowBorderColorActive();
    }
    QString defaultBorderColorHex() const
    {
        return ConfigDefaults::windowBorderColorAccentFallbackHex();
    }

    // CONSTANT slider bounds.
    int borderWidthMin() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderWidthMin;
    }
    int borderWidthMax() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderWidthMax;
    }
    int borderRadiusMin() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderRadiusMin;
    }
    int borderRadiusMax() const
    {
        return ::PhosphorCompositor::DecorationDefaults::BorderRadiusMax;
    }
    int focusFadeDurationMin() const
    {
        return ::PhosphorCompositor::DecorationDefaults::FocusFadeMsMin;
    }
    int focusFadeDurationMax() const
    {
        return ::PhosphorCompositor::DecorationDefaults::FocusFadeMsMax;
    }
    int decorationIdleTimeoutSecMin() const
    {
        return ConfigDefaults::decorationIdleTimeoutSecMin();
    }
    int decorationIdleTimeoutSecMax() const
    {
        return ConfigDefaults::decorationIdleTimeoutSecMax();
    }
    int innerGapMin() const
    {
        return ConfigDefaults::innerGapMin();
    }
    int innerGapMax() const
    {
        return ConfigDefaults::innerGapMax();
    }
    int outerGapMin() const
    {
        return ConfigDefaults::outerGapMin();
    }
    int outerGapMax() const
    {
        return ConfigDefaults::outerGapMax();
    }
    double windowOpacityMin() const
    {
        return ConfigDefaults::windowOpacityMin();
    }
    double windowOpacityMax() const
    {
        return ConfigDefaults::windowOpacityMax();
    }
    double windowTintStrengthMin() const
    {
        return ConfigDefaults::windowTintStrengthMin();
    }
    double windowTintStrengthMax() const
    {
        return ConfigDefaults::windowTintStrengthMax();
    }

Q_SIGNALS:
    void showWindowBorderChanged();
    void windowBorderScopeChanged();
    void windowBorderWidthChanged();
    void windowBorderRadiusChanged();
    void windowBorderColorActiveChanged();
    void windowBorderColorInactiveChanged();
    void hideWindowTitleBarsChanged();
    void windowTitleBarScopeChanged();
    void focusFadeDurationChanged();
    void showWindowOpacityTintChanged();
    void windowOpacityTintScopeChanged();
    void windowOpacityChanged();
    void windowTintStrengthChanged();
    void windowTintColorChanged();
    void innerGapChanged();
    void outerGapChanged();
    void usePerSideOuterGapChanged();
    void outerGapTopChanged();
    void outerGapBottomChanged();
    void outerGapLeftChanged();
    void outerGapRightChanged();

private:
    ISettings* m_settings = nullptr;
};

} // namespace PlasmaZones
