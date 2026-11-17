#include "effects.h"

#include <QFile>
#include <QJsonArray>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>

namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

QColor mix(const QColor &a, const QColor &b, double t) {
    t = clamp01(t);
    return QColor::fromRgbF(float(a.redF() + (b.redF() - a.redF()) * t), float(a.greenF() + (b.greenF() - a.greenF()) * t),
                            float(a.blueF() + (b.blueF() - a.blueF()) * t));
}

QColor scaled(const QColor &c, double k) {
    k = clamp01(k);
    return QColor::fromRgbF(float(c.redF() * k), float(c.greenF() * k), float(c.blueF() * k));
}

// Light adds up, like two lamps on one key.
QColor screen(const QColor &a, const QColor &b) {
    auto s = [](double x, double y) { return float(1 - (1 - x) * (1 - y)); };
    return QColor::fromRgbF(s(a.redF(), b.redF()), s(a.greenF(), b.greenF()), s(a.blueF(), b.blueF()));
}

// A stable pseudo-random number in [0, 1) for an integer, so effects look
// random without any state that would drift between the service and the preview.
double hash(quint32 n) {
    n = (n ^ 61) ^ (n >> 16);
    n *= 9;
    n ^= n >> 4;
    n *= 0x27d4eb2d;
    n ^= n >> 15;
    return (n & 0xFFFFFF) / double(0x1000000);
}

double hash2(int x, int y) { return hash(quint32(x) * 73856093u ^ quint32(y) * 19349663u); }

// Smooth value noise: random values on a grid, eased between.
double noise(double x, double y) {
    const int xi = int(std::floor(x)), yi = int(std::floor(y));
    const double xf = x - xi, yf = y - yi;
    auto ease = [](double t) { return t * t * (3 - 2 * t); };
    const double a = hash2(xi, yi), b = hash2(xi + 1, yi), c = hash2(xi, yi + 1), d = hash2(xi + 1, yi + 1);
    const double u = ease(xf), v = ease(yf);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

} // namespace

bool Lighting::animated() const {
    return effect != QLatin1String("own") && effect != QLatin1String("static");
}

QJsonObject Lighting::toJson() const {
    QJsonArray palette;
    for (const auto &c : colors)
        palette << c.name();
    QJsonObject keys;
    for (auto it = keyColors.begin(); it != keyColors.end(); ++it)
        keys.insert(it.key(), it.value().name());
    return {{"effect", effect}, {"palette", this->palette}, {"colors", palette}, {"speed", speed},
            {"brightness", brightness}, {"direction", direction}, {"reactive", reactive}, {"keys", keys}};
}

Lighting Lighting::fromJson(const QJsonObject &o) {
    Lighting l;
    l.effect = o.value("effect").toString(l.effect);
    l.palette = o.value("palette").toString(l.palette);
    for (const auto &v : o.value("colors").toArray())
        if (QColor c(v.toString()); c.isValid())
            l.colors << c;
    l.speed = std::clamp(o.value("speed").toDouble(l.speed), 0.25, 3.0);
    l.brightness = std::clamp(o.value("brightness").toDouble(l.brightness), 0.0, 1.0);
    l.direction = o.value("direction").toString(l.direction);
    l.reactive = o.value("reactive").toString(l.reactive);
    const QJsonObject keys = o.value("keys").toObject();
    for (auto it = keys.begin(); it != keys.end(); ++it)
        if (QColor c(it.value().toString()); c.isValid())
            l.keyColors.insert(it.key(), c);
    return l;
}

QList<QColor> themeColors(const QString &colorsToml) {
    QFile file(colorsToml);
    QList<QColor> out;
    if (!file.open(QIODevice::ReadOnly))
        return {QColor("#FFD60A"), QColor("#ff375f"), QColor("#0a84ff")};
    const QString text = QString::fromUtf8(file.readAll());
    auto value = [&](const QString &name) {
        const QRegularExpression re(QStringLiteral(R"(^\s*%1\s*=\s*["'](#[0-9a-fA-F]{6})["'])").arg(name),
                                    QRegularExpression::MultilineOption);
        const auto m = re.match(text);
        return m.hasMatch() ? QColor(m.captured(1)) : QColor();
    };
    for (const char *name : {"accent", "magenta", "blue", "cyan", "red"}) {
        const QColor c = value(QString::fromLatin1(name));
        if (c.isValid() && !out.contains(c))
            out << c;
        if (out.size() == 3)
            break;
    }
    if (out.isEmpty())
        out << QColor("#FFD60A");
    return out;
}

Effects::Effects(const QList<KeyGeometry> &layout) { setLayout(layout); }

void Effects::setLayout(const QList<KeyGeometry> &layout) {
    m_keys.clear();
    m_bounds = {};
    for (const auto &k : layout) {
        const QPointF c(k.x + k.w / 2, k.y + k.h / 2);
        m_keys << Key{k.code, c};
        m_bounds = m_bounds.isNull() ? QRectF(c, QSizeF(0.001, 0.001)) : m_bounds.united(QRectF(c, QSizeF(0.001, 0.001)));
    }
}

void Effects::setLighting(const Lighting &lighting, const QList<QColor> &themePalette) {
    m_lighting = lighting;
    m_rainbow = lighting.palette == QLatin1String("rainbow");
    m_palette = lighting.palette == QLatin1String("custom") && !lighting.colors.isEmpty() ? lighting.colors : themePalette;
    if (m_palette.isEmpty())
        m_palette << QColor("#FFD60A");
}

QColor Effects::sample(double p) const {
    p -= std::floor(p);
    if (m_rainbow)
        return QColor::fromHsvF(float(p), 1, 1);
    if (m_palette.size() == 1)
        return m_palette[0];
    const double at = p * m_palette.size();
    const int i = int(at) % m_palette.size();
    return mix(m_palette[i], m_palette[(i + 1) % m_palette.size()], at - std::floor(at));
}

void Effects::press(const QString &code, double t) {
    for (const auto &k : m_keys) {
        if (k.code != code)
            continue;
        m_presses << Press{k.center, code, t};
        if (m_presses.size() > 64)
            m_presses.removeFirst();
        m_heat[code] += 1;
        return;
    }
}

QColor Effects::base(const Key &key, int index, double t) const {
    const QString &e = m_lighting.effect;
    const double s = m_lighting.speed;
    const double w = std::max(1.0, m_bounds.width()), h = std::max(1.0, m_bounds.height());
    const double nx = (key.center.x() - m_bounds.left()) / w, ny = (key.center.y() - m_bounds.top()) / h;

    if (e == QLatin1String("static"))
        return m_lighting.keyColors.value(key.code, Qt::black);
    if (e == QLatin1String("breathing")) {
        // Each breath fades in and out, then the next breath takes the palette's next colour.
        const double cycle = t * s / 4.0;
        const double level = 0.5 - 0.5 * std::cos(2 * kPi * cycle);
        const int n = m_rainbow ? 6 : int(m_palette.size());
        return scaled(sample(std::floor(cycle) / n), 0.04 + 0.96 * level * level);
    }
    if (e == QLatin1String("spectrum"))
        return sample(t * s * 0.08);
    if (e == QLatin1String("wave")) {
        const QString &d = m_lighting.direction;
        double along = nx;
        if (d == QLatin1String("left")) along = 1 - nx;
        else if (d == QLatin1String("down")) along = ny * h / w;
        else if (d == QLatin1String("up")) along = (1 - ny) * h / w;
        else if (d == QLatin1String("out")) along = std::hypot(nx - 0.5, (ny - 0.5) * h / w);
        return sample(along - t * s * 0.25);
    }
    if (e == QLatin1String("aurora")) {
        // Two layers of slow noise: one picks the colour, one the light.
        const double x = key.center.x(), y = key.center.y();
        const double hue = noise(x * 0.16 + t * 0.10 * s, y * 0.30 - t * 0.04 * s);
        const double light = noise(x * 0.22 - t * 0.13 * s + 40, y * 0.45 + 17);
        return scaled(sample(hue * 1.4), 0.15 + 0.85 * std::pow(light, 1.3));
    }
    if (e == QLatin1String("starlight")) {
        // Every key twinkles now and then, each on its own random schedule.
        const double period = 2.5 + 4.0 * hash(quint32(index) * 7 + 1);
        const double local = t * s / period + hash(quint32(index) * 13 + 5);
        const double phase = local - std::floor(local);
        const double twinkle = phase < 0.25 ? std::sin(phase / 0.25 * kPi) : 0;
        const QColor star = sample(hash(quint32(index) * 31 + quint32(std::floor(local))));
        return screen(scaled(sample(0), 0.05), scaled(star, twinkle));
    }
    if (e == QLatin1String("rain")) {
        // Drops fall down each column of keys, each column at its own pace, with a fading tail.
        const int column = int(std::round(key.center.x() * 2));
        const double pace = 0.7 + 0.6 * hash(quint32(column) * 3 + 11);
        const double span = h + 3.0;
        const double head = std::fmod(t * s * 2.2 * pace + hash(quint32(column) * 17) * span, span) - 1.0;
        const double behind = head - (key.center.y() - m_bounds.top());
        const double level = behind < -0.5 ? 0 : behind < 0 ? 1 + behind * 2 : std::exp(-behind / 0.9);
        return screen(scaled(sample(0.5), 0.03), scaled(sample(hash(quint32(column) * 5)), level));
    }
    return Qt::black;
}

QColor Effects::overlay(const Key &key, double t, const QColor &under) const {
    const QString &r = m_lighting.reactive;
    const double s = m_lighting.speed;
    if (r == QLatin1String("fade")) {
        double level = 0;
        for (const auto &p : m_presses)
            if (p.code == key.code)
                level = std::max(level, std::exp(-(t - p.t) * 2.2 * s));
        return screen(under, scaled(sample(0.15), level));
    }
    if (r == QLatin1String("ripple")) {
        QColor out = under;
        for (const auto &p : m_presses) {
            const double age = t - p.t;
            if (age < 0 || age > 1.6 / s)
                continue;
            const double radius = age * 9.0 * s;
            const double distance = std::hypot(key.center.x() - p.at.x(), key.center.y() - p.at.y());
            const double ring = std::exp(-std::pow((distance - radius) / 0.75, 2)) * (1 - age * s / 1.6);
            out = screen(out, scaled(sample(distance * 0.05 + p.t * 0.1), ring));
        }
        return out;
    }
    if (r == QLatin1String("heatmap")) {
        double most = 0;
        for (double v : m_heat)
            most = std::max(most, v);
        if (most <= 0)
            return under;
        const double level = m_heat.value(key.code) / most;
        // Cold keys stay dark; the busiest glow in the palette's last colour.
        return screen(scaled(under, 0.35), level > 0 ? scaled(sample(0.6 * level), 0.15 + 0.85 * level) : QColor(Qt::black));
    }
    return under;
}

QHash<QString, QColor> Effects::render(double t) {
    // Old presses fade out of the picture; heat cools with a half-life of a minute.
    m_presses.erase(std::remove_if(m_presses.begin(), m_presses.end(), [t](const Press &p) { return t - p.t > 8; }),
                    m_presses.end());
    if (m_heatTime > 0 && t > m_heatTime) {
        const double k = std::pow(0.5, (t - m_heatTime) / 60.0);
        for (auto &v : m_heat)
            v *= k;
    }
    m_heatTime = t;

    QHash<QString, QColor> frame;
    const bool drawsBase = m_lighting.effect != QLatin1String("own");
    for (int i = 0; i < m_keys.size(); ++i) {
        const Key &key = m_keys[i];
        QColor c = drawsBase ? base(key, i, t) : QColor(Qt::black);
        c = overlay(key, t, c);
        frame.insert(key.code, scaled(c, m_lighting.brightness));
    }
    return frame;
}
