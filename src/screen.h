#pragma once

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>

// What the keyboard's screen should show.
struct ScreenSettings {
    QString file;                         // a GIF or still image omni keeps a copy of; empty = the keyboard's own
    QString fit = QStringLiteral("fit");  // fit (whole picture, black bars) or fill (cropped to the screen)
    bool dither = true;                   // error diffusion for photos; off = plain threshold for line art
    bool invert = false;

    bool active() const { return !file.isEmpty(); }
    QJsonObject toJson() const;
    static ScreenSettings fromJson(const QJsonObject &o);
    bool operator==(const ScreenSettings &o) const { return toJson() == o.toJson(); }
};

// The frames of a picture, ready for a one-bit screen.
class ScreenFrames {
public:
    // Reads every frame (a GIF is capped at 600) and converts it once.
    bool load(const QString &file, const ScreenSettings &settings, QSize size, QString *error);
    bool isEmpty() const { return m_frames.isEmpty(); }
    int count() const { return int(m_frames.size()); }
    const QImage &frame(int index) const { return m_frames[index]; }
    int delay(int index) const { return m_delays[index]; } // milliseconds
    // The frame showing `ms` milliseconds into a loop of the animation.
    int frameAt(qint64 ms) const;

    // A picture scaled to `size` and reduced to black and white (Grayscale8, 0 or 255).
    static QImage toMono(const QImage &source, QSize size, const ScreenSettings &settings);

private:
    QList<QImage> m_frames;
    QList<int> m_delays;
    int m_total = 0;
};
