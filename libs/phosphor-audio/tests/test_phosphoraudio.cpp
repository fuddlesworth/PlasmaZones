// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAudio/AudioDefaults.h>
#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorAudio/IAudioSpectrumProvider.h>

#include <QTest>

using PhosphorAudio::ChannelMode;
using PhosphorAudio::SpectrumOptions;

class TestPhosphorAudio : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void testDefaults()
    {
        QCOMPARE(PhosphorAudio::Defaults::MinBars, 16);
        QCOMPARE(PhosphorAudio::Defaults::MaxBars, 256);
        QVERIFY(PhosphorAudio::Defaults::MinBars % 2 == 0);
        QVERIFY(PhosphorAudio::Defaults::MaxBars % 2 == 0);
        // The cutoff ranges must be disjoint so lower < higher holds for any
        // in-range pair (cava rejects lower_cutoff_freq >= higher_cutoff_freq).
        QVERIFY(PhosphorAudio::Defaults::MaxLowerCutoffHz < PhosphorAudio::Defaults::MinHigherCutoffHz);
    }

    void testProviderConstruction()
    {
        PhosphorAudio::CavaSpectrumProvider provider;
        QVERIFY(!provider.isRunning());
        const SpectrumOptions opts = provider.options();
        QCOMPARE(opts.barCount, PhosphorAudio::Defaults::DefaultBarCount);
        QCOMPARE(opts.framerate, PhosphorAudio::Defaults::DefaultFramerate);
        QCOMPARE(opts.autosens, PhosphorAudio::Defaults::DefaultAutosens);
        QCOMPARE(opts.sensitivity, PhosphorAudio::Defaults::DefaultSensitivity);
        QCOMPARE(opts.noiseReduction, PhosphorAudio::Defaults::DefaultNoiseReduction);
        QCOMPARE(opts.lowerCutoffHz, PhosphorAudio::Defaults::DefaultLowerCutoffHz);
        QCOMPARE(opts.higherCutoffHz, PhosphorAudio::Defaults::DefaultHigherCutoffHz);
        QCOMPARE(opts.channelMode, ChannelMode::Stereo);
        QVERIFY(!opts.monstercat);
        QVERIFY(!opts.waves);
        QVERIFY(!opts.reverse);
        QCOMPARE(opts.extraSmoothing, PhosphorAudio::Defaults::DefaultExtraSmoothing);
        QVERIFY(opts.inputMethod.isEmpty());
        QCOMPARE(opts.inputSource, QStringLiteral("auto"));
        QVERIFY(provider.spectrum().isEmpty());
    }

    void testBarCountClamping()
    {
        PhosphorAudio::CavaSpectrumProvider provider;
        SpectrumOptions opts = provider.options();

        opts.barCount = 8;
        provider.setOptions(opts);
        QCOMPARE(provider.options().barCount, PhosphorAudio::Defaults::MinBars);

        opts.barCount = 512;
        provider.setOptions(opts);
        QCOMPARE(provider.options().barCount, PhosphorAudio::Defaults::MaxBars);

        opts.barCount = 33;
        provider.setOptions(opts);
        QCOMPARE(provider.options().barCount, 34);
    }

    void testFramerateClamping()
    {
        PhosphorAudio::CavaSpectrumProvider provider;
        SpectrumOptions opts = provider.options();

        opts.framerate = 10;
        provider.setOptions(opts);
        QCOMPARE(provider.options().framerate, PhosphorAudio::Defaults::MinFramerate);

        opts.framerate = 200;
        provider.setOptions(opts);
        QCOMPARE(provider.options().framerate, PhosphorAudio::Defaults::MaxFramerate);

        opts.framerate = 90;
        provider.setOptions(opts);
        QCOMPARE(provider.options().framerate, 90);
    }

    void testAnalysisRangeClamping()
    {
        PhosphorAudio::CavaSpectrumProvider provider;
        SpectrumOptions opts = provider.options();
        opts.sensitivity = 5000;
        opts.noiseReduction = -3;
        opts.lowerCutoffHz = 1;
        opts.higherCutoffHz = 96000;
        opts.extraSmoothing = 2.0;
        provider.setOptions(opts);

        const SpectrumOptions applied = provider.options();
        QCOMPARE(applied.sensitivity, PhosphorAudio::Defaults::MaxSensitivity);
        QCOMPARE(applied.noiseReduction, PhosphorAudio::Defaults::MinNoiseReduction);
        QCOMPARE(applied.lowerCutoffHz, PhosphorAudio::Defaults::MinLowerCutoffHz);
        QCOMPARE(applied.higherCutoffHz, PhosphorAudio::Defaults::MaxHigherCutoffHz);
        QCOMPARE(applied.extraSmoothing, PhosphorAudio::Defaults::MaxExtraSmoothing);
        QVERIFY(applied.lowerCutoffHz < applied.higherCutoffHz);
    }

    void testInputStringSanitization()
    {
        SpectrumOptions opts;
        // Control characters must not survive into the generated config: a
        // newline in the source string would otherwise start a new config
        // line (e.g. redirect raw_target).
        opts.inputSource = QStringLiteral("alsa_output\n[output]\nraw_target=/tmp/evil");
        opts.inputMethod = QStringLiteral("pulse\nsource=x");
        const SpectrumOptions normalized = PhosphorAudio::CavaSpectrumProvider::normalizedOptions(opts);
        QVERIFY(!normalized.inputSource.contains(QLatin1Char('\n')));
        QVERIFY(!normalized.inputMethod.contains(QLatin1Char('\n')));

        // Unknown methods fall back to auto-detection (empty).
        SpectrumOptions bogus;
        bogus.inputMethod = QStringLiteral("nonsense");
        QVERIFY(PhosphorAudio::CavaSpectrumProvider::normalizedOptions(bogus).inputMethod.isEmpty());

        // Empty source falls back to "auto".
        SpectrumOptions blank;
        blank.inputSource = QStringLiteral("   ");
        QCOMPARE(PhosphorAudio::CavaSpectrumProvider::normalizedOptions(blank).inputSource, QStringLiteral("auto"));

        // Known methods pass through case-insensitively.
        SpectrumOptions known;
        known.inputMethod = QStringLiteral("PipeWire");
        QCOMPARE(PhosphorAudio::CavaSpectrumProvider::normalizedOptions(known).inputMethod, QStringLiteral("pipewire"));
    }

    void testChannelModeStrings()
    {
        using PhosphorAudio::channelModeFromString;
        using PhosphorAudio::channelModeToString;
        const auto modes = {ChannelMode::Stereo, ChannelMode::MonoAverage, ChannelMode::MonoLeft,
                            ChannelMode::MonoRight};
        for (ChannelMode mode : modes) {
            QCOMPARE(channelModeFromString(channelModeToString(mode)), mode);
        }
        QCOMPARE(channelModeFromString(QStringLiteral("garbage")), ChannelMode::Stereo);
    }

    void testInterfacePolymorphism()
    {
        PhosphorAudio::CavaSpectrumProvider concrete;
        PhosphorAudio::IAudioSpectrumProvider* iface = &concrete;
        QVERIFY(!iface->isRunning());
        QCOMPARE(iface->options().barCount, PhosphorAudio::Defaults::DefaultBarCount);

        SpectrumOptions opts = iface->options();
        opts.channelMode = ChannelMode::MonoAverage;
        opts.monstercat = true;
        iface->setOptions(opts);
        QCOMPARE(iface->options().channelMode, ChannelMode::MonoAverage);
        QVERIFY(iface->options().monstercat);
    }
};

QTEST_MAIN(TestPhosphorAudio)

#include "test_phosphoraudio.moc"
