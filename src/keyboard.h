#pragma once

#include <QColor>
#include <QImage>
#include <QSize>
#include <QHash>
#include <QList>
#include <QString>

#include "hidraw.h"
#include "layout.h"

// What one key is set to. Distances are millimetres of key travel.
struct KeySettings {
    double actuation = 1.8;
    bool rapidTrigger = false;
    double press = 0.2;   // Rapid Trigger: travel down that presses again
    double release = 0.2; // Rapid Trigger: travel up that releases
    QColor color;         // invalid: the keyboard's own lighting

    bool operator==(const KeySettings &o) const {
        return qFuzzyCompare(actuation, o.actuation) && rapidTrigger == o.rapidTrigger
            && qFuzzyCompare(press, o.press) && qFuzzyCompare(release, o.release) && color == o.color;
    }
    bool operator!=(const KeySettings &o) const { return !(*this == o); }
};

using Settings = QHash<QString, KeySettings>; // by key code

struct Range {
    double min = 0.1, max = 4.0, step = 0.1;
    QList<double> stops; // when set, the only values the keyboard has
    double snap(double v) const;
};

struct Capabilities {
    Range actuation;
    Range sensitivity;
    bool separateRelease = false; // Rapid Trigger press and release can differ
    bool perKeyColor = true;
};

enum Change { ActuationChange = 1, RapidTriggerChange = 2, ColorChange = 4, AllChanges = 7 };

// One connected keyboard. A driver owns its hidraw handles and speaks its
// vendor's protocol; everything above it only sees key codes and millimetres.
class Keyboard {
public:
    virtual ~Keyboard() = default;

    virtual QString model() const = 0;     // shown to the user
    virtual QString identity() const = 0;  // stable across replugs, names the saved settings
    virtual QString firmware() const { return {}; }
    virtual QList<KeyGeometry> layout() const = 0;
    virtual bool isAnalog(const QString &code) const = 0;
    virtual Capabilities capabilities() const = 0;

    // Opens the device and reads what it needs to describe itself.
    virtual bool open(QString *error) = 0;
    // The settings the keyboard holds now, for every key in layout().
    virtual bool read(Settings *settings, QString *error) = 0;
    // Sends the parts named in `changes` for every key.
    virtual bool write(const Settings &settings, int changes, QString *error) = 0;
    // Makes what write() sent survive a replug, for keyboards where that is a
    // separate step. The backend calls it once edits have settled, so a slider
    // drag does not wear the keyboard's flash.
    virtual bool commit(QString *error) { Q_UNUSED(error); return true; }
    // Hands lighting back to the keyboard's own effects.
    virtual bool restoreLighting(QString *error) = 0;

    // A small monochrome screen, if the keyboard has one: its size in pixels,
    // showing a picture (white pixels light up), and handing it back.
    virtual QSize screenSize() const { return {}; }
    virtual bool showScreen(const QImage &image, QString *error) { Q_UNUSED(image); Q_UNUSED(error); return false; }
    virtual bool releaseScreen(QString *error) { Q_UNUSED(error); return true; }

    // The hidraw nodes that carry the keyboard's own key reports, which
    // reactive lighting listens to. Only opened while such an effect is on.
    virtual QList<HidNode> keyInputs() const { return m_inputs; }
    void setNodes(const QList<HidNode> &nodes) {
        m_inputs.clear();
        for (const auto &n : nodes)
            if (n.usagePage == 0x01 && n.keyboard)
                m_inputs << n;
    }

private:
    QList<HidNode> m_inputs;
};

// How a driver says which devices it handles and builds itself from the
// hidraw nodes of one physical keyboard.
struct Driver {
    QString name;
    bool (*matches)(const HidNode &node);
    Keyboard *(*create)(const QList<HidNode> &nodes);
};

const QList<Driver> &drivers();
