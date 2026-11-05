// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <PhosphorAudio/AudioDefaults.h>
#include <PhosphorAudio/phosphoraudio_export.h>

#include <QObject>
#include <QString>
#include <QVector>

namespace PhosphorAudio {

/// How the analyzed channels map onto the emitted bar vector. Stereo emits
/// left bars low→high followed by right bars low→high (cava's raw layout);
/// the mono modes collapse to a single low→high block.
enum class ChannelMode {
    Stereo,
    MonoAverage,
    MonoLeft,
    MonoRight,
};

/// Canonical config-string forms ("stereo", "mono-average", "mono-left",
/// "mono-right") for persisting a ChannelMode. fromString falls back to
/// Stereo for unknown input. PlasmaZones itself persists the canonical
/// strings directly (its settings UI writes them), so it only consumes
/// fromString; toString is the symmetric serialization half kept for
/// library consumers that hold a ChannelMode and need the persisted form.
PHOSPHORAUDIO_EXPORT QString channelModeToString(ChannelMode mode);
PHOSPHORAUDIO_EXPORT ChannelMode channelModeFromString(const QString& mode);

/// Conversions between the persisted settings representation and the
/// SpectrumOptions field values, shared by every settings consumer so the
/// percent scale and the "auto" sentinel are spelled exactly once.
inline double extraSmoothingFromPercent(int percent)
{
    return percent / 100.0;
}
inline QString inputMethodFromSetting(const QString& configured)
{
    return configured == QLatin1String("auto") ? QString() : configured;
}

/// Full analysis parameter set for a spectrum provider. A plain value type:
/// build one (or copy the provider's current set via options()), adjust the
/// fields, and hand it back through setOptions() — the provider normalizes
/// (clamps ranges, rounds bars to even, sanitizes strings) and applies the
/// whole set as one change, so a multi-field edit costs a single restart.
struct PHOSPHORAUDIO_EXPORT SpectrumOptions
{
    int barCount = Defaults::DefaultBarCount;
    int framerate = Defaults::DefaultFramerate;
    /// Automatic gain: the analyzer self-scales so output fills 0..1.
    bool autosens = Defaults::DefaultAutosens;
    /// Percent gain (100 = unity). With autosens on this is only the
    /// starting point the auto-gain adapts from.
    int sensitivity = Defaults::DefaultSensitivity;
    /// Primary smoothing knob (cava's [smoothing] noise_reduction, 0-100):
    /// 0 = fast and twitchy, 100 = slow and smooth.
    int noiseReduction = Defaults::DefaultNoiseReduction;
    /// Analyzed frequency band. The defaults' ranges guarantee
    /// lowerCutoffHz < higherCutoffHz for any pair of in-range values.
    int lowerCutoffHz = Defaults::DefaultLowerCutoffHz;
    int higherCutoffHz = Defaults::DefaultHigherCutoffHz;
    /// Monstercat-style neighbor spread filter.
    bool monstercat = Defaults::DefaultMonstercat;
    /// Wave-style spread filter.
    bool waves = Defaults::DefaultWaves;
    ChannelMode channelMode = ChannelMode::Stereo;
    /// Reverse the frequency order of the emitted bars.
    bool reverse = Defaults::DefaultReverse;
    /// Provider-side EMA on top of noiseReduction: fraction of the previous
    /// frame retained per update (0 = off).
    double extraSmoothing = Defaults::DefaultExtraSmoothing;
    /// Capture backend override ("pipewire", "pulse", ...). Empty = detect.
    QString inputMethod;
    /// Capture device/source for the backend. "auto" = the backend default.
    QString inputSource = QStringLiteral("auto");

    bool operator==(const SpectrumOptions& other) const = default;
};

class PHOSPHORAUDIO_EXPORT IAudioSpectrumProvider : public QObject
{
    Q_OBJECT

public:
    explicit IAudioSpectrumProvider(QObject* parent = nullptr)
        : QObject(parent)
    {
    }

    ~IAudioSpectrumProvider() override = default;

    virtual bool isAvailable() const = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    /// The provider's current (normalized) parameter set.
    virtual SpectrumOptions options() const = 0;
    /// Apply a full parameter set. Implementations normalize the values,
    /// no-op when nothing changed, and restart capture at most once for
    /// however many fields differ.
    virtual void setOptions(const SpectrumOptions& options) = 0;

    virtual QVector<float> spectrum() const = 0;

Q_SIGNALS:
    void spectrumUpdated(const QVector<float>& spectrum);
    void runningChanged(bool running);
    void errorOccurred(const QString& message);
};

} // namespace PhosphorAudio
