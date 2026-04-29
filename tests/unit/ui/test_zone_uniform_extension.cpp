// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QObject>
#include <QColor>
#include <QRectF>
#include <QVector>

#include <cstring>
#include <vector>

#include "daemon/rendering/zoneuniformextension.h"
#include "daemon/rendering/zoneshadercommon.h"

using namespace PlasmaZones;

/**
 * @brief Unit tests for ZoneUniformExtension
 *
 * The extension writes zone data into a UBO region whose byte layout MUST
 * match the GLSL UBO declaration in common.glsl exactly. Any reordering or
 * stride change here silently breaks rendering with no compiler error — so
 * these tests exercise the byte layout directly, without any Qt RHI / GPU.
 */
class TestZoneUniformExtension : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void extensionSize_matchesZoneShaderUniformsRegion();
    void write_emptyZones_zerosAllSlots();
    void write_singleZone_populatesFirstSlotAndZerosRest();
    void write_fullCapacity_usesAllSlots();
    void write_overflow_truncatesToMaxZones();
    void write_overflow_doesNotWritePastBufferEnd();
    void write_shrinkingZones_zerosVacatedSlots();
    void write_nonZeroOffset_writesAtRequestedOffset();
    void dirty_initiallyTrue_clearable();
    void dirty_setOnUpdateFromZones();
    void layout_matchesZoneShaderUniformsOffsets();
};

namespace {

constexpr int kFloatsPerVec4 = 4;
constexpr int kBytesPerVec4 = kFloatsPerVec4 * sizeof(float);
constexpr int kArraysPerZone = 4; // rect, fillColor, borderColor, params
constexpr int kBytesPerZone = kArraysPerZone * kBytesPerVec4;

/// Read a vec4 from a byte buffer at zone-array offset (rectIndex / fillIndex /
/// borderIndex / paramsIndex are 0..3 selecting which array, zoneIdx is 0..MaxZones-1).
struct Vec4
{
    float x, y, z, w;
};

Vec4 readVec4(const std::vector<char>& buf, int arrayIndex, int zoneIdx)
{
    // PhosphorZones::Layout: zoneRects[MaxZones][4] | zoneFillColors[MaxZones][4] | zoneBorderColors[MaxZones][4] |
    // zoneParams[MaxZones][4]
    const int arrayOffset = arrayIndex * MaxZones * kBytesPerVec4;
    const int zoneOffset = zoneIdx * kBytesPerVec4;
    Vec4 v{};
    std::memcpy(&v, buf.data() + arrayOffset + zoneOffset, sizeof(Vec4));
    return v;
}

ZoneData makeZone(qreal x, qreal y, qreal w, qreal h, const QColor& fill, const QColor& border, float radius,
                  float width, bool highlighted, int number)
{
    ZoneData z;
    z.rect = QRectF(x, y, w, h);
    z.fillColor = fill;
    z.borderColor = border;
    z.borderRadius = radius;
    z.borderWidth = width;
    z.isHighlighted = highlighted;
    z.zoneNumber = number;
    return z;
}

} // namespace

void TestZoneUniformExtension::extensionSize_matchesZoneShaderUniformsRegion()
{
    ZoneUniformExtension ext;
    const int expected = static_cast<int>(sizeof(ZoneShaderUniforms) - sizeof(PhosphorShaders::BaseUniforms));
    QCOMPARE(ext.extensionSize(), expected);
    QCOMPARE(ext.extensionSize(), MaxZones * kArraysPerZone * kBytesPerVec4);
}

void TestZoneUniformExtension::write_emptyZones_zerosAllSlots()
{
    ZoneUniformExtension ext;
    ext.updateFromZones({});

    std::vector<char> buf(ext.extensionSize(), char{0x55}); // poison
    ext.write(buf.data(), 0);

    for (int i = 0; i < MaxZones; ++i) {
        for (int arr = 0; arr < kArraysPerZone; ++arr) {
            const Vec4 v = readVec4(buf, arr, i);
            QCOMPARE(v.x, 0.0f);
            QCOMPARE(v.y, 0.0f);
            QCOMPARE(v.z, 0.0f);
            QCOMPARE(v.w, 0.0f);
        }
    }
}

void TestZoneUniformExtension::write_singleZone_populatesFirstSlotAndZerosRest()
{
    ZoneUniformExtension ext;
    QVector<ZoneData> zones;
    zones.append(makeZone(10.0, 20.0, 100.0, 200.0, QColor::fromRgbF(0.1f, 0.2f, 0.3f, 0.4f),
                          QColor::fromRgbF(0.5f, 0.6f, 0.7f, 0.8f),
                          /*radius*/ 8.0f, /*width*/ 2.0f, /*highlighted*/ true, /*number*/ 7));
    ext.updateFromZones(zones);

    std::vector<char> buf(ext.extensionSize(), 0);
    ext.write(buf.data(), 0);

    // Slot 0: rect
    Vec4 r = readVec4(buf, 0, 0);
    QCOMPARE(r.x, 10.0f);
    QCOMPARE(r.y, 20.0f);
    QCOMPARE(r.z, 100.0f);
    QCOMPARE(r.w, 200.0f);

    // Slot 0: fillColor
    Vec4 f = readVec4(buf, 1, 0);
    QVERIFY(qFuzzyCompare(f.x + 1.0f, 0.1f + 1.0f));
    QVERIFY(qFuzzyCompare(f.y + 1.0f, 0.2f + 1.0f));
    QVERIFY(qFuzzyCompare(f.z + 1.0f, 0.3f + 1.0f));
    QVERIFY(qFuzzyCompare(f.w + 1.0f, 0.4f + 1.0f));

    // Slot 0: borderColor
    Vec4 b = readVec4(buf, 2, 0);
    QVERIFY(qFuzzyCompare(b.x + 1.0f, 0.5f + 1.0f));
    QVERIFY(qFuzzyCompare(b.y + 1.0f, 0.6f + 1.0f));
    QVERIFY(qFuzzyCompare(b.z + 1.0f, 0.7f + 1.0f));
    QVERIFY(qFuzzyCompare(b.w + 1.0f, 0.8f + 1.0f));

    // Slot 0: params (radius, width, highlighted, number)
    Vec4 p = readVec4(buf, 3, 0);
    QCOMPARE(p.x, 8.0f);
    QCOMPARE(p.y, 2.0f);
    QCOMPARE(p.z, 1.0f);
    QCOMPARE(p.w, 7.0f);

    // Slots 1..MaxZones-1: all zeroed
    for (int i = 1; i < MaxZones; ++i) {
        for (int arr = 0; arr < kArraysPerZone; ++arr) {
            const Vec4 v = readVec4(buf, arr, i);
            QCOMPARE(v.x, 0.0f);
            QCOMPARE(v.y, 0.0f);
            QCOMPARE(v.z, 0.0f);
            QCOMPARE(v.w, 0.0f);
        }
    }
}

void TestZoneUniformExtension::write_fullCapacity_usesAllSlots()
{
    ZoneUniformExtension ext;
    QVector<ZoneData> zones;
    for (int i = 0; i < MaxZones; ++i) {
        zones.append(makeZone(i, i + 1, i + 2, i + 3, QColor::fromRgbF(0, 0, 0, 1), QColor::fromRgbF(0, 0, 0, 1), 0.0f,
                              0.0f, false, i));
    }
    ext.updateFromZones(zones);

    std::vector<char> buf(ext.extensionSize(), 0);
    ext.write(buf.data(), 0);

    for (int i = 0; i < MaxZones; ++i) {
        Vec4 r = readVec4(buf, 0, i);
        QCOMPARE(r.x, static_cast<float>(i));
        QCOMPARE(r.y, static_cast<float>(i + 1));
        QCOMPARE(r.z, static_cast<float>(i + 2));
        QCOMPARE(r.w, static_cast<float>(i + 3));
        Vec4 p = readVec4(buf, 3, i);
        QCOMPARE(p.w, static_cast<float>(i)); // zoneNumber
    }
}

void TestZoneUniformExtension::write_overflow_truncatesToMaxZones()
{
    ZoneUniformExtension ext;
    QVector<ZoneData> zones;
    for (int i = 0; i < MaxZones + 10; ++i) {
        zones.append(
            makeZone(i, 0, 1, 1, QColor::fromRgbF(0, 0, 0, 1), QColor::fromRgbF(0, 0, 0, 1), 0.0f, 0.0f, false, i));
    }
    ext.updateFromZones(zones);

    std::vector<char> buf(ext.extensionSize(), 0);
    ext.write(buf.data(), 0);

    // First MaxZones populated
    for (int i = 0; i < MaxZones; ++i) {
        Vec4 r = readVec4(buf, 0, i);
        QCOMPARE(r.x, static_cast<float>(i));
    }
    // Buffer is exactly MaxZones * kBytesPerZone wide — past-end writes would
    // have overflowed and corrupted memory; the static_assert in the header
    // backs this up at compile time, but we still want a runtime check that
    // updateFromZones doesn't index past MaxZones.
}

void TestZoneUniformExtension::write_overflow_doesNotWritePastBufferEnd()
{
    // Poison the region immediately after the extension buffer, then verify
    // write() never touches those bytes even when updateFromZones is fed
    // too many zones. Catches silent stride mistakes that the static_assert
    // would miss if a future refactor moved zoneParams inside a union.
    ZoneUniformExtension ext;
    QVector<ZoneData> zones;
    for (int i = 0; i < MaxZones + 10; ++i) {
        zones.append(
            makeZone(i, 0, 1, 1, QColor::fromRgbF(0, 0, 0, 1), QColor::fromRgbF(0, 0, 0, 1), 0.0f, 0.0f, false, i));
    }
    ext.updateFromZones(zones);

    constexpr int kGuardBytes = 64;
    std::vector<char> buf(static_cast<size_t>(ext.extensionSize()) + kGuardBytes, static_cast<char>(0xAA));
    ext.write(buf.data(), 0);

    // Bytes after extensionSize() must still be 0xAA — write() must not have
    // scribbled past the end of the caller's buffer.
    for (int i = 0; i < kGuardBytes; ++i) {
        const unsigned char actual = static_cast<unsigned char>(buf[ext.extensionSize() + i]);
        QCOMPARE(actual, static_cast<unsigned char>(0xAA));
    }
}

void TestZoneUniformExtension::write_shrinkingZones_zerosVacatedSlots()
{
    // Populate all slots, then shrink to a smaller set. The vacated tail
    // (slots [small, MaxZones)) must be zeroed on the next updateFromZones.
    // Without this, stale data from the previous update would render as
    // ghost zones — the test is a guard against an "optimisation" that
    // iterates only zones.size() instead of MaxZones.
    ZoneUniformExtension ext;
    QVector<ZoneData> full;
    for (int i = 0; i < MaxZones; ++i) {
        full.append(makeZone(i + 100.0, i + 200.0, 10.0, 20.0, QColor::fromRgbF(1, 0, 0, 1),
                             QColor::fromRgbF(0, 1, 0, 1), 5.0f, 2.0f, true, i));
    }
    ext.updateFromZones(full);

    QVector<ZoneData> small;
    small.append(makeZone(0.0, 0.0, 1.0, 2.0, QColor::fromRgbF(0.5f, 0.5f, 0.5f, 1), QColor::fromRgbF(0, 0, 0, 1), 1.0f,
                          1.0f, false, 0));
    ext.updateFromZones(small);

    std::vector<char> buf(ext.extensionSize(), static_cast<char>(0xFF));
    ext.write(buf.data(), 0);

    // Slot 0 populated with the small zone.
    Vec4 r0 = readVec4(buf, 0, 0);
    QCOMPARE(r0.x, 0.0f);
    QCOMPARE(r0.y, 0.0f);
    QCOMPARE(r0.z, 1.0f);
    QCOMPARE(r0.w, 2.0f);

    // Slots 1..MaxZones-1 must be zero — not leftover 0xFF or the old
    // populated data.
    for (int i = 1; i < MaxZones; ++i) {
        for (int arr = 0; arr < kArraysPerZone; ++arr) {
            const Vec4 v = readVec4(buf, arr, i);
            QCOMPARE(v.x, 0.0f);
            QCOMPARE(v.y, 0.0f);
            QCOMPARE(v.z, 0.0f);
            QCOMPARE(v.w, 0.0f);
        }
    }
}

void TestZoneUniformExtension::write_nonZeroOffset_writesAtRequestedOffset()
{
    // The UBO writer calls write(buf, offset = sizeof(BaseUniforms)) so the
    // extension region lands after the base struct. Ensure write() honours
    // offset — a refactor to memcpy(buf, ...) instead of memcpy(buf+offset,
    // ...) would break the UBO layout silently.
    ZoneUniformExtension ext;
    QVector<ZoneData> zones;
    zones.append(makeZone(7.0, 8.0, 9.0, 10.0, QColor::fromRgbF(0.2f, 0.3f, 0.4f, 1.0f),
                          QColor::fromRgbF(0.5f, 0.6f, 0.7f, 1.0f), 0.0f, 0.0f, false, 3));
    ext.updateFromZones(zones);

    constexpr int kPrefix = 32;
    std::vector<char> buf(static_cast<size_t>(kPrefix + ext.extensionSize()), static_cast<char>(0xCC));
    ext.write(buf.data(), kPrefix);

    // Prefix bytes untouched — verifies write() didn't stomp on them.
    for (int i = 0; i < kPrefix; ++i) {
        QCOMPARE(static_cast<unsigned char>(buf[i]), static_cast<unsigned char>(0xCC));
    }
    // Extension data lands at offset kPrefix.
    Vec4 r;
    std::memcpy(&r, buf.data() + kPrefix, sizeof(r));
    QCOMPARE(r.x, 7.0f);
    QCOMPARE(r.y, 8.0f);
    QCOMPARE(r.z, 9.0f);
    QCOMPARE(r.w, 10.0f);
}

void TestZoneUniformExtension::dirty_initiallyTrue_clearable()
{
    ZoneUniformExtension ext;
    QVERIFY(ext.isDirty()); // initially dirty so first prepare() uploads
    ext.clearDirty();
    QVERIFY(!ext.isDirty());
}

void TestZoneUniformExtension::dirty_setOnUpdateFromZones()
{
    ZoneUniformExtension ext;
    ext.clearDirty();
    QVERIFY(!ext.isDirty());
    ext.updateFromZones({});
    QVERIFY(ext.isDirty());
}

void TestZoneUniformExtension::layout_matchesZoneShaderUniformsOffsets()
{
    // Write integer-valued zone data (no float-precision noise from QColor's
    // internal storage) and verify the extension's bytes land at the same
    // offsets as ZoneShaderUniforms' zone region.
    ZoneShaderUniforms expected = {};
    expected.zoneRects[0][0] = 1.0f;
    expected.zoneRects[0][1] = 2.0f;
    expected.zoneRects[0][2] = 3.0f;
    expected.zoneRects[0][3] = 4.0f;
    expected.zoneParams[0][0] = 9.0f;
    expected.zoneParams[0][1] = 3.0f;
    expected.zoneParams[0][2] = 0.0f;
    expected.zoneParams[0][3] = 5.0f;
    // For colors, write integer-valued floats directly (sidestep QColor float
    // round-trip) — we only care about byte offset, not the round-trip.
    expected.zoneFillColors[0][0] = 1.0f;
    expected.zoneFillColors[0][1] = 0.0f;
    expected.zoneFillColors[0][2] = 0.0f;
    expected.zoneFillColors[0][3] = 1.0f;
    expected.zoneBorderColors[0][0] = 0.0f;
    expected.zoneBorderColors[0][1] = 1.0f;
    expected.zoneBorderColors[0][2] = 0.0f;
    expected.zoneBorderColors[0][3] = 1.0f;

    ZoneUniformExtension ext;
    QVector<ZoneData> zones;
    zones.append(makeZone(1.0, 2.0, 3.0, 4.0, QColor::fromRgbF(1.0f, 0.0f, 0.0f, 1.0f),
                          QColor::fromRgbF(0.0f, 1.0f, 0.0f, 1.0f),
                          /*radius*/ 9.0f, /*width*/ 3.0f, /*highlighted*/ false, /*number*/ 5));
    ext.updateFromZones(zones);

    std::vector<char> actual(ext.extensionSize(), 0);
    ext.write(actual.data(), 0);

    const char* expectedZoneStart = reinterpret_cast<const char*>(&expected) + sizeof(PhosphorShaders::BaseUniforms);
    if (std::memcmp(actual.data(), expectedZoneStart, ext.extensionSize()) != 0) {
        // Find first differing byte for diagnostic
        for (int i = 0; i < ext.extensionSize(); ++i) {
            if (actual[i] != expectedZoneStart[i]) {
                qWarning("layout mismatch at byte offset %d: actual=0x%02x expected=0x%02x", i,
                         static_cast<unsigned char>(actual[i]), static_cast<unsigned char>(expectedZoneStart[i]));
                break;
            }
        }
    }
    QCOMPARE(std::memcmp(actual.data(), expectedZoneStart, ext.extensionSize()), 0);
}

QTEST_APPLESS_MAIN(TestZoneUniformExtension)
#include "test_zone_uniform_extension.moc"
