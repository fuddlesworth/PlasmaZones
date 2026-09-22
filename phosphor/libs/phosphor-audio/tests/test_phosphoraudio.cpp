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

    void testGeneratedConfigDefaults()
    {
        SpectrumOptions opts;
        // Pin the method so the config is environment-independent (empty
        // would run the pipewire/pulse autodetect probe).
        opts.inputMethod = QStringLiteral("pulse");
        const QString config = PhosphorAudio::CavaSpectrumProvider::generateConfig(opts);
        QVERIFY(config.contains(QStringLiteral("framerate=60\n")));
        QVERIFY(config.contains(QStringLiteral("bars=64\n")));
        QVERIFY(config.contains(QStringLiteral("autosens=1\n")));
        QVERIFY(config.contains(QStringLiteral("sensitivity=100\n")));
        QVERIFY(config.contains(QStringLiteral("lower_cutoff_freq=50\n")));
        QVERIFY(config.contains(QStringLiteral("higher_cutoff_freq=10000\n")));
        QVERIFY(config.contains(QStringLiteral("method=pulse\n")));
        QVERIFY(config.contains(QStringLiteral("source=auto\n")));
        QVERIFY(config.contains(QStringLiteral("channels=stereo\n")));
        QVERIFY(config.contains(QStringLiteral("reverse=0\n")));
        QVERIFY(config.contains(QStringLiteral("noise_reduction=77\n")));
        QVERIFY(config.contains(QStringLiteral("monstercat=0\n")));
        QVERIFY(config.contains(QStringLiteral("waves=0\n")));
    }

    void testGeneratedConfigVariantMapping()
    {
        SpectrumOptions opts;
        opts.inputMethod = QStringLiteral("pipewire");
        opts.channelMode = ChannelMode::MonoLeft;
        opts.reverse = true;
        opts.monstercat = true;
        opts.autosens = false;
        const QString config = PhosphorAudio::CavaSpectrumProvider::generateConfig(opts);
        QVERIFY(config.contains(QStringLiteral("method=pipewire\n")));
        QVERIFY(config.contains(QStringLiteral("channels=mono\n")));
        QVERIFY(config.contains(QStringLiteral("mono_option=left\n")));
        QVERIFY(config.contains(QStringLiteral("reverse=1\n")));
        QVERIFY(config.contains(QStringLiteral("monstercat=1\n")));
        QVERIFY(config.contains(QStringLiteral("autosens=0\n")));
    }

    void testGeneratedConfigBlocksInjection()
    {
        // A hostile source must not be able to open a new config line: the
        // sanitizer strips the newlines, so the whole payload stays inline on
        // the single source= line and no second raw_target can appear.
        SpectrumOptions opts;
        opts.inputMethod = QStringLiteral("pulse");
        opts.inputSource = QStringLiteral("evil\n[output]\nraw_target=/tmp/evil");
        const QString config = PhosphorAudio::CavaSpectrumProvider::generateConfig(opts);
        QVERIFY(config.contains(QStringLiteral("source=evil[output]raw_target=/tmp/evil\n")));
        QVERIFY(!config.contains(QStringLiteral("\nraw_target=/tmp/evil")));
        QVERIFY(config.contains(QStringLiteral("raw_target=/dev/stdout\n")));
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
