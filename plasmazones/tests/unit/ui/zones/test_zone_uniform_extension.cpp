// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QTest>
#include <QFile>
#include <QObject>
#include <QColor>
#include <QRectF>
#include <QRegularExpression>
#include <QVector>

#include <cstring>
#include <limits>
#include <vector>

#include <PhosphorRendering/ZoneShaderCommon.h>
#include <PhosphorRendering/ZoneUniformExtension.h>

using PhosphorRendering::MaxZones;
using PhosphorRendering::ZoneData;
using PhosphorRendering::ZoneShaderUniforms;
using PhosphorRendering::ZoneUniformExtension;

/**
 * @brief Unit tests for ZoneUniformExtension
 *
 * The extension writes zone data into a UBO region whose byte layout MUST
 * match the GLSL UBO declaration in common.glsl exactly. Any reordering or
 * stride change here silently breaks rendering with no compiler error — so
 * these tests exercise the byte layout directly, without any Qt RHI / GPU.
 *
 * layout_matchesGlslUboDeclaration additionally READS the shader source via
 * P_SOURCE_DIR, so this binary is not relocatable: it needs the source tree,
 * not just an install.
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
    void scale_defaultsToIdentity();
    void scale_writesAtTailOffset();
    void scale_survivesZoneUpdate();
    void dirty_setOnScaleChangeOnly();
    void scale_rejectsNonPositiveAndNonFinite();
    void requiresPhysicalResolution_isTrue();
    void layout_matchesZoneShaderUniformsOffsets();
    void layout_matchesGlslUboDeclaration();
    void scaleHelpers_matchTheirDocumentedGuards();
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
    // The zone arrays plus the trailing zoneScale scalar and its std140 pad.
    QCOMPARE(ext.extensionSize(), MaxZones * kArraysPerZone * kBytesPerVec4 + kBytesPerVec4);
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
    QCOMPARE(f.x + 1.0f, 0.1f + 1.0f);
    QCOMPARE(f.y + 1.0f, 0.2f + 1.0f);
    QCOMPARE(f.z + 1.0f, 0.3f + 1.0f);
    QCOMPARE(f.w + 1.0f, 0.4f + 1.0f);

    // Slot 0: borderColor
    Vec4 b = readVec4(buf, 2, 0);
    QCOMPARE(b.x + 1.0f, 0.5f + 1.0f);
    QCOMPARE(b.y + 1.0f, 0.6f + 1.0f);
    QCOMPARE(b.z + 1.0f, 0.7f + 1.0f);
    QCOMPARE(b.w + 1.0f, 0.8f + 1.0f);

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
    // This asserts TRUNCATION only: that the first MaxZones slots hold what was
    // asked for. It does NOT detect a past-end write — that is silent UB, and
    // these assertions stay true under an updateFromZones that runs off the end
    // of the array. write_overflow_doesNotWritePastBufferEnd is the test that
    // actually proves that, via its 0xAA guard bytes.
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

namespace {

/// Read the trailing zoneScale scalar out of a written extension buffer.
float readScale(const std::vector<char>& buf)
{
    float scale = 0.0f;
    std::memcpy(&scale, buf.data() + MaxZones * kArraysPerZone * kBytesPerVec4, sizeof(float));
    return scale;
}

} // namespace

void TestZoneUniformExtension::scale_defaultsToIdentity()
{
    // Not merely cosmetic: the shader multiplies every corner radius and border
    // width by this, so a zeroed default would paint square, border-less zones
    // until the item's first sync reported the real device-pixel ratio.
    ZoneUniformExtension ext;
    std::vector<char> buf(ext.extensionSize(), char{0x55});
    ext.write(buf.data(), 0);
    QCOMPARE(readScale(buf), 1.0f);
}

void TestZoneUniformExtension::scale_writesAtTailOffset()
{
    ZoneUniformExtension ext;
    QVERIFY(ext.setScale(2.0f));

    std::vector<char> buf(ext.extensionSize(), 0);
    ext.write(buf.data(), 0);
    QCOMPARE(readScale(buf), 2.0f);

    // The scale must not bleed into the last zone's params — an off-by-one in
    // the tail offset would land inside zoneParams[63] and corrupt that zone.
    const Vec4 lastParams = readVec4(buf, 3, MaxZones - 1);
    QCOMPARE(lastParams.x, 0.0f);
    QCOMPARE(lastParams.y, 0.0f);
    QCOMPARE(lastParams.z, 0.0f);
    QCOMPARE(lastParams.w, 0.0f);
}

void TestZoneUniformExtension::scale_survivesZoneUpdate()
{
    // Zone contents and the scale are pushed by separate calls on different
    // clocks. updateFromZones() must not stomp a scale already reported, or a
    // drag frame would reset the overlay to 1x mid-gesture on a scaled screen.
    ZoneUniformExtension ext;
    QVERIFY(ext.setScale(1.5f));

    QVector<ZoneData> zones;
    zones.append(makeZone(0.0, 0.0, 1.0, 1.0, QColor::fromRgbF(1.0f, 1.0f, 1.0f, 1.0f),
                          QColor::fromRgbF(1.0f, 1.0f, 1.0f, 1.0f),
                          /*radius*/ 8.0f, /*width*/ 2.0f, /*highlighted*/ false, /*number*/ 1));
    ext.updateFromZones(zones);

    std::vector<char> buf(ext.extensionSize(), 0);
    ext.write(buf.data(), 0);
    QCOMPARE(readScale(buf), 1.5f);
}

void TestZoneUniformExtension::dirty_setOnScaleChangeOnly()
{
    ZoneUniformExtension ext;
    QVERIFY(ext.setScale(2.0f));
    ext.clearDirty();

    // Re-reporting the same ratio must not dirty the extension: the item calls
    // setScale() every frame, and a spurious dirty would re-upload the whole
    // 4 KiB zone region on every frame of every overlay.
    QVERIFY(ext.setScale(2.0f));
    QVERIFY(!ext.isDirty());

    QVERIFY(ext.setScale(1.0f));
    QVERIFY(ext.isDirty());
}

void TestZoneUniformExtension::scale_rejectsNonPositiveAndNonFinite()
{
    // The shader multiplies every corner radius and border width by this
    // value, so a bad one does not fail loudly: NaN propagates through the
    // clamp and erases the zone, and zero or a negative renders square
    // corners with a hairline border, which looks like a design choice. Any
    // of them must leave the last good scale in place.
    ZoneUniformExtension ext;
    QVERIFY(ext.setScale(2.0f));
    ext.clearDirty();

    QVERIFY2(!ext.setScale(0.0f), "zero must be rejected");
    QVERIFY2(!ext.setScale(-1.5f), "a negative scale must be rejected");
    QVERIFY2(!ext.setScale(std::numeric_limits<float>::quiet_NaN()), "NaN must be rejected");
    QVERIFY2(!ext.setScale(std::numeric_limits<float>::infinity()), "infinity must be rejected");
    QVERIFY(!ext.isDirty());

    std::vector<char> buf(ext.extensionSize(), 0);
    ext.write(buf.data(), 0);
    QCOMPARE(readScale(buf), 2.0f);
}

void TestZoneUniformExtension::requiresPhysicalResolution_isTrue()
{
    // The whole logical-to-device contract rests on this staying true. The
    // item reports a device-pixel ratio for zoneScale precisely because the
    // base pre-multiplies iResolution by the same ratio, and it only does
    // that while the installed extension asks for physical resolution. Flip
    // this to false and iResolution goes logical while the radius stays
    // dpr-scaled, so every corner is wrong by the display scale with nothing
    // in the build to catch it.
    ZoneUniformExtension ext;
    QVERIFY(ext.requiresPhysicalResolution());
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
    // A fresh extension reports the identity scale, not a zeroed one — see the
    // constructor's note on why zero would render the first frames square.
    expected.zoneScale = 1.0f;

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

void TestZoneUniformExtension::layout_matchesGlslUboDeclaration()
{
    // The class comment above says the byte layout "MUST match the GLSL UBO
    // declaration in common.glsl exactly", and until this test nothing checked
    // it. Every other assertion in this file compares C++ against C++: the
    // offsetof static_asserts in ZoneUniformExtension.h already make
    // layout_matchesZoneShaderUniformsOffsets() unable to fail without a
    // compile error first. So the whole suite passed while MaxZones and the
    // GLSL array dimensions disagreed, or while a member was inserted ahead of
    // uZoneScale — either of which renders all 27 overlay packs as garbage with
    // no build error, because std140 offsets are decided independently on each
    // side and never cross-checked.
    //
    // Parse the real file rather than restating the layout, so the test tracks
    // whatever the shaders actually compile against.
    QFile glsl(QStringLiteral(P_SOURCE_DIR "/data/overlays/shared/common.glsl"));
    // Opened first: QVERIFY2 takes both arguments as function-call arguments,
    // whose evaluation order C++ leaves unsequenced, so errorString() could be
    // sampled before open() ran and report nothing.
    if (!glsl.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QFAIL(qPrintable(glsl.errorString()));
    }
    const QString source = QString::fromUtf8(glsl.readAll());

    // Anchor on the FULL qualifier, not just the block name. Dropping `std140`
    // makes the layout implementation-defined and changing `binding = 0`
    // unbinds the block from the buffer the C++ side writes. Either one breaks
    // every pack with no build error, so matching only "uniform ZoneUniforms"
    // would leave the two things most worth pinning unpinned.
    static const QRegularExpression blockRe(
        QStringLiteral(R"(layout\s*\(\s*std140\s*,\s*binding\s*=\s*0\s*\)\s*uniform\s+ZoneUniforms)"));
    const QRegularExpressionMatch blockMatch = blockRe.match(source);
    QVERIFY2(blockMatch.hasMatch(),
             "ZoneUniforms must be declared layout(std140, binding = 0); the C++ side depends on both");
    const int blockStart = blockMatch.capturedStart();

    const int bodyStart = source.indexOf(QLatin1Char('{'), blockStart);
    // Checked before it is used as a search origin: QString::indexOf treats a
    // negative `from` as an offset back from the end of the string, so a failed
    // bodyStart would silently make the closing-brace search scan the file tail
    // and still produce a plausible-looking body.
    QVERIFY2(bodyStart >= 0, "no opening brace after the ZoneUniforms declaration");
    const int bodyEnd = source.indexOf(QLatin1Char('}'), bodyStart);
    QVERIFY2(bodyEnd > bodyStart, "no closing brace for the ZoneUniforms block (unbalanced brace in a comment?)");
    const QString body = source.mid(bodyStart + 1, bodyEnd - bodyStart - 1);

    // The block must stay ANONYMOUS. Naming the instance (`} zu;`) would force
    // every pack to write zu.zoneRects, so all 27 stop compiling against the
    // block the C++ side writes — with no build error on the C++ side at all.
    QVERIFY2(source.mid(bodyEnd + 1).trimmed().startsWith(QLatin1Char(';')),
             "ZoneUniforms must stay an anonymous block; an instance name would make every pack qualify its members");

    // std140 layout only depends on member type, order and array length, so
    // that triple is exactly what has to agree. Comments carry none of it.
    static const QRegularExpression memberRe(QStringLiteral(R"(^\s*(\w+)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;)"),
                                             QRegularExpression::MultilineOption);
    // Same shape but anchored at BOTH ends, used only by the totality check so a
    // line carrying two declarations fails instead of half-matching.
    static const QRegularExpression wholeLineRe(QStringLiteral(R"(^\s*(\w+)\s+(\w+)\s*(?:\[\s*(\d+)\s*\])?\s*;\s*$)"));

    struct Member
    {
        QString type;
        QString name;
        int arrayLen; // 0 == not an array
    };
    // Block comments stripped ONCE, up front, so the member parse and the
    // totality count below are derived from the same text. Parsing `body` while
    // counting `stripped` let a commented-out declaration become a phantom
    // member that the count never saw, which shifted every later index and
    // failed with a bare number naming nothing.
    static const QRegularExpression blockCommentRe(QStringLiteral(R"(/\*.*?\*/)"),
                                                   QRegularExpression::DotMatchesEverythingOption);
    QString stripped = body;
    stripped.replace(blockCommentRe, QString());

    QVector<Member> members;
    auto it = memberRe.globalMatch(stripped);
    while (it.hasNext()) {
        const auto m = it.next();
        members.append({m.captured(1), m.captured(2), m.captured(3).isEmpty() ? 0 : m.captured(3).toInt()});
    }

    // The parse must be TOTAL. A declaration the regex does not recognise is
    // otherwise dropped SILENTLY, and a dropped member still occupies std140
    // space, so the head walk and the size compare both stay green while every
    // pack reads shifted bytes. Three shapes do this: a declaration wrapped
    // across two lines, comma-separated declarators (`float a, b;`), and a
    // precision qualifier (`highp vec4 foo;`). Requiring every non-comment,
    // non-blank line to have been consumed turns each of them into a failure
    // that names the offending line instead of a silent pass.
    {
        int consumed = 0;
        const QStringList bodyLines = stripped.split(QLatin1Char('\n'));
        for (const QString& rawLine : bodyLines) {
            // Strip a trailing line comment, then test what is left.
            QString code = rawLine;
            const int commentAt = code.indexOf(QLatin1String("//"));
            if (commentAt >= 0) {
                code.truncate(commentAt);
            }
            if (code.trimmed().isEmpty()) {
                continue;
            }
            ++consumed;
            // Fully anchored (`$`), unlike memberRe, which is only `^`-anchored so
            // it can consume the FIRST declaration on a line and leave a second
            // one on the same line silently unparsed. `float uZoneScale; vec4
            // uExtra;` passed every other assertion while uExtra never reached the
            // C++ side at all.
            QVERIFY2(wholeLineRe.match(code).hasMatch(),
                     qPrintable(QStringLiteral("unparsed line in the ZoneUniforms block, so a member would be "
                                               "silently dropped from the layout check: '%1'")
                                    .arg(code.trimmed())));
        }
        QCOMPARE(members.size(), consumed);
    }

    // std140 sizing, by declared type. Needed for the head-region check below:
    // the zone arrays all happen to be vec4, so a vec4-only rule would be
    // enough for the tail, but the base block carries a mat4, ints, vec2s and
    // vec2 arrays, and getting their strides right is the whole point of
    // computing the head offset independently.
    //
    // Returns the size CONSUMED by the member including any alignment padding
    // inserted before it, given the offset it starts at.
    const auto std140Advance = [](const Member& m, int offset) -> int {
        int baseAlign = 0;
        int baseSize = 0;
        if (m.type == QLatin1String("float") || m.type == QLatin1String("int") || m.type == QLatin1String("uint")
            || m.type == QLatin1String("bool")) {
            baseAlign = 4;
            baseSize = 4;
        } else if (m.type == QLatin1String("vec2")) {
            baseAlign = 8;
            baseSize = 8;
        } else if (m.type == QLatin1String("vec3")) {
            // std140 vec3: base ALIGNMENT 16 but base SIZE 12, so a following
            // float packs into the same 16 bytes.
            baseAlign = 16;
            baseSize = 12;
        } else if (m.type == QLatin1String("vec4")) {
            baseAlign = 16;
            baseSize = 16;
        } else if (m.type == QLatin1String("mat4")) {
            baseAlign = 16;
            baseSize = 64;
        } else {
            return -1; // unknown type: caller fails loudly rather than guessing
        }
        // std140 rounds an array element's STRIDE up to a multiple of 16. That
        // is 16 for every scalar and vector type, but 64 for mat4, so the
        // stride has to be derived rather than hard-coded.
        if (m.arrayLen > 0) {
            const int stride = (baseSize + 15) / 16 * 16;
            baseAlign = 16;
            baseSize = stride * m.arrayLen;
        }
        const int aligned = (offset + baseAlign - 1) / baseAlign * baseAlign;
        return (aligned - offset) + baseSize;
    };

    // The GLSL block declares the WHOLE UBO: BaseUniforms first, then the zone
    // extension. The tail is this class's contract, but the HEAD has to be
    // pinned too. Inserting, removing or reordering a base member shifts the
    // entire extension region in std140 while leaving extensionSize() and the
    // tail comparison identical, which breaks all 27 packs with nothing failing.
    // The GLSL VIEW of BaseUniforms, member for member. Two deliberate
    // differences from the C++ struct: zoneCount/highlightedCount are the
    // overlay family's names for appField0/appField1, and BaseUniforms'
    // trailing iIsReversed is absent because the overlay family never reads
    // it and the block's round-up to 16 absorbs it either way.
    // The size walk below would pass a swap of
    // two same-size adjacent members (iTimeDelta with iFrame, say), which shifts
    // nothing but makes every pack read the wrong 4 bytes for both, with no
    // build error. Identity has to be pinned, not just the total.
    const QVector<Member> expectedHead = {
        {QStringLiteral("mat4"), QStringLiteral("qt_Matrix"), 0},
        {QStringLiteral("float"), QStringLiteral("qt_Opacity"), 0},
        {QStringLiteral("float"), QStringLiteral("iTime"), 0},
        {QStringLiteral("float"), QStringLiteral("iTimeDelta"), 0},
        {QStringLiteral("int"), QStringLiteral("iFrame"), 0},
        {QStringLiteral("vec2"), QStringLiteral("iResolution"), 0},
        {QStringLiteral("int"), QStringLiteral("zoneCount"), 0},
        {QStringLiteral("int"), QStringLiteral("highlightedCount"), 0},
        {QStringLiteral("vec4"), QStringLiteral("iMouse"), 0},
        {QStringLiteral("vec4"), QStringLiteral("iDate"), 0},
        {QStringLiteral("vec4"), QStringLiteral("customParams"), 8},
        {QStringLiteral("vec4"), QStringLiteral("customColors"), 16},
        {QStringLiteral("vec2"), QStringLiteral("iChannelResolution"), 4},
        {QStringLiteral("int"), QStringLiteral("iAudioSpectrumSize"), 0},
        {QStringLiteral("int"), QStringLiteral("iFlipBufferY"), 0},
        {QStringLiteral("vec2"), QStringLiteral("iTextureResolution"), 4},
        {QStringLiteral("float"), QStringLiteral("iTimeHi"), 0},
    };

    const QVector<Member> expectedTail = {
        {QStringLiteral("vec4"), QStringLiteral("zoneRects"), MaxZones},
        {QStringLiteral("vec4"), QStringLiteral("zoneFillColors"), MaxZones},
        {QStringLiteral("vec4"), QStringLiteral("zoneBorderColors"), MaxZones},
        {QStringLiteral("vec4"), QStringLiteral("zoneParams"), MaxZones},
        {QStringLiteral("float"), QStringLiteral("uZoneScale"), 0},
    };

    int tailStart = -1;
    for (int i = 0; i < members.size(); ++i) {
        if (members[i].name == QLatin1String("zoneRects")) {
            tailStart = i;
            break;
        }
    }
    QVERIFY2(tailStart >= 0, "zoneRects not found in the ZoneUniforms block");

    QCOMPARE(tailStart, expectedHead.size());
    for (int i = 0; i < expectedHead.size(); ++i) {
        const Member& actualMember = members[i];
        QCOMPARE(actualMember.type, expectedHead[i].type);
        QCOMPARE(actualMember.name, expectedHead[i].name);
        QCOMPARE(actualMember.arrayLen, expectedHead[i].arrayLen);
    }

    // Exactly the expected tail, and nothing after it. A member appended past
    // uZoneScale would land in the 12 std140 pad bytes the C++ struct declares
    // as _pad_after_zoneScale, silently reading garbage.
    QCOMPARE(members.size() - tailStart, expectedTail.size());
    for (int i = 0; i < expectedTail.size(); ++i) {
        const Member& actualMember = members[tailStart + i];
        QCOMPARE(actualMember.type, expectedTail[i].type);
        QCOMPARE(actualMember.name, expectedTail[i].name);
        // Catches MaxZones drifting away from the hard-coded [64] in the GLSL.
        QCOMPARE(actualMember.arrayLen, expectedTail[i].arrayLen);
    }

    // Walk the head region with the real std140 rules and confirm the zone
    // arrays start exactly where BaseUniforms ends. This is what catches a base
    // member being added, dropped or reordered.
    int headSize = 0;
    for (int i = 0; i < tailStart; ++i) {
        const int advance = std140Advance(members[i], headSize);
        QVERIFY2(advance > 0,
                 qPrintable(
                     QStringLiteral("unhandled GLSL type '%1' for member '%2'").arg(members[i].type, members[i].name)));
        headSize += advance;
    }
    // The base block itself closes on a 16-byte boundary before the zone arrays
    // begin, and the zone arrays are 16-aligned anyway.
    headSize = (headSize + 15) / 16 * 16;
    QCOMPARE(headSize, static_cast<int>(sizeof(PhosphorShaders::BaseUniforms)));

    // Independently recompute the std140 size of the extension region from the
    // parsed declaration and check it against what the C++ side reports.
    int glslSize = 0;
    for (int i = tailStart; i < members.size(); ++i) {
        const int advance = std140Advance(members[i], glslSize);
        QVERIFY2(advance > 0,
                 qPrintable(
                     QStringLiteral("unhandled GLSL type '%1' for member '%2'").arg(members[i].type, members[i].name)));
        glslSize += advance;
    }
    glslSize = (glslSize + 15) / 16 * 16;

    ZoneUniformExtension ext;
    // No literal 4112 here: the compare above derives it from the parsed
    // GLSL, extensionSize_matchesZoneShaderUniformsRegion derives it from
    // MaxZones, and ZoneShaderCommon.h static_asserts it at compile time. A
    // fourth hard-code would fail a deliberate, consistent MaxZones change
    // on both sides for no defect.
    QCOMPARE(ext.extensionSize(), glslSize);
}

void TestZoneUniformExtension::scaleHelpers_matchTheirDocumentedGuards()
{
    // layout_matchesGlslUboDeclaration() pins the UBO BLOCK. Nothing pinned the
    // helper BODIES, and this PR's headline claims rest on three of them: the
    // corner-radius clamp (a radius larger than half a narrow zone used to
    // collapse that zone into a sliver), zoneBorderWidth's zero-versus-floor
    // split (width 0 means the border is off, not one pixel), and zoneLen's
    // uZoneScale multiply (the whole point of the scale uniform). Deleting any
    // of the three left the entire suite green, so a changelog guarantee had no
    // test that could fail on it.
    //
    // Source-level assertions, not a GPU render: the packs are validated
    // separately by shader_validate_bundled, and what is at risk here is a
    // careless edit to the shared prologue rather than a compile failure.
    QFile glsl(QStringLiteral(P_SOURCE_DIR "/data/overlays/shared/common.glsl"));
    if (!glsl.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QFAIL(qPrintable(glsl.errorString()));
    }
    const QString source = QString::fromUtf8(glsl.readAll());

    // Comments stripped FIRST, then all whitespace. Stripping whitespace alone
    // would let an assertion match text inside a comment that merely quotes the
    // expression, which is exactly how a doc block could keep this test green
    // after the code it describes was deleted.
    QString compact = source;
    compact.remove(QRegularExpression(QStringLiteral("/\\*.*?\\*/"), QRegularExpression::DotMatchesEverythingOption));
    compact.remove(QRegularExpression(QStringLiteral("//[^\n]*")));
    compact.remove(QRegularExpression(QStringLiteral("\\s+")));

    // zoneSdf: the radius is scaled AND clamped to the zone's smaller half-side.
    // Without the upper bound, sdRoundedBox is evaluated with a radius larger
    // than the box and the zone renders as a sliver.
    QVERIFY2(
        compact.contains(QStringLiteral("z.radius=clamp(radiusLogical*uZoneScale,0.0,min(z.halfSize.x,z.halfSize.y))")),
        "zoneSdf() must clamp the scaled corner radius to min(halfSize.x, halfSize.y)");

    // zoneBorderWidth: 0 means OFF and must return 0; anything else is floored
    // at one device pixel so a thin border cannot flicker out on a fractional
    // scale. Collapsing the two arms in either direction makes one end of the
    // configured range unreachable.
    QVERIFY2(compact.contains(QStringLiteral("returnwidthLogical<=0.0?0.0:max(widthLogical*uZoneScale,1.0)")),
             "zoneBorderWidth() must return 0 for a width of 0 and floor any other width at 1 device px");

    // zoneLen: the plain logical-to-device multiply every other zone-edge length
    // in the catalog goes through. Returning logicalPx unscaled compiles, renders
    // and is wrong on every HiDPI display.
    QVERIFY2(compact.contains(QStringLiteral("floatzoneLen(floatlogicalPx){returnlogicalPx*uZoneScale;}")),
             "zoneLen() must multiply by uZoneScale");

    // zoneStrokeWidth and zoneEdgeBand are the derived pair; both exist so a
    // pack that scales a border down does not go sub-pixel.
    QVERIFY2(compact.contains(QStringLiteral("returndeviceWidth<=0.0?0.0:max(deviceWidth,1.0)")),
             "zoneStrokeWidth() must preserve 0 and floor anything else at 1 device px");
    QVERIFY2(compact.contains(QStringLiteral("returnmax(deviceWidth,zoneLen(minLogical))")),
             "zoneEdgeBand() must floor a derived band at its logical minimum");

    // zoneFillHue's epsilon: zoneFillColors[i].rgb is premultiplied, so
    // un-premultiplying a fully transparent fill is 0/0. The guard is what keeps
    // a NaN out of the fragment colour.
    QVERIFY2(compact.contains(QStringLiteral("returnfillColor.a>1e-3?fillColor.rgb/fillColor.a:vec3(1.0)")),
             "zoneFillHue() must guard the un-premultiply against a zero alpha");
}

QTEST_APPLESS_MAIN(TestZoneUniformExtension)
#include "test_zone_uniform_extension.moc"
