// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "configdefaults_limits.h"

namespace PlasmaZones {

// Chain link 4: rendering-backend, audio-spectrum shader, decoration-shader, and
// decoration-performance default accessors.
class ConfigDefaultsShaders : public ConfigDefaultsLimits
{
public:
    // ═══════════════════════════════════════════════════════════════════════════
    // Rendering Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static QString renderingBackend()
    {
        return QStringLiteral("auto");
    }

    // Single source of truth for the legal backend tokens. Order here is the
    // schema's declaration order, which is the order pickers offer. The
    // user-facing words for these tokens live in settingsvaluelabels.cpp, the
    // app-side label table every enum key resolves through.
    static const QStringList& renderingBackendOptions()
    {
        static const QStringList keys = {
            QStringLiteral("auto"),
            QStringLiteral("vulkan"),
            QStringLiteral("opengl"),
        };
        return keys;
    }

    static QString normalizeRenderingBackend(const QString& raw)
    {
        const QString normalized = raw.toLower().trimmed();
        return renderingBackendOptions().contains(normalized) ? normalized : renderingBackend();
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Shader Settings
    // ═══════════════════════════════════════════════════════════════════════════

    static int shaderFrameRate()
    {
        return 60;
    }
    static constexpr int shaderFrameRateMin()
    {
        return 30;
    }
    static constexpr int shaderFrameRateMax()
    {
        return 144;
    }
    static bool enableAudioVisualizer()
    {
        return false;
    }
    static int audioSpectrumBarCount()
    {
        return 64;
    }
    static constexpr int audioSpectrumBarCountMin()
    {
        return 16;
    }
    static constexpr int audioSpectrumBarCountMax()
    {
        return 256;
    }

    // Audio spectrum analysis parameters (Shaders.Audio group). Values and
    // ranges mirror PhosphorAudio::Defaults — the provider re-clamps to the
    // same bounds, so schema and library agree on the canonical range.
    static bool audioAutosens()
    {
        return true;
    }
    static int audioSensitivity()
    {
        return 100;
    }
    static constexpr int audioSensitivityMin()
    {
        return 10;
    }
    static constexpr int audioSensitivityMax()
    {
        return 500;
    }
    static int audioNoiseReduction()
    {
        return 77;
    }
    static constexpr int audioNoiseReductionMin()
    {
        return 0;
    }
    static constexpr int audioNoiseReductionMax()
    {
        return 100;
    }
    static int audioLowerCutoffHz()
    {
        return 50;
    }
    static constexpr int audioLowerCutoffHzMin()
    {
        return 20;
    }
    static constexpr int audioLowerCutoffHzMax()
    {
        return 500;
    }
    static int audioHigherCutoffHz()
    {
        return 10000;
    }
    static constexpr int audioHigherCutoffHzMin()
    {
        return 1000;
    }
    static constexpr int audioHigherCutoffHzMax()
    {
        return 20000;
    }
    static bool audioMonstercat()
    {
        return false;
    }
    static bool audioWaves()
    {
        return false;
    }
    static bool audioReverse()
    {
        return false;
    }
    // Provider-side extra smoothing on top of noise reduction, stored as a
    // percent (the provider consumes it as a 0-0.95 fraction).
    static int audioExtraSmoothing()
    {
        return 50;
    }
    static constexpr int audioExtraSmoothingMin()
    {
        return 0;
    }
    static constexpr int audioExtraSmoothingMax()
    {
        return 95;
    }

    static QString audioChannelMode()
    {
        return QStringLiteral("stereo");
    }
    static const QStringList& audioChannelModeOptions()
    {
        static const QStringList opts = {QStringLiteral("stereo"), QStringLiteral("mono-average"),
                                         QStringLiteral("mono-left"), QStringLiteral("mono-right")};
        return opts;
    }
    static QString normalizeAudioChannelMode(const QString& raw)
    {
        const QString normalized = raw.toLower().trimmed();
        return audioChannelModeOptions().contains(normalized) ? normalized : audioChannelMode();
    }

    static QString audioInputMethod()
    {
        return QStringLiteral("auto");
    }
    static const QStringList& audioInputMethodOptions()
    {
        // "auto" = provider detection (pipewire/pulse probe). The rest mirror
        // the capture backends upstream cava can be built with; the settings
        // UI offers auto/pipewire/pulse and the remainder stay valid for
        // hand-edited configs.
        static const QStringList opts = {QStringLiteral("auto"), QStringLiteral("pipewire"),  QStringLiteral("pulse"),
                                         QStringLiteral("alsa"), QStringLiteral("jack"),      QStringLiteral("sndio"),
                                         QStringLiteral("oss"),  QStringLiteral("portaudio"), QStringLiteral("fifo"),
                                         QStringLiteral("shmem")};
        return opts;
    }
    static QString normalizeAudioInputMethod(const QString& raw)
    {
        const QString normalized = raw.toLower().trimmed();
        return audioInputMethodOptions().contains(normalized) ? normalized : audioInputMethod();
    }

    static QString audioInputSource()
    {
        return QStringLiteral("auto");
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Decoration shader Settings
    //
    // The group/key accessors (decorationsGroup / decorationProfileTreeKey) are
    // inherited from ConfigKeys — no forwarding accessors needed here (same as
    // every other group: ConfigDefaults derives from ConfigKeys).
    // ═══════════════════════════════════════════════════════════════════════════

    /// Built-in DecorationProfileTree seed layer. NOT persisted and NOT the
    /// schema default (that is the empty tree — the stored blob holds only
    /// user edits): `Settings::decorationProfileTree()` overlays this tree at
    /// LOWEST precedence on every read via
    /// `DecorationProfileTree::withSeedDefaults`, the same seed model as the
    /// animation motion defaults (PhosphorProfileRegistry's low-precedence
    /// owner tag). A user edit at a seeded path becomes a real override and
    /// wins; clearing that override (per-page Reset) reveals the seed again;
    /// an engaged-but-empty chain keeps the surface explicitly undecorated.
    ///
    /// The decoration tree is the user-applied surface-shader pack stack. Window
    /// border and title-bar appearance are owned by the window rules, not by this
    /// tree, so windows and popups start undecorated until the user engages a
    /// pack (e.g. glow) from the Decoration pages.
    ///
    /// The PopupFrame-based card surfaces are the exception: the OSD ("osd") and
    /// the three PopupFrame popups ("popup.layoutPicker", "popup.zoneSelector",
    /// "popup.cheatsheet") ship a default chain that chromes their cards through the
    /// surface-decoration pipeline rather than PopupFrame's built-in MultiEffect
    /// — a crisp neutral frame-contrast border plus a real, theme-tinted drop
    /// shadow. (Snap-assist is left undecorated: it carries its own anchor, not
    /// PopupFrame, so it has no equivalent card chrome to replace.) These flow
    /// through the same merged tree the Decoration pages edit, so a user can
    /// retune them (a parameters-only retune keeps the seed chain) or clear
    /// them (remove every pack, which persists an explicit empty chain). The border
    /// colour resolves from the theme in OverlayService::applyDecoration
    /// (useThemeNeutral, at frameContrast 0.2) so it tracks light and dark;
    /// edgeSoftness 0.5 keeps the 1px border a crisp hairline. The shadow pack is
    /// a proper drop shadow (offset, edge-feathered so it never cuts off in a
    /// hard rectangle), tinted with the theme background (useThemeTint,
    /// PopupFrame's original glow colour) so the halo tracks light and dark too.
    static ::PhosphorSurfaceShaders::DecorationProfileTree decorationProfileTree()
    {
        // One shared card decoration for every PopupFrame surface — the OSD and
        // the three popups read as the same surface family. PopupFrame squares its
        // body off when decorated (radius 0), so the packs are the sole owners
        // of the corner rounding. The corner radius is NOT set here: each popup
        // slot publishes its own card radius (cardCornerRadius) and
        // OverlayService::applyDecoration injects it into every pack that
        // declares cornerRadius, so the corners follow the card and border and
        // shadow coincide.
        const auto cardDecoration = []() -> ::PhosphorSurfaceShaders::DecorationProfile {
            ::PhosphorSurfaceShaders::DecorationProfile card;
            card.chain = QStringList{QStringLiteral("border"), QStringLiteral("shadow")};
            QVariantMap borderParams;
            borderParams.insert(QStringLiteral("borderWidth"), 1);
            borderParams.insert(QStringLiteral("useSystemAccent"), false);
            borderParams.insert(QStringLiteral("useThemeNeutral"), true);
            borderParams.insert(QStringLiteral("edgeSoftness"), 0.5);
            QVariantMap shadowParams;
            shadowParams.insert(QStringLiteral("shadowSize"), 28);
            shadowParams.insert(QStringLiteral("shadowStrength"), 0.6);
            shadowParams.insert(QStringLiteral("offsetY"), 6);
            shadowParams.insert(QStringLiteral("useThemeTint"), true);
            QVariantMap params;
            params.insert(QStringLiteral("border"), borderParams);
            params.insert(QStringLiteral("shadow"), shadowParams);
            card.parameters = params;
            return card;
        };

        ::PhosphorSurfaceShaders::DecorationProfileTree tree;
        const ::PhosphorSurfaceShaders::DecorationProfile card = cardDecoration();
        tree.setOverride(QStringLiteral("osd"), card);
        tree.setOverride(QStringLiteral("popup.layoutPicker"), card);
        tree.setOverride(QStringLiteral("popup.zoneSelector"), card);
        tree.setOverride(QStringLiteral("popup.cheatsheet"), card);
        return tree;
    }

    // ═══════════════════════════════════════════════════════════════════════════
    // Decorations.Performance
    //
    // An animated pack repaints every window carrying it on every vsync. That
    // never lets the GPU leave its top performance state — measured at ~110 W and
    // +12 C over an idle desktop with the effect unloaded, on a GPU that is only
    // ~45% busy. What costs is not the work per frame but that there IS work every
    // frame, so these bound WHEN the chain animates, not how much it does.
    // ═══════════════════════════════════════════════════════════════════════════

    /// Animate only the focused window's decoration; unfocused windows hold their
    /// last composite. Divides the continuous redraw by the number of decorated
    /// windows on screen. Off by default: it visibly changes what the desktop
    /// looks like (only the window you are using shimmers), so it is the user's
    /// call, not ours.
    static bool decorationAnimateFocusedOnly()
    {
        return false;
    }

    /// Stop animating the decoration chain once the session has been idle for
    /// decorationIdleTimeoutSec, and resume on the first input. On by default:
    /// nobody is looking at an animation they walked away from, and this is where
    /// most of the wasted power actually goes.
    static bool decorationPauseWhenIdle()
    {
        return true;
    }

    /// Seconds of no input before decoration animation pauses.
    static constexpr int decorationIdleTimeoutSec()
    {
        return 30;
    }
    static constexpr int decorationIdleTimeoutSecMin()
    {
        return 5;
    }
    static constexpr int decorationIdleTimeoutSecMax()
    {
        return 3600;
    }
};

} // namespace PlasmaZones
