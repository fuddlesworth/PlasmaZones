// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorAudio/AudioDefaults.h>
#include <PhosphorAudio/CavaSpectrumProvider.h>
#include <PhosphorAudio/IAudioSpectrumProvider.h>

#include <QTest>

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
    }

    void testProviderConstruction()
    {
        PhosphorAudio::CavaSpectrumProvider provider;
        QVERIFY(!provider.isRunning());
        QCOMPARE(provider.barCount(), 64);
        QCOMPARE(provider.framerate(), 60);
        QVERIFY(provider.spectrum().isEmpty());
    }

    void testBarCountClamping()
    {
        PhosphorAudio::CavaSpectrumProvider provider;

        provider.setBarCount(8);
        QCOMPARE(provider.barCount(), PhosphorAudio::Defaults::MinBars);

        provider.setBarCount(512);
        QCOMPARE(provider.barCount(), PhosphorAudio::Defaults::MaxBars);

        provider.setBarCount(33);
        QCOMPARE(provider.barCount(), 34);
    }

    void testFramerateClamping()
    {
        PhosphorAudio::CavaSpectrumProvider provider;

        provider.setFramerate(10);
        QCOMPARE(provider.framerate(), PhosphorAudio::Defaults::MinFramerate);

        provider.setFramerate(200);
        QCOMPARE(provider.framerate(), PhosphorAudio::Defaults::MaxFramerate);

        provider.setFramerate(90);
        QCOMPARE(provider.framerate(), 90);
    }

    void testInterfacePolymorphism()
    {
        PhosphorAudio::CavaSpectrumProvider concrete;
        PhosphorAudio::IAudioSpectrumProvider* iface = &concrete;
        QVERIFY(!iface->isRunning());
        QCOMPARE(iface->barCount(), 64);
    }
};

QTEST_MAIN(TestPhosphorAudio)

#include "test_phosphoraudio.moc"
