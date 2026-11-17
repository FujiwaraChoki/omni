#pragma once

#include <QColor>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "layout.h"

// How a keyboard's lighting should look. Stored per keyboard in
// ~/.config/omni/<keyboard>.json and drawn by the lighting service; the window
// draws the same thing for its preview.
struct Lighting {
    // "own" hands lighting back to the keyboard. The others are drawn on the computer.
    QString effect = QStringLiteral("own"); // own static breathing spectrum wave aurora starlight rain
    QString palette = QStringLiteral("theme"); // theme, rainbow, or custom
    QList<QColor> colors;                      // the custom palette
    double speed = 1.0;                        // 0.25 .. 3
    double brightness = 1.0;                   // 0 .. 1
    QString direction = QStringLiteral("right"); // wave: right left up down out
    QString reactive = QStringLiteral("none");   // none fade ripple heatmap
    QHash<QString, QColor> keyColors;          // the static effect's per-key colours

    bool drawn() const { return effect != QLatin1String("own") || reactive != QLatin1String("none"); }
    bool animated() const;
    QJsonObject toJson() const;
    static Lighting fromJson(const QJsonObject &o);
    bool operator==(const Lighting &o) const { return toJson() == o.toJson(); }
};

// The colours an Omarchy theme offers for lighting: its accent first.
QList<QColor> themeColors(const QString &colorsToml);

// Draws lighting frames. Feed it key presses as they happen and ask for the
// colour of every key at a moment in seconds.
class Effects {
public:
    explicit Effects(const QList<KeyGeometry> &layout = {});
    void setLayout(const QList<KeyGeometry> &layout);
    void setLighting(const Lighting &lighting, const QList<QColor> &themePalette);
    const Lighting &lighting() const { return m_lighting; }

    void press(const QString &code, double t);
    QHash<QString, QColor> render(double t);

    // A colour from the palette at position p, wrapping at 1.
    QColor sample(double p) const;

private:
    struct Key { QString code; QPointF center; };
    struct Press { QPointF at; QString code; double t; };

    QColor base(const Key &key, int index, double t) const;
    QColor overlay(const Key &key, double t, const QColor &under) const;

    QList<Key> m_keys;
    QRectF m_bounds;
    Lighting m_lighting;
    QList<QColor> m_palette;
    bool m_rainbow = false;
    QList<Press> m_presses;
    QHash<QString, double> m_heat;
    double m_heatTime = 0;
};
