// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: LGPL-2.1-or-later

#include <PhosphorShaders/ShaderBindings.h>

#include <QSet>
#include <QStringView>
#include <QTest>

#include <array>

using namespace PhosphorShaders;

namespace {

int bindingOf(const char16_t* name)
{
    return Bindings::expectedSamplerBinding(QStringView(name));
}

/// Every canonical contract sampler, in table order. `uCursorSprite` and
/// `uBackdrop` are deliberately absent: they are the documented aliases of
/// `uTexture0` and `uWallpaper`, so including them would double-count a slot
/// the contiguity check below asserts is claimed exactly once.
const std::array<const char16_t*, 16> kContractSamplers = {
    u"uZoneLabels", u"iChannel0", u"iChannel1",  u"iChannel2",      u"iChannel3", u"iChannel4",
    u"iChannel5",   u"iChannel6", u"iChannel7",  u"uAudioSpectrum", u"uTexture0", u"uTexture1",
    u"uTexture2",   u"uTexture3", u"uWallpaper", u"uDepthBuffer"};

} // namespace

class TestShaderBindings : public QObject
{
    Q_OBJECT

private Q_SLOTS:

    // ═══════════════════════════════════════════════════════════════════════
    // expectedSamplerBinding
    // ═══════════════════════════════════════════════════════════════════════

    /// The literal bindings each shared GLSL header writes into its
    /// `layout(binding = N)` declarations. These are spelled out rather than
    /// derived from the constants on purpose: deriving them would let a header
    /// and this test drift together, which is the drift the pack gate exists to
    /// catch.
    void samplerBindingsMatchTheDeclaredTable()
    {
        QCOMPARE(bindingOf(u"uZoneLabels"), 1);
        QCOMPARE(bindingOf(u"iChannel0"), 2);
        QCOMPARE(bindingOf(u"iChannel7"), 9);
        QCOMPARE(bindingOf(u"uAudioSpectrum"), 10);
        QCOMPARE(bindingOf(u"uTexture0"), 11);
        QCOMPARE(bindingOf(u"uTexture3"), 14);
        QCOMPARE(bindingOf(u"uWallpaper"), 15);
        QCOMPARE(bindingOf(u"uDepthBuffer"), 16);
    }

    /// The two documented aliases. A family that has no `uTexture0` of its own
    /// spends slot 11 on its own sampler (pointer / `uCursorSprite`), and the
    /// one wallpaper slot answers to both family names.
    void aliasesResolveToTheSlotTheyShare()
    {
        QCOMPARE(bindingOf(u"uCursorSprite"), bindingOf(u"uTexture0"));
        QCOMPARE(bindingOf(u"uBackdrop"), bindingOf(u"uWallpaper"));
    }

    /// Past the end of either indexed run. `iChannel8` and `uTexture4` are both
    /// nine characters, so they reach the digit branch and must be rejected on
    /// the range check rather than on the length one.
    void indicesPastTheBudgetAreNotContractSamplers()
    {
        QCOMPARE(bindingOf(u"iChannel8"), -1);
        QCOMPARE(bindingOf(u"iChannel9"), -1);
        QCOMPARE(bindingOf(u"uTexture4"), -1);
        QCOMPARE(bindingOf(u"uTexture9"), -1);
    }

    /// The length check. A two-digit index is ten characters and never reaches
    /// the digit branch, so it answers -1 rather than reading `iChannel1`'s
    /// slot from the first digit.
    void multiDigitIndicesAreNotTruncated()
    {
        QCOMPARE(bindingOf(u"iChannel10"), -1);
        QCOMPARE(bindingOf(u"uTexture10"), -1);
    }

    /// Right length, right prefix, wrong final character. `digitValue()` answers
    /// -1 there, which the range check has to reject; a bare `< kChannelCount`
    /// would let it through as a negative binding.
    void nonDigitSuffixesAreRejected()
    {
        QCOMPARE(bindingOf(u"iChannelX"), -1);
        QCOMPARE(bindingOf(u"uTextureX"), -1);
    }

    void unknownNamesAreNotContractSamplers()
    {
        QCOMPARE(bindingOf(u""), -1);
        QCOMPARE(bindingOf(u"iChannel"), -1);
        QCOMPARE(bindingOf(u"uTexture"), -1);
        QCOMPARE(bindingOf(u"uSomeConsumerTexture"), -1);
        // Case matters: GLSL identifiers are case-sensitive and the reflection
        // the validator feeds this is verbatim.
        QCOMPARE(bindingOf(u"ichannel0"), -1);
        QCOMPARE(bindingOf(u"UTexture0"), -1);
    }

    /// The header calls the table CONTIGUOUS, and F604's portability note counts
    /// on that: the contract claims bindings 1 through 16 and nothing else, which
    /// is exactly 16 combined image samplers, the per-shader limit Qt documents
    /// on QRhiShaderResourceBinding. A slot added without a matching binding
    /// number, or a gap opened by a removal, breaks the count this test pins.
    void theContractClaimsSixteenContiguousSamplerSlots()
    {
        QSet<int> claimed;
        for (const char16_t* name : kContractSamplers) {
            const int binding = bindingOf(name);
            QVERIFY2(binding >= 0, qPrintable(QStringView(name).toString()));
            QVERIFY2(!claimed.contains(binding), qPrintable(QStringView(name).toString()));
            claimed.insert(binding);
        }
        QCOMPARE(claimed.size(), 16);
        for (int binding = Bindings::kConsumer; binding <= Bindings::kDepth; ++binding) {
            QVERIFY2(claimed.contains(binding), qPrintable(QStringLiteral("gap at %1").arg(binding)));
        }
        QCOMPARE(Bindings::kDepth - Bindings::kConsumer + 1, 16);
    }

    // ═══════════════════════════════════════════════════════════════════════
    // isConsumerBinding
    // ═══════════════════════════════════════════════════════════════════════

    /// The gap slot and the open range above the reserved one.
    void consumerBindingsAreTheGapSlotAndTheOpenRange()
    {
        QVERIFY(Bindings::isConsumerBinding(Bindings::kConsumer));
        QVERIFY(Bindings::isConsumerBinding(Bindings::kExtraBase));
        QVERIFY(Bindings::isConsumerBinding(Bindings::kMaxBinding));
        QVERIFY(Bindings::isConsumerBinding(20));
    }

    /// The uniform block, the whole reserved sampler run, and anything past the
    /// portable ceiling. The ceiling matters most: a consumer that claimed 32
    /// would build an SRB this library says nothing about.
    void reservedAndOutOfRangeBindingsAreNotConsumerBindings()
    {
        QVERIFY(!Bindings::isConsumerBinding(Bindings::kUniformBlock));
        for (int binding = Bindings::kChannelBase; binding <= Bindings::kReservedEnd; ++binding) {
            QVERIFY2(!Bindings::isConsumerBinding(binding), qPrintable(QStringLiteral("binding %1").arg(binding)));
        }
        QVERIFY(!Bindings::isConsumerBinding(Bindings::kMaxBinding + 1));
        QVERIFY(!Bindings::isConsumerBinding(-1));
    }

    /// The property that makes the validator's two-branch check sound: a name
    /// that resolves to a contract binding never lands in a slot a consumer may
    /// also claim, so the two branches cannot both accept the same binding.
    ///
    /// `uZoneLabels` is the one exception and it is the point of the gap slot.
    /// The zone-labels texture IS the overlay consumer's own, which is why it
    /// sits at kConsumer rather than inside the reserved run.
    void onlyTheZoneLabelsSlotIsBothContractAndConsumer()
    {
        for (const char16_t* name : kContractSamplers) {
            const int binding = bindingOf(name);
            const bool isZoneLabels = QStringView(name) == QLatin1String("uZoneLabels");
            QCOMPARE(Bindings::isConsumerBinding(binding), isZoneLabels);
        }
    }
};

QTEST_MAIN(TestShaderBindings)
#include "test_shaderbindings.moc"
