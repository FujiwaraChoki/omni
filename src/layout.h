#pragma once

#include <QList>
#include <QString>

// A key drawn on screen. Positions and sizes are in key units (1u = one
// alphanumeric key pitch). `code` is the layout-neutral name the web uses for
// physical keys (KeyboardEvent.code: "KeyA", "ShiftLeft", "IntlBackslash"),
// which every driver maps to its own key numbering.
struct KeyGeometry {
    QString code;
    QString label;
    double x = 0, y = 0, w = 1, h = 1;
};

enum class Form { Tkl, Sixty };
enum class Region { Ansi, Iso };

// The physical key layout of a board shape, built from the standard rows.
// `rightModifiers` names the keys between the space bar and the right edge,
// which differ from board to board ("AltRight", "MetaRight", "Fn", "ControlRight").
QList<KeyGeometry> buildLayout(Form form, Region region, const QStringList &rightModifiers);

// The HID keyboard usage for a code, or 0. SteelSeries addresses keys by
// usage; the vendor Fn key is 0xF0 there and has no standard usage.
quint8 hidUsage(const QString &code);

// The legend printed on a US keycap for a code.
QString keyLabel(const QString &code);
