#include "apexdriver.h"

#include <QThread>
#include <cmath>

namespace {

constexpr quint16 kVendor = 0x1038;
constexpr quint16 kProduct = 0x1642;
constexpr int kConfigInterface = 1;
constexpr int kOutputSize = 65;  // report ID + 64
constexpr int kFeatureSize = 645; // report ID + 644, the descriptor's size

// The 68 keys with OmniPoint switches, by HID usage, in the order every 38 6x
// frame lists them. Usages a layout lacks (ISO, JIS) are still sent.
const quint8 kAnalog[] = {
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x32, 0x33, 0x34,
    0x35, 0x36, 0x37, 0x38, 0x39, 0x64, 0x87, 0x88, 0x89, 0x8A, 0x8B, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4,
    0xE5, 0xE6, 0xE7, 0xF0,
};
constexpr int kAnalogCount = int(sizeof kAnalog);

// Raw (actuation, reset) hall values for 0.1 mm .. 4.0 mm. PROVISIONAL: this
// is the Apex Pro TKL 2023 table; it is replaced by values measured on the
// Gen 3 from its own OLED menu before any actuation write is enabled.
const quint8 kActuation[40][2] = {
    {5, 5},     {5, 4},     {6, 5},     {8, 6},     {10, 8},    {12, 10},   {14, 12},   {16, 14},
    {18, 16},   {21, 18},   {24, 21},   {27, 24},   {30, 27},   {33, 29},   {36, 33},   {39, 36},
    {43, 40},   {47, 42},   {52, 47},   {57, 52},   {62, 57},   {68, 62},   {74, 68},   {81, 74},
    {88, 81},   {95, 87},   {104, 95},  {114, 104}, {124, 114}, {134, 123}, {147, 134}, {161, 147},
    {175, 161}, {189, 174}, {197, 189}, {206, 197}, {215, 206}, {224, 215}, {224, 217}, {224, 219},
};

// Off until kActuation holds this board's own values, so a wrong table can
// never set the wrong travel.
constexpr bool kActuationVerified = false;

// The full LED list OpenRGB sends to this board (HID usages, plus 0xF0 Fn and 0xFB media key).
const quint8 kLeds[] = {
    0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F, 0x30, 0x32, 0x33, 0x34,
    0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F, 0x40, 0x41, 0x42, 0x43, 0x44,
    0x45, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50, 0x51, 0x52, 0x64, 0xE0, 0xE1, 0xE2, 0xE3,
    0xE4, 0xE5, 0xE6, 0xE7, 0xF0, 0x31, 0xFB,
};

QByteArray frame(std::initializer_list<int> bytes, int size) {
    QByteArray f(size, 0);
    int i = 0;
    for (int b : bytes)
        f[i++] = char(b);
    return f;
}

void pace() {
    // The board wants about 13 ms between commands (GG's own pacing).
    QThread::msleep(13);
}

} // namespace

bool ApexDriver::matches(const HidNode &node) {
    return node.vendor == kVendor && node.product == kProduct;
}

ApexDriver::ApexDriver(const QList<HidNode> &nodes) {
    for (const auto &node : nodes)
        if (node.interface == kConfigInterface)
            m_node = node;
}

QString ApexDriver::identity() const {
    return QStringLiteral("1038:1642") + (m_node.serial.isEmpty() ? QString() : ":" + m_node.serial);
}

QList<KeyGeometry> ApexDriver::layout() const {
    return buildLayout(Form::Tkl, m_region, {"AltRight", "MetaRight", "Fn", "ControlRight"});
}

bool ApexDriver::isAnalog(const QString &code) const {
    const quint8 usage = hidUsage(code);
    return usage && std::find(kAnalog, kAnalog + kAnalogCount, usage) != kAnalog + kAnalogCount;
}

Capabilities ApexDriver::capabilities() const {
    Capabilities c;
    c.actuation = {0.1, 4.0, 0.1};
    c.sensitivity = {0.1, 1.0, 0.1};
    c.separateRelease = false;
    c.perKeyColor = true;
    return c;
}

bool ApexDriver::allowed(const QByteArray &f, bool feature) {
    if (f.size() < 2 || f[0] != 0)
        return false;
    const quint8 cmd = quint8(f[1]);
    const quint8 sub = f.size() > 2 ? quint8(f[2]) : 0;
    if (feature) {
        switch (cmd) {
        case 0x40: return quint8(f[2]) != 0xFF; // direct lighting, never the untested fill form
        case 0x4B: return true;                  // lighting init
        case 0x38: return sub == 0x61 || sub == 0x62 || sub == 0x65;
        case 0xB8: return sub == 0x61 || sub == 0x62 || sub == 0x65; // reads of the same tables
        default: return false;
        }
    }
    switch (cmd) {
    case 0x41: return true;                // back to the keyboard's own lighting
    case 0x90: return sub == 0x00 || sub == 0x01; // firmware version, build id
    case 0xF2: case 0xF5: return true;     // layout and region reads
    default: return false;
    }
}

bool ApexDriver::output(const QByteArray &f, QByteArray *answer, QString *error) {
    if (!allowed(f, false)) {
        if (error) *error = QStringLiteral("refused unknown command %1").arg(quint8(f[1]), 2, 16, QChar('0'));
        return false;
    }
    if (answer)
        m_device.drain();
    if (!m_device.write(f, error))
        return false;
    pace();
    if (!answer)
        return true;
    // Answers echo the command byte; anything else is a stale report.
    for (int tries = 0; tries < 10; ++tries) {
        const QByteArray reply = m_device.read(200);
        if (reply.isEmpty())
            break;
        if (quint8(reply[0]) == quint8(f[1])) {
            *answer = reply;
            return true;
        }
    }
    if (error) *error = QStringLiteral("the keyboard did not answer");
    return false;
}

bool ApexDriver::feature(const QByteArray &f, QString *error) {
    if (!allowed(f, true)) {
        if (error) *error = QStringLiteral("refused unknown command %1").arg(quint8(f[1]), 2, 16, QChar('0'));
        return false;
    }
    const bool ok = m_device.setFeature(f, error);
    pace();
    return ok;
}

bool ApexDriver::open(QString *error) {
    if (m_node.path.isEmpty()) {
        if (error) *error = QStringLiteral("no configuration interface");
        return false;
    }
    if (!m_device.open(m_node.path, error))
        return false;
    QByteArray answer;
    if (!output(frame({0x00, 0x90, 0x00}, kOutputSize), &answer, error))
        return false;
    m_firmware = QString::fromLatin1(answer.mid(2).split('\0').value(0));
    // Layout: 0 US, 1 European, 3 Japanese. Only ANSI and ISO are drawn.
    if (output(frame({0x00, 0xF2}, kOutputSize), &answer, nullptr) && answer.size() > 2 && answer[1] == 0)
        m_region = answer[2] == 0 ? Region::Ansi : Region::Iso;
    return true;
}

bool ApexDriver::readTable(quint8 table, quint8 layer, QByteArray *entries, QString *error) {
    // B8 <table> asks for the live values: the request names every key, the
    // answer (a feature read) fills in each entry.
    const int width = table == 0x61 ? 3 : 2;
    QByteArray request = frame({0x00, 0xB8, table, kAnalogCount}, kFeatureSize);
    int at = 4;
    if (table == 0x61)
        request[at++] = char(layer);
    for (quint8 usage : kAnalog) {
        request[at] = char(usage);
        at += width;
    }
    if (!feature(request, error))
        return false;
    const QByteArray reply = m_device.getFeature(0, kFeatureSize, error);
    // [00] B8 00 <table> <count> [layer] entries...
    const int header = table == 0x61 ? 6 : 5;
    if (reply.size() < header + kAnalogCount * width || quint8(reply[1]) != 0xB8 || reply[2] != 0 || quint8(reply[3]) != table) {
        if (error && error->isEmpty()) *error = QStringLiteral("unexpected answer from the keyboard");
        return false;
    }
    *entries = reply.mid(header, kAnalogCount * width);
    return true;
}

QPair<quint8, quint8> ApexDriver::actuationRaw(double mm) {
    const int index = qBound(0, int(std::lround(mm * 10)) - 1, 39);
    return {kActuation[index][0], kActuation[index][1]};
}

double ApexDriver::actuationMm(quint8 actuation, quint8 reset) {
    // The nearest entry: exact for values this app or the OLED menu wrote.
    int best = 0, bestDistance = INT_MAX;
    for (int i = 0; i < 40; ++i) {
        const int d = 4 * std::abs(kActuation[i][0] - actuation) + std::abs(kActuation[i][1] - reset);
        if (d < bestDistance) {
            best = i;
            bestDistance = d;
        }
    }
    return (best + 1) / 10.0;
}

bool ApexDriver::read(Settings *settings, QString *error) {
    QByteArray actuation, mode, sensitivity;
    if (!readTable(0x61, 0, &actuation, error) || !readTable(0x62, 0, &mode, error) || !readTable(0x65, 0, &sensitivity, error))
        return false;
    QHash<quint8, KeySettings> byUsage;
    m_rawActuation.clear();
    for (int i = 0; i < kAnalogCount; ++i) {
        const quint8 usage = quint8(actuation[3 * i]);
        KeySettings &k = byUsage[usage];
        const quint8 h = quint8(actuation[3 * i + 1]), l = quint8(actuation[3 * i + 2]);
        m_rawActuation[usage] = {h, l};
        k.actuation = actuationMm(h, l);
        k.rapidTrigger = quint8(mode[2 * i + 1]) == 0x02;
        k.press = k.release = qBound(1, int(quint8(sensitivity[2 * i + 1])), 40) / 10.0;
    }
    settings->clear();
    for (const auto &key : layout())
        settings->insert(key.code, byUsage.value(hidUsage(key.code)));
    return true;
}

bool ApexDriver::write(const Settings &settings, int changes, QString *error) {
    QHash<quint8, KeySettings> byUsage;
    for (auto it = settings.begin(); it != settings.end(); ++it)
        if (const quint8 usage = hidUsage(it.key()))
            byUsage.insert(usage, it.value());

    if ((changes & ActuationChange) && !kActuationVerified) {
        if (error) *error = QStringLiteral("actuation for this keyboard is not calibrated yet");
        return false;
    }
    if (changes & ActuationChange) {
        QByteArray f = frame({0x00, 0x38, 0x61, kAnalogCount, 0x00}, kFeatureSize);
        for (int i = 0; i < kAnalogCount; ++i) {
            const quint8 usage = kAnalog[i];
            const auto raw = byUsage.contains(usage) ? actuationRaw(byUsage[usage].actuation)
                                                     : m_rawActuation.value(usage, actuationRaw(1.8));
            m_rawActuation[usage] = raw;
            f[5 + 3 * i] = char(usage);
            f[6 + 3 * i] = char(raw.first);
            f[7 + 3 * i] = char(raw.second);
        }
        if (!feature(f, error))
            return false;
    }
    if (changes & RapidTriggerChange) {
        QByteArray modes = frame({0x00, 0x38, 0x62, kAnalogCount}, kFeatureSize);
        QByteArray sensitivity = frame({0x00, 0x38, 0x65, kAnalogCount}, kFeatureSize);
        for (int i = 0; i < kAnalogCount; ++i) {
            const quint8 usage = kAnalog[i];
            const KeySettings k = byUsage.value(usage);
            modes[4 + 2 * i] = char(usage);
            modes[5 + 2 * i] = char(k.rapidTrigger ? 0x02 : 0x00);
            sensitivity[4 + 2 * i] = char(usage);
            sensitivity[5 + 2 * i] = char(qBound(1, int(std::lround(k.press * 10)), 40));
        }
        if (!feature(sensitivity, error) || !feature(modes, error))
            return false;
    }
    if (changes & ColorChange) {
        bool any = false;
        for (const auto &k : settings)
            any = any || k.color.isValid();
        if (!any)
            return restoreLighting(error);
        if (!m_directMode) {
            if (!feature(frame({0x00, 0x4B}, kFeatureSize), error))
                return false;
            QThread::msleep(50);
            m_directMode = true;
        }
        const int count = int(sizeof kLeds);
        QByteArray f = frame({0x00, 0x40, count}, kFeatureSize);
        for (int i = 0; i < count; ++i) {
            const QColor c = byUsage.value(kLeds[i]).color;
            f[3 + 4 * i] = char(kLeds[i]);
            f[4 + 4 * i] = char(c.isValid() ? c.red() : 0);
            f[5 + 4 * i] = char(c.isValid() ? c.green() : 0);
            f[6 + 4 * i] = char(c.isValid() ? c.blue() : 0);
        }
        if (!feature(f, error))
            return false;
    }
    return true;
}

bool ApexDriver::restoreLighting(QString *error) {
    m_directMode = false;
    return output(frame({0x00, 0x41}, kOutputSize), nullptr, error);
}
