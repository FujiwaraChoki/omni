#pragma once

#include "keyboard.h"

#include <QMap>
#include <QPair>

// SteelSeries Apex Pro TKL Gen 3 (wired, 1038:1642), over USB interface 1.
// The protocol notes and their sources are in docs/protocol-steelseries-apex-gen3.md.
class ApexDriver : public Keyboard {
public:
    explicit ApexDriver(const QList<HidNode> &nodes);

    static bool matches(const HidNode &node);

    QString model() const override { return QStringLiteral("Apex Pro TKL Gen 3"); }
    QString identity() const override;
    QString firmware() const override { return m_firmware; }
    QList<KeyGeometry> layout() const override;
    bool isAnalog(const QString &code) const override;
    Capabilities capabilities() const override;

    bool open(QString *error) override;
    bool read(Settings *settings, QString *error) override;
    bool write(const Settings &settings, int changes, QString *error) override;
    bool restoreLighting(QString *error) override;

    // Millimetres to the board's raw hall-sensor pair (actuation, reset) and back.
    static QPair<quint8, quint8> actuationRaw(double mm);
    static double actuationMm(quint8 actuation, quint8 reset);
    // Every frame below starts with the report ID byte. Anything outside the
    // documented safe set is refused before it reaches the keyboard.
    static bool allowed(const QByteArray &frame, bool feature);

private:
    bool output(const QByteArray &frame, QByteArray *answer, QString *error);
    bool feature(const QByteArray &frame, QString *error);
    bool readTable(quint8 table, quint8 layer, QByteArray *entries, QString *error);

    HidNode m_node;
    HidDevice m_device;
    QString m_firmware;
    Region m_region = Region::Ansi;
    bool m_directMode = false;
    // Raw actuation pairs as last read, so keys the layout leaves out are sent back unchanged.
    QMap<quint8, QPair<quint8, quint8>> m_rawActuation;
};
