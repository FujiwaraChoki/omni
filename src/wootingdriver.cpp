#include "wootingdriver.h"

#include <QElapsedTimer>
#include <QVersionNumber>
#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <tuple>

namespace {

constexpr quint16 kVendor = 0x31E3;
constexpr quint16 kModel = 0x1340;      // the low nibble follows the gamepad mode
constexpr quint16 kBootloader = 0x134F; // never opened
constexpr quint16 kConfigPage = 0xFF55;
constexpr quint8 kStatusOk = 0x88;
constexpr int kColumns = 21;            // the RGB and per-key matrices are 6 x 21

// Report IDs 1..6 carry these payload sizes; writes use the smallest that fits.
constexpr int kReportSizes[] = {32, 62, 254, 510, 1022, 2046};

// Feature commands omni may send: reads, live lighting, profile reload and save.
const quint8 kAllowedCommands[] = {0, 1, 3, 8, 11, 19, 30, 31, 32, 33, 38, 39, 49, 51, 55, 61, 62, 72, 73, 74, 82, 83};
// Output report types omni may send: direct RGB, keyboard profile, per-key actuation, per-key Rapid Trigger.
const quint8 kAllowedOutputs[] = {0x0B, 0x11, 0x15, 0x1D};

enum Command : quint8 {
    GetVersion = 1, GetCurrentProfile = 11, GetDeviceConfig = 19, LightingReset = 32, LightingInit = 33,
    GetKeyboardProfile = 39, GetActuationProfile = 49, GetRapidTriggerProfile = 61,
};
enum Output : quint8 { DirectLighting = 0x0B, KeyboardProfile = 0x11, ActuationProfile = 0x15, RapidTriggerProfile = 0x1D };

// --- protobuf, just enough for these messages -------------------------------

void varint(QByteArray &o, quint64 v) {
    do {
        quint8 b = v & 0x7F;
        v >>= 7;
        o += char(v ? b | 0x80 : b);
    } while (v);
}
void putVarint(QByteArray &o, int field, quint64 v) { varint(o, quint64(field) << 3); varint(o, v); }
void putBytes(QByteArray &o, int field, const QByteArray &b) {
    varint(o, quint64(field) << 3 | 2);
    varint(o, quint64(b.size()));
    o += b;
}

struct Field { int number = 0; int wire = 0; quint64 value = 0; QByteArray bytes; };

bool readVarint(const QByteArray &b, int &i, quint64 &v) {
    v = 0;
    for (int shift = 0; i < b.size() && shift < 64; shift += 7) {
        const quint8 c = quint8(b[i++]);
        v |= quint64(c & 0x7F) << shift;
        if (!(c & 0x80))
            return true;
    }
    return false;
}

QList<Field> parse(const QByteArray &b) {
    QList<Field> fields;
    for (int i = 0; i < b.size();) {
        quint64 key;
        if (!readVarint(b, i, key))
            break;
        Field f;
        f.number = int(key >> 3);
        f.wire = int(key & 7);
        if (f.wire == 0) {
            if (!readVarint(b, i, f.value)) break;
        } else if (f.wire == 2) {
            quint64 n;
            if (!readVarint(b, i, n) || i + qint64(n) > b.size()) break;
            f.bytes = b.mid(i, int(n));
            i += int(n);
        } else if (f.wire == 5) {
            if (i + 4 > b.size()) break;
            f.value = quint32(quint8(b[i])) | quint32(quint8(b[i + 1])) << 8 | quint32(quint8(b[i + 2])) << 16 | quint32(quint8(b[i + 3])) << 24;
            i += 4;
        } else if (f.wire == 1) {
            i += 8;
            continue;
        } else {
            break;
        }
        fields << f;
    }
    return fields;
}

const Field *find(const QList<Field> &fields, int number) {
    for (const auto &f : fields)
        if (f.number == number)
            return &f;
    return nullptr;
}

// Repeated varints arrive packed or unpacked; accept both.
QList<quint64> varints(const QList<Field> &fields, int number) {
    QList<quint64> out;
    for (const auto &f : fields) {
        if (f.number != number)
            continue;
        if (f.wire == 0) {
            out << f.value;
        } else if (f.wire == 2) {
            for (int i = 0; i < f.bytes.size();) {
                quint64 v;
                if (!readVarint(f.bytes, i, v)) break;
                out << v;
            }
        }
    }
    return out;
}

// --- the 60HE v2 matrix -------------------------------------------------------

struct MatrixKey { const char *code; int row, col; };
const MatrixKey kMatrix[] = {
    {"Escape", 1, 0}, {"Digit1", 1, 1}, {"Digit2", 1, 2}, {"Digit3", 1, 3}, {"Digit4", 1, 4}, {"Digit5", 1, 5},
    {"Digit6", 1, 6}, {"Digit7", 1, 7}, {"Digit8", 1, 8}, {"Digit9", 1, 9}, {"Digit0", 1, 10}, {"Minus", 1, 11},
    {"Equal", 1, 12}, {"Backspace", 1, 13},
    {"Tab", 2, 0}, {"KeyQ", 2, 1}, {"KeyW", 2, 2}, {"KeyE", 2, 3}, {"KeyR", 2, 4}, {"KeyT", 2, 5}, {"KeyY", 2, 6},
    {"KeyU", 2, 7}, {"KeyI", 2, 8}, {"KeyO", 2, 9}, {"KeyP", 2, 10}, {"BracketLeft", 2, 11}, {"BracketRight", 2, 12},
    {"Backslash", 2, 13}, // ANSI only
    {"CapsLock", 3, 0}, {"KeyA", 3, 1}, {"KeyS", 3, 2}, {"KeyD", 3, 3}, {"KeyF", 3, 4}, {"KeyG", 3, 5}, {"KeyH", 3, 6},
    {"KeyJ", 3, 7}, {"KeyK", 3, 8}, {"KeyL", 3, 9}, {"Semicolon", 3, 10}, {"Quote", 3, 11},
    {"IntlHash", 3, 12}, // ISO only
    {"Enter", 3, 13},
    {"ShiftLeft", 4, 0},
    {"IntlBackslash", 4, 1}, // ISO only
    {"KeyZ", 4, 2}, {"KeyX", 4, 3}, {"KeyC", 4, 4}, {"KeyV", 4, 5}, {"KeyB", 4, 6}, {"KeyN", 4, 7}, {"KeyM", 4, 8},
    {"Comma", 4, 9}, {"Period", 4, 10}, {"Slash", 4, 11}, {"ShiftRight", 4, 13},
    {"ControlLeft", 5, 0}, {"MetaLeft", 5, 1}, {"AltLeft", 5, 2},
    {"Space", 5, 6}, // the one switch; (5,4) (5,5) (5,7) (5,8) are LEDs under the bar
    {"AltRight", 5, 10}, {"ContextMenu", 5, 11}, {"ControlRight", 5, 12}, {"Fn", 5, 13},
};
const int kSpaceLeds[] = {4, 5, 7, 8};

int id(int row, int col) { return (row & 7) << 5 | (col & 31); }

QByteArray frame(std::initializer_list<int> bytes) {
    QByteArray f;
    for (int b : bytes)
        f += char(b);
    return f;
}

// KBase fields omni edits: 1 actuation, 3 Rapid Trigger on, 4 press, 8 release.
// Everything else (2 typing on, 5 tachyon, 6 tick rate, 7 strict range, 9 tachyon
// mode) goes back exactly as it was read.
struct Base {
    quint32 actuation = WootingDriver::analogValue(2.0);
    bool digital = true;
    bool rapidTrigger = false;
    std::optional<quint32> press, release, tickRate, tachyonMode;
    bool tachyon = false;
    std::optional<bool> strict;

    static Base from(const QByteArray &kbase) {
        Base b;
        const auto f = parse(kbase);
        if (auto *x = find(f, 1)) b.actuation = quint32(x->value);
        if (auto *x = find(f, 2)) b.digital = x->value;
        if (auto *x = find(f, 3)) b.rapidTrigger = x->value;
        if (auto *x = find(f, 4)) b.press = quint32(x->value);
        if (auto *x = find(f, 5)) b.tachyon = x->value;
        if (auto *x = find(f, 6)) b.tickRate = quint32(x->value);
        if (auto *x = find(f, 7)) b.strict = x->value != 0;
        if (auto *x = find(f, 8)) if (x->value) b.release = quint32(x->value);
        if (auto *x = find(f, 9)) b.tachyonMode = quint32(x->value);
        return b;
    }
    // Wootility writes every non-optional field, even when zero, in field order;
    // the firmware rejects messages that leave them out.
    QByteArray encode() const {
        QByteArray o;
        putVarint(o, 1, actuation);
        putVarint(o, 2, digital);
        putVarint(o, 3, rapidTrigger);
        if (press) putVarint(o, 4, *press);
        putVarint(o, 5, tachyon);
        if (tickRate) putVarint(o, 6, *tickRate);
        if (strict) putVarint(o, 7, *strict);
        if (release) putVarint(o, 8, *release);
        if (tachyonMode) putVarint(o, 9, *tachyonMode);
        return o;
    }
};

} // namespace

// --- encodings ----------------------------------------------------------------

quint32 WootingDriver::analogValue(double mm) {
    // Stock Lekker Tikken switches span 0..4.0 mm over 14 bits; the 0x4000 bit marks the 14-bit form.
    const int v = qBound(0, int(std::floor(mm / 4.0 * 16383 + 0.5)), 16383);
    return 0x4000 | quint32(v);
}

double WootingDriver::analogMm(quint32 wire) {
    const double v = wire <= 0xFF ? wire * 16383.0 / 255 : (wire >> 14) == 1 ? double(wire & 0x3FFF) : 0;
    // Round to the 0.05 mm grid the keyboard is set in.
    return std::round(v / 16383 * 4.0 * 20) / 20;
}

quint16 WootingDriver::rgb565(const QColor &c) {
    return quint16((c.red() & 0xF8) << 8 | (c.green() & 0xFC) << 3 | (c.blue() & 0xF8) >> 3);
}

int WootingDriver::matrixId(const QString &code, Region region) {
    if (region == Region::Ansi && (code == "IntlHash" || code == "IntlBackslash"))
        return -1;
    if (region == Region::Iso && code == "Backslash")
        return -1;
    for (const auto &k : kMatrix)
        if (code == QLatin1String(k.code))
            return id(k.row, k.col);
    return -1;
}

QByteArray WootingDriver::command(quint8 cmd, quint32 param) {
    // Feature report 1: magic D1 DA, command, 32-bit little-endian parameter.
    return frame({0x01, 0xD1, 0xDA, cmd, int(param & 0xFF), int(param >> 8 & 0xFF), int(param >> 16 & 0xFF), int(param >> 24 & 0xFF)});
}

QByteArray WootingDriver::outputReport(quint8 type, const QByteArray &body) {
    const QByteArray payload = frame({0xD1, 0xDA, type}) + body;
    for (int i = 0; i < 6; ++i) {
        if (payload.size() <= kReportSizes[i]) {
            QByteArray report(1 + kReportSizes[i], 0);
            report[0] = char(i + 1);
            std::copy(payload.begin(), payload.end(), report.begin() + 1);
            return report;
        }
    }
    return {};
}

QByteArray WootingDriver::profileBody(quint8 flags, const QByteArray &message) {
    // flags: bit 0 saves to flash, bits 1-5 pick the slot; 0 means live only.
    return frame({flags, int(message.size() & 0xFF), int(message.size() >> 8 & 0xFF)}) + message;
}

bool WootingDriver::allowedCommand(quint8 cmd) {
    return std::find(std::begin(kAllowedCommands), std::end(kAllowedCommands), cmd) != std::end(kAllowedCommands);
}

bool WootingDriver::allowedOutput(quint8 type) {
    return std::find(std::begin(kAllowedOutputs), std::end(kAllowedOutputs), type) != std::end(kAllowedOutputs);
}

WootingDriver::Messages WootingDriver::encode(const Settings &settings, Region region, const QByteArray &previousBase) {
    struct Key { int id; quint32 actuation; bool rt; quint32 press, release; };
    QList<Key> keys;
    for (auto it = settings.begin(); it != settings.end(); ++it) {
        const int m = matrixId(it.key(), region);
        if (m < 0)
            continue;
        const KeySettings &k = it.value();
        keys << Key{m, analogValue(k.actuation), k.rapidTrigger, analogValue(k.press), analogValue(k.release)};
    }
    std::sort(keys.begin(), keys.end(), [](const Key &a, const Key &b) { return a.id < b.id; });

    // The most common value becomes the profile's own setting; the rest are per-key exceptions.
    auto mostCommon = [](const auto &values) {
        std::map<std::decay_t<decltype(values.front())>, int> counts;
        for (const auto &v : values) ++counts[v];
        return std::max_element(counts.begin(), counts.end(), [](auto &a, auto &b) { return a.second < b.second; })->first;
    };

    Base base = Base::from(previousBase);
    Messages out;
    if (keys.isEmpty())
        return out;

    QList<quint32> actuations;
    for (const auto &k : keys) actuations << k.actuation;
    base.actuation = mostCommon(actuations);
    QList<Key> overrides;
    for (const auto &k : keys)
        if (k.actuation != base.actuation)
            overrides << k;
    QByteArray actuation;
    if (!overrides.isEmpty() && overrides.size() < 50) {
        // partialEncoded: wire value << 16 | matrix id, one entry per key.
        QByteArray entries;
        for (const auto &k : overrides)
            putVarint(entries, 1, quint64(k.actuation) << 16 | quint64(k.id));
        putBytes(actuation, 4, entries);
    } else if (!overrides.isEmpty()) {
        // fullEncoded: a 6 x 21 matrix where 0 means "use the profile's value".
        QByteArray matrix;
        for (int row = 0; row < 6; ++row) {
            QByteArray values;
            for (int col = 0; col < kColumns; ++col) {
                quint32 v = 0;
                for (const auto &k : overrides)
                    if (k.id == id(row, col)) v = k.actuation;
                varint(values, v);
            }
            QByteArray rowMessage;
            putBytes(rowMessage, 1, values);
            putBytes(matrix, 1, rowMessage);
        }
        putBytes(actuation, 3, matrix);
    }
    out.actuationProfile = actuation;

    using Rt = std::tuple<bool, quint32, quint32>;
    QList<Rt> rts;
    for (const auto &k : keys) rts << Rt{k.rt, k.press, k.release};
    const Rt common = mostCommon(rts);
    base.rapidTrigger = std::get<0>(common);
    if (base.rapidTrigger) {
        base.press = std::get<1>(common);
        base.release = std::get<2>(common) != std::get<1>(common) ? std::optional<quint32>(std::get<2>(common)) : std::nullopt;
    } else if (!base.press) {
        base.press = analogValue(0.4);
    }

    // Exceptions to the profile's Rapid Trigger, grouped by setting, at most 10 keys a group.
    std::map<Rt, QList<int>> groups;
    for (const auto &k : keys) {
        const Rt rt{k.rt, k.press, k.release};
        const bool followsBase = k.rt == base.rapidTrigger
            && (!k.rt || (k.press == *base.press && k.release == base.release.value_or(*base.press)));
        if (!followsBase)
            groups[k.rt ? rt : Rt{false, 0, 0}] << k.id;
    }
    QByteArray rtProfile;
    for (const auto &[setting, ids] : groups) {
        for (int start = 0; start < ids.size(); start += 10) {
            QByteArray group;
            for (int i = start; i < qMin(start + 10, int(ids.size())); ++i)
                putVarint(group, 1, quint64(ids[i]));
            if (std::get<0>(setting)) {
                QByteArray config;
                putVarint(config, 1, std::get<1>(setting));
                if (std::get<2>(setting) != std::get<1>(setting))
                    putVarint(config, 2, std::get<2>(setting));
                putBytes(group, 2, config);
            } else {
                putVarint(group, 3, 0); // enabled: false, Rapid Trigger off for these keys
            }
            putBytes(rtProfile, 1, group);
        }
    }
    out.rapidTriggerProfile = rtProfile;

    QByteArray profile;
    putBytes(profile, 1, base.encode());
    out.keyboardProfile = profile;
    return out;
}

// --- device -------------------------------------------------------------------

bool WootingDriver::matches(const HidNode &node) {
    return node.vendor == kVendor && (node.product & 0xFFF0) == kModel && node.product != kBootloader;
}

WootingDriver::WootingDriver(const QList<HidNode> &nodes) {
    for (const auto &node : nodes)
        if (node.usagePage == kConfigPage)
            m_node = node;
}

QString WootingDriver::identity() const {
    const QString serial = !m_serial.isEmpty() ? m_serial : m_node.serial;
    return QStringLiteral("31e3:1340") + (serial.isEmpty() ? QString() : ":" + serial);
}

QList<KeyGeometry> WootingDriver::layout() const {
    return buildLayout(Form::Sixty, m_region, {"AltRight", "ContextMenu", "ControlRight", "Fn"});
}

bool WootingDriver::isAnalog(const QString &code) const {
    return matrixId(code, m_region) >= 0; // every key is a Hall-effect switch
}

Capabilities WootingDriver::capabilities() const {
    Capabilities c;
    c.actuation = {0.1, 4.0, 0.05};
    c.sensitivity = {0.1, 2.35, 0.05};
    c.separateRelease = true;
    c.perKeyColor = true;
    return c;
}

bool WootingDriver::ask(quint8 cmd, quint32 param, QByteArray *body, QString *error) {
    if (!allowedCommand(cmd)) {
        if (error) *error = QStringLiteral("refused command %1").arg(cmd);
        return false;
    }
    m_device.drain();
    if (!m_device.setFeature(command(cmd, param), error))
        return false;
    // The answer is an input report: [id] D1 DA <cmd> <status> <len u16> body.
    // Events (EE) and log lines (DD) can arrive in between; skip them.
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 1500) {
        const QByteArray r = m_device.read(int(1500 - timer.elapsed()));
        if (r.size() < 7 || quint8(r[1]) != 0xD1 || quint8(r[2]) != 0xDA || quint8(r[3]) != cmd)
            continue;
        if (quint8(r[4]) != kStatusOk) {
            if (error) *error = QStringLiteral("the keyboard refused command %1 (status %2)").arg(cmd).arg(quint8(r[4]), 2, 16, QChar('0'));
            return false;
        }
        const int length = quint8(r[5]) | quint8(r[6]) << 8;
        if (body) *body = r.mid(7, length);
        return true;
    }
    if (error) *error = QStringLiteral("the keyboard did not answer");
    return false;
}

bool WootingDriver::send(quint8 type, const QByteArray &body, QString *error) {
    if (!allowedOutput(type)) {
        if (error) *error = QStringLiteral("refused report %1").arg(type, 2, 16, QChar('0'));
        return false;
    }
    const QByteArray report = outputReport(type, body);
    if (report.isEmpty()) {
        if (error) *error = QStringLiteral("settings too large for one report");
        return false;
    }
    return m_device.write(report, error);
}

bool WootingDriver::open(QString *error) {
    if (m_node.path.isEmpty()) {
        if (error) *error = QStringLiteral("no configuration interface");
        return false;
    }
    if (!m_device.open(m_node.path, error))
        return false;
    HidLock lock(m_device);
    QByteArray body;
    if (!ask(GetVersion, 0, &body, error) || body.size() < 3)
        return false;
    m_firmware = QStringLiteral("%1.%2.%3").arg(quint8(body[0])).arg(quint8(body[1])).arg(quint8(body[2]));
    if (QVersionNumber::fromString(m_firmware) < QVersionNumber(2, 13)) {
        if (error) *error = QStringLiteral("firmware %1 is too old; update it with Wootility to 2.13 or newer").arg(m_firmware);
        return false;
    }
    if (!ask(GetDeviceConfig, 0, &body, error) || body.size() < 6)
        return false;
    switch (quint8(body[5])) {
    case 0: m_region = Region::Ansi; break;
    case 1: m_region = Region::Iso; break;
    default:
        if (error) *error = QStringLiteral("split space bar and JIS layouts are not supported yet");
        return false;
    }
    if (!ask(GetCurrentProfile, 0, &body, error) || body.isEmpty())
        return false;
    m_slot = quint8(body[0]) & 0x1F;
    return true;
}

bool WootingDriver::read(Settings *settings, QString *error) {
    HidLock lock(m_device);
    // Reads name the active onboard slot: index in the low byte, namespace 0.
    QByteArray profile, actuation, rapidTrigger;
    if (!ask(GetKeyboardProfile, quint32(m_slot), &profile, error)
        || !ask(GetActuationProfile, quint32(m_slot), &actuation, error)
        || !ask(GetRapidTriggerProfile, quint32(m_slot), &rapidTrigger, error))
        return false;
    const Field *baseField = find(parse(profile), 1);
    m_base = baseField ? baseField->bytes : QByteArray();
    const Base base = Base::from(m_base);

    QHash<int, quint32> overrides;
    const auto act = parse(actuation);
    if (const Field *partial = find(act, 4)) {
        for (quint64 e : varints(parse(partial->bytes), 1))
            overrides[int(e & 0xFF)] = quint32(e >> 16);
    } else if (const Field *full = find(act, 3)) {
        int row = 0;
        for (const auto &r : parse(full->bytes)) {
            if (r.number != 1) continue;
            int col = 0;
            for (quint64 v : varints(parse(r.bytes), 1)) {
                if (v) overrides[id(row, col)] = quint32(v);
                ++col;
            }
            ++row;
        }
    }

    struct Rt { bool on; quint32 press, release; };
    const quint32 basePress = base.press.value_or(analogValue(0.4));
    QHash<int, Rt> perKey;
    for (const auto &g : parse(rapidTrigger)) {
        if (g.number != 1) continue;
        const auto fields = parse(g.bytes);
        Rt rt{base.rapidTrigger, basePress, base.release.value_or(basePress)};
        if (const Field *config = find(fields, 2)) {
            const auto c = parse(config->bytes);
            rt.on = true;
            rt.press = find(c, 1) ? quint32(find(c, 1)->value) : basePress;
            rt.release = find(c, 2) && find(c, 2)->value ? quint32(find(c, 2)->value) : rt.press;
        } else if (const Field *enabled = find(fields, 3)) {
            rt.on = enabled->value != 0;
        }
        for (quint64 key : varints(fields, 1))
            perKey[int(key)] = rt;
    }

    settings->clear();
    for (const auto &key : layout()) {
        const int m = matrixId(key.code, m_region);
        KeySettings k;
        k.actuation = analogMm(overrides.value(m, base.actuation));
        const Rt rt = perKey.value(m, Rt{base.rapidTrigger, basePress, base.release.value_or(basePress)});
        k.rapidTrigger = rt.on;
        k.press = analogMm(rt.press);
        k.release = analogMm(rt.release);
        settings->insert(key.code, k);
    }
    return true;
}

bool WootingDriver::writeSettings(quint8 flags, QString *error) {
    // Wootility's order: per-key Rapid Trigger, per-key actuation, then the profile itself.
    return send(RapidTriggerProfile, profileBody(flags, m_pending.rapidTriggerProfile), error)
        && send(ActuationProfile, profileBody(flags, m_pending.actuationProfile), error)
        && send(KeyboardProfile, profileBody(flags, m_pending.keyboardProfile), error);
}

bool WootingDriver::write(const Settings &settings, int changes, QString *error) {
    HidLock lock(m_device);
    if (changes & (ActuationChange | RapidTriggerChange)) {
        m_pending = encode(settings, m_region, m_base);
        const Field *base = find(parse(m_pending.keyboardProfile), 1);
        m_base = base ? base->bytes : m_base;
        if (!writeSettings(0x00, error)) // live, in the keyboard's RAM
            return false;
        m_dirty = true;
    }
    if (changes & ColorChange) {
        bool any = false;
        for (const auto &k : settings)
            any = any || k.color.isValid();
        if (!any)
            return restoreLighting(error);
        if (!m_directMode) {
            if (!ask(LightingInit, 0, nullptr, error))
                return false;
            m_directMode = true;
        }
        // 6 x 21 RGB565, little-endian, row-major.
        QByteArray colors(6 * kColumns * 2, 0);
        auto put = [&](int row, int col, const QColor &c) {
            const quint16 v = c.isValid() ? rgb565(c) : 0;
            colors[2 * (row * kColumns + col)] = char(v & 0xFF);
            colors[2 * (row * kColumns + col) + 1] = char(v >> 8);
        };
        for (auto it = settings.begin(); it != settings.end(); ++it) {
            const int m = matrixId(it.key(), m_region);
            if (m < 0) continue;
            put(m >> 5, m & 31, it->color);
            if (it.key() == "Space")
                for (int col : kSpaceLeds)
                    put(5, col, it->color);
        }
        if (!send(DirectLighting, colors, error))
            return false;
    }
    return true;
}

bool WootingDriver::commit(QString *error) {
    HidLock lock(m_device);
    if (!m_dirty)
        return true;
    // The same messages again, with the save bit and the active slot.
    if (!writeSettings(quint8(m_slot << 1 | 1), error))
        return false;
    m_dirty = false;
    return true;
}

bool WootingDriver::restoreLighting(QString *error) {
    HidLock lock(m_device);
    m_directMode = false;
    return ask(LightingReset, 0, nullptr, error);
}
