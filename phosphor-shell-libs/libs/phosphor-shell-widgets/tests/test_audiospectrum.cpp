// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later
#include "../src/AudioSpectrum.h"
#include <PhosphorAudio/IAudioSpectrumProvider.h>
#include <QtTest>
#include <limits>
class FakeSpectrum : public PhosphorAudio::IAudioSpectrumProvider
{
public:
    bool isAvailable() const override
    {
        return true;
    }
    void start() override
    {
        ++starts;
        running = true;
    }
    void stop() override
    {
        ++stops;
        running = false;
    }
    bool isRunning() const override
    {
        return running;
    }
    PhosphorAudio::SpectrumOptions options() const override
    {
        return config;
    }
    void setOptions(const PhosphorAudio::SpectrumOptions& value) override
    {
        config = value;
    }
    QVector<float> spectrum() const override
    {
        return {};
    }
    PhosphorAudio::SpectrumOptions config;
    int starts = 0, stops = 0;
    bool running = false;
};
class TestAudioSpectrum : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void sharesCaptureAndReleasesDestroyedConsumers()
    {
        FakeSpectrum provider;
        AudioSpectrum spectrum(&provider, nullptr);
        QObject bar;
        spectrum.setActive(&bar, true);
        spectrum.setActive(&bar, true);
        QCOMPARE(provider.starts, 1);
        {
            QObject popup;
            spectrum.setActive(&popup, true);
            QCOMPARE(provider.starts, 1);
            spectrum.setActive(&bar, false);
            QCOMPARE(provider.stops, 0);
            QCOMPARE(spectrum.consumers(), 1);
        }
        QCOMPARE(provider.stops, 1);
        QCOMPARE(spectrum.consumers(), 0);
        QVERIFY(spectrum.samples().isEmpty());
        spectrum.setActive(&bar, true);
        QCOMPARE(provider.starts, 2);
        spectrum.setActive(&bar, false);
        QCOMPARE(provider.stops, 2);
    }
    void boundsAndSanitizesSamples()
    {
        FakeSpectrum provider;
        AudioSpectrum spectrum(&provider, nullptr);
        Q_EMIT provider.spectrumUpdated({-1, 2, std::numeric_limits<float>::quiet_NaN(), 0.5});
        QCOMPARE(spectrum.samples(), QVariantList({0.0f, 1.0f, 0.0f, 0.5f}));
        Q_EMIT provider.spectrumUpdated(QVector<float>(200, 0.5f));
        QCOMPARE(spectrum.samples().size(), 64);
        QCOMPARE(provider.config.barCount, 48);
        QCOMPARE(provider.config.framerate, 30);
    }
};
QTEST_GUILESS_MAIN(TestAudioSpectrum)
#include "test_audiospectrum.moc"
