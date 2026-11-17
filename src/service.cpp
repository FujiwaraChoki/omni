#include "service.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>

// --- key reports ----------------------------------------------------------------

KeyInput::KeyInput(const HidNode &node, QObject *parent) : QObject(parent), m_reportIds(node.reportIds) {
    m_fd = ::open(QFile::encodeName(node.path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0)
        return;
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] {
        char buffer[128];
        ssize_t n;
        while ((n = ::read(m_fd, buffer, sizeof buffer)) > 0) {
            const QSet<quint8> now = pressedIn(QByteArray(buffer, int(n)), m_reportIds);
            for (quint8 usage : now)
                if (!m_down.contains(usage))
                    if (const QString code = codeForUsage(usage); !code.isEmpty())
                        emit pressed(code);
            m_down = now;
        }
    });
}

KeyInput::~KeyInput() {
    if (m_fd >= 0)
        ::close(m_fd);
}

QSet<quint8> KeyInput::pressedIn(const QByteArray &raw, bool reportIds) {
    const QByteArray r = reportIds && !raw.isEmpty() ? raw.mid(1) : raw;
    QSet<quint8> down;
    if (r.size() == 8) {
        // Boot report: modifier bits, a reserved byte, up to six usages.
        for (int bit = 0; bit < 8; ++bit)
            if (quint8(r[0]) & (1 << bit))
                down.insert(quint8(0xE0 + bit));
        for (int i = 2; i < 8; ++i)
            if (quint8(r[i]) > 3) // 1-3 are rollover and error codes
                down.insert(quint8(r[i]));
    } else if (r.size() >= 16) {
        // Bitmap: one bit per usage, usage 0 first.
        for (int byte = 0; byte < qMin(32, int(r.size())); ++byte)
            for (int bit = 0; bit < 8; ++bit)
                if (quint8(r[byte]) & (1 << bit))
                    if (const int usage = byte * 8 + bit; usage > 3)
                        down.insert(quint8(usage));
    }
    return down;
}

// --- the service ----------------------------------------------------------------

LightingService::LightingService(QObject *parent) : QObject(parent) {
    m_clock.start();
    m_palette = themeColors(QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml");
    // A theme switch changes the "theme" palette live.
    connect(&m_theme, &Theme::changed, this, [this] {
        m_palette = themeColors(QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml");
        for (auto &d : m_devices)
            d->effects.setLighting(d->config.lighting, m_palette);
    });

    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(400);
    connect(&m_rescanTimer, &QTimer::timeout, this, &LightingService::rescan);
    m_devWatcher.addPath("/dev");
    connect(&m_devWatcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_rescanTimer.start(); });

    // The window saves by renaming a new file into place, which the
    // directory watch sees; re-arm after every change.
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(60);
    connect(&m_reloadTimer, &QTimer::timeout, this, [this] { reload(); });
    QDir().mkpath(DeviceConfig::directory());
    m_configWatcher.addPath(DeviceConfig::directory());
    connect(&m_configWatcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_reloadTimer.start(); });
    connect(&m_configWatcher, &QFileSystemWatcher::fileChanged, this, [this] { m_reloadTimer.start(); });

    connect(&m_tick, &QTimer::timeout, this, &LightingService::tick);
    m_tick.start(33);
    rescan();
}

LightingService::~LightingService() { release(); }

void LightingService::report(Device &d, const QString &error) {
    // One line per new problem, for `journalctl --user -u omni-lighting`.
    if (error == d.lastError)
        return;
    d.lastError = error;
    if (!error.isEmpty())
        fprintf(stderr, "%s: %s\n", qPrintable(d.keyboard->model()), qPrintable(error));
}

void LightingService::release() {
    for (auto &d : m_devices) {
        QString error;
        if (d->lit)
            d->keyboard->restoreLighting(&error);
        if (d->screenOn)
            d->keyboard->releaseScreen(&error);
        d->lit = d->screenOn = false;
    }
}

void LightingService::rescan() {
    QHash<QString, QList<HidNode>> groups;
    QHash<QString, const Driver *> owner;
    for (const auto &node : enumerateHidraw())
        for (const auto &driver : drivers())
            if (driver.matches(node)) {
                const QString location = QStringLiteral("%1/%2:%3").arg(node.usbDevice).arg(node.vendor, 4, 16, QChar('0')).arg(node.product, 4, 16, QChar('0'));
                groups[location] << node;
                owner[location] = &driver;
            }
    for (size_t i = m_devices.size(); i-- > 0;)
        if (!groups.contains(m_devices[i]->location))
            m_devices.erase(m_devices.begin() + long(i));
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        if (std::any_of(m_devices.begin(), m_devices.end(), [&](const auto &d) { return d->location == it.key(); }))
            continue;
        auto d = std::make_unique<Device>();
        d->keyboard.reset(owner[it.key()]->create(it.value()));
        QString error;
        if (!d->keyboard->open(&error)) {
            fprintf(stderr, "%s: %s\n", qPrintable(d->keyboard->model()), qPrintable(error));
            continue;
        }
        d->location = it.key();
        d->configFile = DeviceConfig::path(DeviceConfig::directory(), d->keyboard->identity());
        d->effects.setLayout(d->keyboard->layout());
        reload(*d);
        m_devices.push_back(std::move(d));
    }
}

void LightingService::reload() {
    for (auto &d : m_devices)
        reload(*d);
    // Files replaced by a rename drop out of the watch.
    if (!m_configWatcher.directories().contains(DeviceConfig::directory()))
        m_configWatcher.addPath(DeviceConfig::directory());
}

void LightingService::reload(Device &d) {
    d.config = DeviceConfig::load(d.configFile);
    d.effects.setLighting(d.config.lighting, m_palette);
    d.lastFrame.clear(); // send the next frame even if it looks the same

    // Key reports are only read while a reactive effect is on.
    const bool listen = d.config.lighting.reactive != QLatin1String("none");
    if (listen && d.inputs.empty()) {
        for (const auto &node : d.keyboard->keyInputs()) {
            auto input = std::make_unique<KeyInput>(node);
            if (!input->isOpen())
                continue;
            Device *device = &d;
            connect(input.get(), &KeyInput::pressed, this, [this, device](const QString &code) {
                device->effects.press(code, m_clock.elapsed() / 1000.0);
            });
            d.inputs.push_back(std::move(input));
        }
    } else if (!listen) {
        d.inputs.clear();
    }

    const QJsonObject wanted = d.config.screen.toJson();
    if (d.config.screen.active() && wanted != d.framesFor && d.keyboard->screenSize().isValid()) {
        QString error;
        if (!d.frames.load(d.config.screen.file, d.config.screen, d.keyboard->screenSize(), &error))
            report(d, QStringLiteral("screen picture: ") + error);
        d.framesFor = wanted;
        d.shownFrame = -1;
    } else if (!d.config.screen.active()) {
        d.frames = {};
        d.framesFor = {};
    }
}

void LightingService::tick() {
    const qint64 now = m_clock.elapsed();
    bool busy = false;
    for (auto &dp : m_devices) {
        Device &d = *dp;
        const Lighting &l = d.config.lighting;
        QString error;

        if (l.drawn()) {
            const QHash<QString, QColor> frame = d.effects.render(now / 1000.0);
            busy = busy || l.animated() || l.reactive != QLatin1String("none");
            // A still picture goes out once, then every few seconds in case the keyboard was replugged or reset.
            if (frame != d.lastFrame || now - d.lastLight > 3000) {
                Settings settings;
                for (auto it = frame.begin(); it != frame.end(); ++it)
                    settings[it.key()].color = it.value();
                if (d.keyboard->write(settings, ColorChange, &error)) {
                    d.lastFrame = frame;
                    d.lastLight = now;
                    d.lit = true;
                    report(d, {});
                } else {
                    report(d, error);
                }
            }
        } else if (d.lit) {
            if (d.keyboard->restoreLighting(&error))
                d.lit = false;
            report(d, error);
        }

        if (d.config.screen.active() && !d.frames.isEmpty()) {
            busy = busy || d.frames.count() > 1;
            const int index = d.frames.frameAt(now);
            if (index != d.shownFrame || now - d.lastScreen > 3000) {
                if (d.keyboard->showScreen(d.frames.frame(index), &error)) {
                    d.shownFrame = index;
                    d.lastScreen = now;
                    d.screenOn = true;
                    report(d, {});
                } else {
                    report(d, error);
                }
            }
        } else if (d.screenOn) {
            if (d.keyboard->releaseScreen(&error))
                d.screenOn = false;
            report(d, error);
        }
    }
    // 30 frames a second while something moves; a slow tick otherwise.
    const int interval = busy ? 33 : 250;
    if (m_tick.interval() != interval)
        m_tick.setInterval(interval);
}
