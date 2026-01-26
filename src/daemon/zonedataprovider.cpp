// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "zonedataprovider.h"
#include <QMutexLocker>
#include <QVariantMap>
#include <QDebug>
#include <cmath>

namespace PlasmaZones {

static constexpr int ZONE_COLS = 32;
// Texture layout: 32 columns (zones) x 6 rows
// Row 0: x_lo, x_hi, y_lo, y_hi (16-bit pixel coordinates)
// Row 1: w_lo, w_hi, h_lo, h_hi (16-bit pixel dimensions)
// Row 2: fill color RGBA
// Row 3: border color RGBA
// Row 4: params (borderRadius, borderWidth, isHighlighted, zoneNumber)
// Row 5: reserved
static constexpr int ROW_POS = 0;    // x, y as 16-bit
static constexpr int ROW_SIZE = 1;   // w, h as 16-bit
static constexpr int ROW_FILL = 2;
static constexpr int ROW_BORDER = 3;
static constexpr int ROW_PARAMS = 4;

ZoneDataProvider::ZoneDataProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_image(ZONE_COLS, 6, QImage::Format_RGBA8888)
{
    m_image.fill(0);
}

float ZoneDataProvider::clamp(float v, float lo, float hi)
{
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

void ZoneDataProvider::setPixel(QImage &img, int col, int row, float x, float y, float z, float w)
{
    // For Format_RGBA8888: bytes are R, G, B, A in memory order
    // This ensures shader's texture().rgba maps correctly to our x, y, z, w values
    uchar *line = img.scanLine(row);
    int offset = col * 4; // 4 bytes per pixel
    line[offset + 0] = static_cast<uchar>(clamp(x, 0.f, 1.f) * 255.f); // R
    line[offset + 1] = static_cast<uchar>(clamp(y, 0.f, 1.f) * 255.f); // G
    line[offset + 2] = static_cast<uchar>(clamp(z, 0.f, 1.f) * 255.f); // B
    line[offset + 3] = static_cast<uchar>(clamp(w, 0.f, 1.f) * 255.f); // A
}

// Encode two 16-bit pixel values into RGBA (R,G = first value, B,A = second value)
void ZoneDataProvider::setPixel16(QImage &img, int col, int row, int val1, int val2)
{
    Q_ASSERT(col >= 0 && col < img.width());
    Q_ASSERT(row >= 0 && row < img.height());
    Q_ASSERT(val1 >= 0 && val1 <= 65535);
    Q_ASSERT(val2 >= 0 && val2 <= 65535);

    if (col < 0 || col >= img.width() || row < 0 || row >= img.height()) {
        qWarning() << "ZoneDataProvider::setPixel16: out of bounds col=" << col << "row=" << row;
        return;
    }

    uchar *line = img.scanLine(row);
    int offset = col * 4;
    // val1 encoded as R (low byte) + G (high byte)
    line[offset + 0] = static_cast<uchar>(val1 & 0xFF);        // R = val1 low
    line[offset + 1] = static_cast<uchar>((val1 >> 8) & 0xFF); // G = val1 high
    // val2 encoded as B (low byte) + A (high byte)
    line[offset + 2] = static_cast<uchar>(val2 & 0xFF);        // B = val2 low
    line[offset + 3] = static_cast<uchar>((val2 >> 8) & 0xFF); // A = val2 high
}

void ZoneDataProvider::setZones(const QVariantList &zones)
{
    QImage img(ZONE_COLS, 6, QImage::Format_RGBA8888);
    img.fill(0);

    const int n = qMin(zones.size(), ZONE_COLS);
    for (int i = 0; i < n; ++i) {
        const QVariantMap z = zones[i].toMap();

        // Use PIXEL coordinates directly (from x, y, width, height)
        int px = z.value(QLatin1String("x")).toInt();
        int py = z.value(QLatin1String("y")).toInt();
        int pw = z.value(QLatin1String("width")).toInt();
        int ph = z.value(QLatin1String("height")).toInt();
        float fillR = z.value(QLatin1String("fillR")).toFloat();
        float fillA = z.value(QLatin1String("fillA")).toFloat();
        float borderA = z.value(QLatin1String("borderA"), QVariant(1.0)).toFloat();
        qDebug() << "ZoneDataProvider: zone" << i << "pixels:" << px << py << pw << ph
                 << "fillR:" << fillR << "fillA:" << fillA << "borderA:" << borderA;

        // Row 0: position (x, y) as 16-bit values encoded as RGBA bytes
        // Use setPixel with explicit byte extraction (setPixel16 may have issues)
        float r0 = float(px & 0xFF) / 255.f;
        float g0 = float((px >> 8) & 0xFF) / 255.f;
        float b0 = float(py & 0xFF) / 255.f;
        float a0 = float((py >> 8) & 0xFF) / 255.f;
        setPixel(img, i, ROW_POS, r0, g0, b0, a0);
        
        // Row 1: size (width, height) as 16-bit values encoded as RGBA bytes
        float r1 = float(pw & 0xFF) / 255.f;
        float g1 = float((pw >> 8) & 0xFF) / 255.f;
        float b1 = float(ph & 0xFF) / 255.f;
        float a1 = float((ph >> 8) & 0xFF) / 255.f;
        setPixel(img, i, ROW_SIZE, r1, g1, b1, a1);
        
        qDebug() << "ZoneDataProvider: row1 bytes: R=" << int(r1*255) << "G=" << int(g1*255) 
                 << "B=" << int(b1*255) << "A=" << int(a1*255);

        // Row 2: fill color (premultiplied RGBA 0-1)
        setPixel(img, i, ROW_FILL,
                 z.value(QLatin1String("fillR")).toFloat(),
                 z.value(QLatin1String("fillG")).toFloat(),
                 z.value(QLatin1String("fillB")).toFloat(),
                 z.value(QLatin1String("fillA")).toFloat());

        // Row 3: border color (RGBA 0-1)
        setPixel(img, i, ROW_BORDER,
                 z.value(QLatin1String("borderR")).toFloat(),
                 z.value(QLatin1String("borderG")).toFloat(),
                 z.value(QLatin1String("borderB")).toFloat(),
                 z.value(QLatin1String("borderA"), QVariant(1.0)).toFloat());

        // Row 4: params (borderRadius, borderWidth as pixels / 255, isHighlighted, zoneNumber / 255)
        const float isH = z.value(QLatin1String("isHighlighted")).toBool() ? 1.f : 0.f;
        setPixel(img, i, ROW_PARAMS,
                 z.value(QLatin1String("shaderBorderRadius"), QVariant(8.0)).toFloat() / 255.f,
                 z.value(QLatin1String("shaderBorderWidth"), QVariant(2.0)).toFloat() / 255.f,
                 isH,
                 z.value(QLatin1String("zoneNumber"), QVariant(1.0)).toFloat() / 255.f);
    }

    QMutexLocker lock(&m_mutex);
    m_image = img;
}

QImage ZoneDataProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id)
    Q_UNUSED(requestedSize)
    QMutexLocker lock(&m_mutex);
    if (size)
        *size = m_image.size();
    return m_image;
}

} // namespace PlasmaZones
