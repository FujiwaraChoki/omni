#pragma once

#include "keyboard.h"

#include <QMap>

// Wooting 60HE v2 (31E3:134x), over the 0xFF55 "V3" configuration interface.
// The protocol notes and their sources are in docs/protocol-wooting-60he-v2.md.
class WootingDriver : public Keyboard {
public:
    explicit WootingDriver(const QList<HidNode> &nodes);

    static bool matches(const HidNode &node);

    QString model() const override { return QStringLiteral("Wooting 60HE v2"); }
    QString identity() const override;
    QString firmware() const override { return m_firmware; }
    QList<KeyGeometry> layout() const override;
    bool isAnalog(const QString &code) const override;
    Capabilities capabilities() const override;

    bool open(QString *error) override;
    bool read(Settings *settings, QString *error) override;
    bool write(const Settings &settings, int changes, QString *error) override;
    bool commit(QString *error) override;
    bool restoreLighting(QString *error) override;

    // Protocol pieces, public for the tests.
    static quint32 analogValue(double mm);   // mm -> 14-bit wire value
    static double analogMm(quint32 wire);     // wire value (8- or 14-bit) -> mm
    static quint16 rgb565(const QColor &c);
    static int matrixId(const QString &code, Region region); // -1 when the key has none
    static QByteArray command(quint8 cmd, quint32 param);
    static QByteArray outputReport(quint8 type, const QByteArray &body); // report ID + padding included
    static QByteArray profileBody(quint8 flags, const QByteArray &message);
    // The three messages a settings write sends, from per-key settings.
    struct Messages { QByteArray keyboardProfile, actuationProfile, rapidTriggerProfile; };
    static Messages encode(const Settings &settings, Region region, const QByteArray &previousBase);
    static bool allowedCommand(quint8 cmd);
    static bool allowedOutput(quint8 type);

private:
    bool ask(quint8 cmd, quint32 param, QByteArray *body, QString *error);
    bool send(quint8 type, const QByteArray &body, QString *error);
    bool writeSettings(quint8 flags, QString *error);

    HidNode m_node;
    HidDevice m_device;
    QString m_firmware;
    QString m_serial;
    Region m_region = Region::Ansi;
    int m_slot = 0;
    bool m_directMode = false;
    QByteArray m_base;      // KBase as read, so fields omni does not edit go back unchanged
    Messages m_pending;     // last live write, saved to the slot on commit()
    bool m_dirty = false;
};
