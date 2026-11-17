#include "screen.h"

#include <QImageReader>
#include <QPainter>
#include <vector>

QJsonObject ScreenSettings::toJson() const {
    return {{"file", file}, {"name", name}, {"fit", fit}, {"dither", dither}, {"invert", invert}};
}

ScreenSettings ScreenSettings::fromJson(const QJsonObject &o) {
    ScreenSettings s;
    s.file = o.value("file").toString();
    s.name = o.value("name").toString();
    s.fit = o.value("fit").toString(s.fit);
    s.dither = o.value("dither").toBool(s.dither);
    s.invert = o.value("invert").toBool(s.invert);
    return s;
}

QImage ScreenFrames::toMono(const QImage &source, QSize size, const ScreenSettings &settings) {
    // Composite onto black first, so transparent GIF pixels stay dark.
    QImage canvas(size, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    if (!source.isNull()) {
        const Qt::AspectRatioMode mode = settings.fit == QLatin1String("fill") ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio;
        const QImage scaled = source.scaled(size, mode, Qt::SmoothTransformation);
        QPainter p(&canvas);
        p.drawImage((size.width() - scaled.width()) / 2, (size.height() - scaled.height()) / 2, scaled);
    }
    const QImage gray = canvas.convertToFormat(QImage::Format_Grayscale8);
    const int w = size.width(), h = size.height();
    std::vector<float> level(size_t(w * h));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float v = gray.constScanLine(y)[x];
            level[size_t(y * w + x)] = settings.invert ? 255 - v : v;
        }

    QImage out(size, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar *row = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const float old = level[size_t(y * w + x)];
            const float now = old > 127 ? 255 : 0;
            row[x] = uchar(now);
            if (!settings.dither)
                continue;
            // Floyd–Steinberg: push the rounding error onto the pixels not yet drawn.
            const float err = old - now;
            auto push = [&](int dx, int dy, float k) {
                const int nx = x + dx, ny = y + dy;
                if (nx >= 0 && nx < w && ny < h)
                    level[size_t(ny * w + nx)] += err * k;
            };
            push(1, 0, 7 / 16.f);
            push(-1, 1, 3 / 16.f);
            push(0, 1, 5 / 16.f);
            push(1, 1, 1 / 16.f);
        }
    }
    return out;
}

bool ScreenFrames::load(const QString &file, const ScreenSettings &settings, QSize size, QString *error) {
    m_frames.clear();
    m_delays.clear();
    m_total = 0;
    QImageReader reader(file);
    reader.setAutoTransform(true);
    if (!reader.canRead()) {
        if (error) *error = reader.errorString();
        return false;
    }
    while (m_frames.size() < 600) {
        const QImage image = reader.read();
        if (image.isNull())
            break;
        m_frames << toMono(image, size, settings);
        // GIFs that ask for no delay play at 10 fps, as browsers do; the screen tops out near 30.
        const int delay = reader.nextImageDelay();
        m_delays << (delay <= 10 ? 100 : qMax(33, delay));
        m_total += m_delays.last();
        if (!reader.supportsAnimation() || !reader.jumpToNextImage())
            break;
    }
    if (m_frames.isEmpty()) {
        if (error) *error = reader.errorString();
        return false;
    }
    return true;
}

int ScreenFrames::frameAt(qint64 ms) const {
    if (m_frames.size() <= 1 || m_total <= 0)
        return 0;
    qint64 t = ms % m_total;
    for (int i = 0; i < m_delays.size(); ++i) {
        if (t < m_delays[i])
            return i;
        t -= m_delays[i];
    }
    return int(m_frames.size()) - 1;
}
