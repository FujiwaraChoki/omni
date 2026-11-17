#include "backend.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QProcess>
#include <QStandardPaths>

#include "portal.h"

namespace {
QString themeFile() { return QDir::homePath() + "/.local/state/omarchy/current/theme/colors.toml"; }
} // namespace

Backend::Backend(QObject *parent, std::function<QList<HidNode>()> scan, const QString &configDirectory, bool watchDevices)
    : QObject(parent),
      m_scan(scan ? scan : [] { return enumerateHidraw(); }),
      m_configDirectory(configDirectory.isEmpty() ? DeviceConfig::directory() : configDirectory),
      m_live(watchDevices) {
    m_clock.start();
    m_palette = themeColors(themeFile());

    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(400); // a keyboard brings up its interfaces one by one
    connect(&m_rescanTimer, &QTimer::timeout, this, &Backend::rescan);
    m_writeTimer.setSingleShot(true);
    m_writeTimer.setInterval(120);
    connect(&m_writeTimer, &QTimer::timeout, this, &Backend::writePending);
    // Saving to the keyboard's flash waits until edits settle.
    m_commitTimer.setSingleShot(true);
    m_commitTimer.setInterval(1500);
    connect(&m_commitTimer, &QTimer::timeout, this, &Backend::commitAll);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(80);
    connect(&m_saveTimer, &QTimer::timeout, this, &Backend::saveConfig);
    connect(&m_previewTimer, &QTimer::timeout, this, &Backend::previewTick);
    m_previewTimer.start(33);

    if (m_live) {
        // hidraw nodes appear and vanish in /dev as keyboards come and go.
        m_watcher.addPath("/dev");
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_rescanTimer.start(); });
        // Key presses in the window light up the preview of reactive effects.
        QCoreApplication::instance()->installEventFilter(this);
        rescan();
    }
}

Backend::~Backend() { flush(); }

Backend::Device *Backend::device() {
    return m_current >= 0 && m_current < int(m_devices.size()) ? m_devices[size_t(m_current)].get() : nullptr;
}

const Backend::Device *Backend::device() const {
    return m_current >= 0 && m_current < int(m_devices.size()) ? m_devices[size_t(m_current)].get() : nullptr;
}

void Backend::setError(const QString &error) {
    if (error == m_error)
        return;
    m_error = error;
    emit errorChanged();
}

void Backend::reloadTheme() {
    m_palette = themeColors(themeFile());
    for (auto &d : m_devices)
        d->effects.setLighting(d->config.lighting, m_palette);
    emit lightingChanged();
}

// --- devices ----------------------------------------------------------------------

void Backend::rescan() {
    // Group the nodes of each physical keyboard and offer them to the drivers.
    QHash<QString, QList<HidNode>> groups;
    QHash<QString, const Driver *> owner;
    for (const auto &node : m_scan()) {
        for (const auto &driver : drivers()) {
            if (!driver.matches(node))
                continue;
            const QString location = QStringLiteral("%1/%2:%3").arg(node.usbDevice).arg(node.vendor, 4, 16, QChar('0')).arg(node.product, 4, 16, QChar('0'));
            groups[location] << node;
            owner[location] = &driver;
        }
    }

    const QString previous = device() ? device()->location : QString();
    bool changed = false;
    for (size_t i = m_devices.size(); i-- > 0;) {
        if (!groups.contains(m_devices[i]->location)) {
            m_devices.erase(m_devices.begin() + long(i));
            changed = true;
        }
    }
    QString failure;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        const bool known = std::any_of(m_devices.begin(), m_devices.end(),
                                       [&](const auto &d) { return d->location == it.key(); });
        if (known)
            continue;
        std::unique_ptr<Keyboard> keyboard(owner[it.key()]->create(it.value()));
        QString error;
        if (!keyboard->open(&error)) {
            failure = tr("Could not open the %1: %2").arg(keyboard->model(), error);
            continue;
        }
        adopt(keyboard.release(), it.key());
        changed = true;
    }

    if (changed) {
        m_current = -1;
        for (size_t i = 0; i < m_devices.size(); ++i)
            if (m_devices[i]->location == previous)
                m_current = int(i);
        if (m_current < 0 && !m_devices.empty())
            m_current = 0;
        emit devicesChanged();
        emit keysChanged();
        emit selectionChanged();
        emit lightingChanged();
        emit screenChanged();
        emit historyChanged();
    }
    if (!failure.isEmpty())
        setError(failure);
    else if (changed)
        setError({});
}

void Backend::adopt(Keyboard *keyboard, const QString &location) {
    auto d = std::make_unique<Device>();
    d->keyboard.reset(keyboard);
    d->location = location;
    d->layout = keyboard->layout();
    QString error;
    if (!keyboard->read(&d->settings, &error)) {
        setError(tr("Could not read the %1: %2").arg(keyboard->model(), error));
        for (const auto &key : d->layout)
            d->settings.insert(key.code, KeySettings{});
    }
    d->configFile = DeviceConfig::path(m_configDirectory, keyboard->identity());
    d->config = DeviceConfig::load(d->configFile);
    d->config.model = keyboard->model();
    d->effects.setLayout(d->layout);
    d->effects.setLighting(d->config.lighting, m_palette);
    loadScreenFrames(*d);
    const bool needsService = d->config.lighting.drawn() || d->config.screen.active();
    m_devices.push_back(std::move(d));
    if (m_current < 0)
        m_current = 0;
    if (needsService)
        startService();
    emit devicesChanged();
    emit keysChanged();
    emit lightingChanged();
    emit screenChanged();
}

QVariantList Backend::devices() const {
    QVariantList list;
    for (const auto &d : m_devices)
        list << QVariantMap{{"model", d->keyboard->model()}, {"firmware", d->keyboard->firmware()},
                            {"hasScreen", d->keyboard->screenSize().isValid()}};
    return list;
}

void Backend::setCurrent(int index) {
    if (index == m_current || index < 0 || index >= int(m_devices.size()))
        return;
    flush();
    m_current = index;
    emit devicesChanged();
    emit keysChanged();
    emit selectionChanged();
    emit lightingChanged();
    emit screenChanged();
    emit historyChanged();
}

// --- keys -------------------------------------------------------------------------

QVariantList Backend::keys() const {
    QVariantList list;
    const Device *d = device();
    if (!d)
        return list;
    for (const auto &key : d->layout) {
        const KeySettings k = d->settings.value(key.code);
        list << QVariantMap{
            {"code", key.code}, {"label", key.label},
            {"x", key.x}, {"y", key.y}, {"w", key.w}, {"h", key.h},
            {"analog", d->keyboard->isAnalog(key.code)},
            {"actuation", k.actuation}, {"rapidTrigger", k.rapidTrigger},
            {"press", k.press}, {"release", k.release},
        };
    }
    return list;
}

QVariantMap Backend::capabilities() const {
    const Device *d = device();
    if (!d)
        return {};
    const Capabilities c = d->keyboard->capabilities();
    return {
        {"actuationMin", c.actuation.min}, {"actuationMax", c.actuation.max}, {"actuationStep", c.actuation.step},
        {"actuationStops", QVariant::fromValue(QVariantList(c.actuation.stops.begin(), c.actuation.stops.end()))},
        {"sensitivityMin", c.sensitivity.min}, {"sensitivityMax", c.sensitivity.max}, {"sensitivityStep", c.sensitivity.step},
        {"separateRelease", c.separateRelease}, {"perKeyColor", c.perKeyColor},
        {"hasScreen", d->keyboard->screenSize().isValid()},
    };
}

QStringList Backend::selection() const {
    const Device *d = device();
    if (!d)
        return {};
    // In layout order, so the list reads like the keyboard.
    QStringList list;
    for (const auto &key : d->layout)
        if (d->selection.contains(key.code))
            list << key.code;
    return list;
}

QVariantMap Backend::selected() const {
    // What the controls show: the shared value of the selected keys, or
    // "mixed" when they differ. Settings that only analog keys have are
    // summarised over the analog keys alone.
    const Device *d = device();
    QVariantMap map{{"count", 0}, {"analogCount", 0}};
    if (!d || d->selection.isEmpty())
        return map;
    QVariant actuation, rapidTrigger, press, release, color;
    bool firstAnalog = true, first = true, mixedAct = false, mixedRt = false, mixedPress = false, mixedRelease = false, mixedColor = false;
    int analog = 0;
    for (const auto &code : d->selection) {
        const KeySettings k = d->settings.value(code);
        const QColor kc = d->config.lighting.keyColors.value(code);
        const QString c = kc.isValid() ? kc.name() : QString();
        if (first) { color = c; first = false; }
        else if (color.toString() != c) mixedColor = true;
        if (!d->keyboard->isAnalog(code))
            continue;
        ++analog;
        if (firstAnalog) {
            actuation = k.actuation; rapidTrigger = k.rapidTrigger; press = k.press; release = k.release;
            firstAnalog = false;
            continue;
        }
        mixedAct |= !qFuzzyCompare(actuation.toDouble(), k.actuation);
        mixedRt |= rapidTrigger.toBool() != k.rapidTrigger;
        mixedPress |= !qFuzzyCompare(press.toDouble(), k.press);
        mixedRelease |= !qFuzzyCompare(release.toDouble(), k.release);
    }
    map["count"] = int(d->selection.size());
    map["analogCount"] = analog;
    map["actuation"] = actuation;
    map["actuationMixed"] = mixedAct;
    map["rapidTrigger"] = rapidTrigger;
    map["rapidTriggerMixed"] = mixedRt;
    map["press"] = press;
    map["pressMixed"] = mixedPress;
    map["release"] = release;
    map["releaseMixed"] = mixedRelease;
    map["color"] = color;
    map["colorMixed"] = mixedColor;
    return map;
}

void Backend::select(const QString &code, bool extend) {
    Device *d = device();
    if (!d)
        return;
    if (!extend)
        d->selection = {code};
    else if (d->selection.contains(code))
        d->selection.remove(code);
    else
        d->selection.insert(code);
    emit selectionChanged();
}

void Backend::setSelection(const QStringList &codes, bool extend) {
    Device *d = device();
    if (!d)
        return;
    if (!extend)
        d->selection.clear();
    for (const auto &code : codes)
        d->selection.insert(code);
    emit selectionChanged();
}

void Backend::selectAll() {
    Device *d = device();
    if (!d)
        return;
    for (const auto &key : d->layout)
        d->selection.insert(key.code);
    emit selectionChanged();
}

void Backend::selectNone() {
    Device *d = device();
    if (!d)
        return;
    d->selection.clear();
    emit selectionChanged();
}

// --- undo -------------------------------------------------------------------------

Backend::Snapshot Backend::snapshot(const Device &d, int changes) const {
    return {d.settings, d.config.lighting, d.config.screen, changes};
}

void Backend::remember(Device &d, int changes) {
    // A slider drag is one step of undo, not one per frame.
    const bool continues = m_lastEdit.isValid() && m_lastEdit.elapsed() < 700 && m_lastChanges == changes && !d.undo.isEmpty();
    if (!continues) {
        d.undo << snapshot(d, changes);
        d.redo.clear();
    }
    m_lastEdit.restart();
    m_lastChanges = changes;
}

void Backend::restore(Device &d, const Snapshot &s) {
    d.settings = s.settings;
    const bool screenChanged = !(d.config.screen == s.screen);
    d.config.lighting = s.lighting;
    d.config.screen = s.screen;
    d.effects.setLighting(d.config.lighting, m_palette);
    if (screenChanged)
        loadScreenFrames(d);
    m_lastEdit.invalidate();
    emit keysChanged();
    emit selectionChanged();
    emit lightingChanged();
    emit this->screenChanged();
    emit historyChanged();
    const int keyChanges = s.changes & (ActuationChange | RapidTriggerChange);
    if (keyChanges)
        queueWrite(keyChanges);
    if (s.changes & (LightingChange | ScreenChange))
        m_saveTimer.start();
}

bool Backend::canUndo() const { const Device *d = device(); return d && !d->undo.isEmpty(); }
bool Backend::canRedo() const { const Device *d = device(); return d && !d->redo.isEmpty(); }

void Backend::undo() {
    Device *d = device();
    if (!d || d->undo.isEmpty())
        return;
    const Snapshot s = d->undo.takeLast();
    d->redo << snapshot(*d, s.changes);
    restore(*d, s);
}

void Backend::redo() {
    Device *d = device();
    if (!d || d->redo.isEmpty())
        return;
    const Snapshot s = d->redo.takeLast();
    d->undo << snapshot(*d, s.changes);
    restore(*d, s);
}

// --- key settings -------------------------------------------------------------------

void Backend::edit(int changes, const std::function<void(KeySettings &)> &apply) {
    Device *d = device();
    if (!d || d->selection.isEmpty())
        return;
    Settings next = d->settings;
    for (const auto &code : d->selection)
        if (d->keyboard->isAnalog(code))
            apply(next[code]);
    if (next == d->settings)
        return;
    remember(*d, changes);
    d->settings = next;
    emit keysChanged();
    emit selectionChanged();
    emit historyChanged();
    queueWrite(changes);
}

void Backend::setActuation(double mm) {
    const Device *d = device();
    if (!d) return;
    const double v = d->keyboard->capabilities().actuation.snap(mm);
    edit(ActuationChange, [v](KeySettings &k) { k.actuation = v; });
}

void Backend::setRapidTrigger(bool on) {
    edit(RapidTriggerChange, [on](KeySettings &k) { k.rapidTrigger = on; });
}

void Backend::setPress(double mm) {
    const Device *d = device();
    if (!d) return;
    const Capabilities c = d->keyboard->capabilities();
    const double v = c.sensitivity.snap(mm);
    edit(RapidTriggerChange, [v, c](KeySettings &k) {
        k.press = v;
        if (!c.separateRelease)
            k.release = v;
    });
}

void Backend::setRelease(double mm) {
    const Device *d = device();
    if (!d) return;
    const Capabilities c = d->keyboard->capabilities();
    const double v = c.sensitivity.snap(mm);
    edit(RapidTriggerChange, [v, c](KeySettings &k) {
        k.release = v;
        if (!c.separateRelease)
            k.press = v;
    });
}

void Backend::queueWrite(int changes) {
    if (Device *d = device())
        d->pending |= changes;
    m_writeTimer.start();
}

void Backend::writePending() {
    for (auto &d : m_devices) {
        if (!d->pending)
            continue;
        const int changes = d->pending;
        d->pending = 0;
        QString error;
        if (!d->keyboard->write(d->settings, changes, &error)) {
            setError(tr("Could not update the %1: %2").arg(d->keyboard->model(), error));
            // Show what the keyboard really holds rather than what failed to send.
            Settings actual;
            if (d->keyboard->read(&actual, nullptr)) {
                d->settings = actual;
                emit keysChanged();
                emit selectionChanged();
            }
            continue;
        }
        setError({});
        m_commitTimer.start();
    }
}

void Backend::commitAll() {
    for (auto &d : m_devices) {
        QString error;
        if (!d->keyboard->commit(&error))
            setError(tr("Could not save to the %1: %2").arg(d->keyboard->model(), error));
    }
}

void Backend::flush() {
    if (m_writeTimer.isActive()) {
        m_writeTimer.stop();
        writePending();
    }
    if (m_commitTimer.isActive()) {
        m_commitTimer.stop();
        commitAll();
    }
    if (m_saveTimer.isActive()) {
        m_saveTimer.stop();
        saveConfig();
    }
}

// --- lighting ---------------------------------------------------------------------

QVariantMap Backend::lighting() const {
    const Device *d = device();
    if (!d)
        return {};
    const Lighting &l = d->config.lighting;
    QStringList colors;
    for (const auto &c : l.colors)
        colors << c.name();
    QStringList theme;
    for (const auto &c : m_palette)
        theme << c.name();
    return {{"effect", l.effect}, {"palette", l.palette}, {"colors", colors}, {"themeColors", theme},
            {"speed", l.speed}, {"brightness", l.brightness}, {"direction", l.direction}, {"reactive", l.reactive},
            {"drawn", l.drawn()}};
}

void Backend::editLighting(const std::function<void(Lighting &)> &apply) {
    Device *d = device();
    if (!d)
        return;
    Lighting next = d->config.lighting;
    apply(next);
    if (next == d->config.lighting)
        return;
    remember(*d, LightingChange);
    d->config.lighting = next;
    d->effects.setLighting(next, m_palette);
    emit lightingChanged();
    emit selectionChanged();
    emit historyChanged();
    m_saveTimer.start();
}

void Backend::setEffect(const QString &effect) {
    static const QStringList known{"own", "static", "breathing", "spectrum", "wave", "aurora", "starlight", "rain"};
    if (known.contains(effect))
        editLighting([&](Lighting &l) { l.effect = effect; });
}

void Backend::setColor(const QString &color) {
    const QColor c = color.isEmpty() ? QColor() : QColor(color);
    if (!color.isEmpty() && !c.isValid())
        return;
    const Device *d = device();
    if (!d || d->selection.isEmpty())
        return;
    const QSet<QString> keys = d->selection;
    editLighting([&](Lighting &l) {
        // Colouring keys means static lighting; the keyboard's own effects can't take per-key colours.
        l.effect = "static";
        for (const auto &code : keys) {
            if (c.isValid())
                l.keyColors.insert(code, c);
            else
                l.keyColors.remove(code);
        }
    });
}

void Backend::setPalette(const QString &palette) {
    if (palette == "theme" || palette == "rainbow" || palette == "custom")
        editLighting([&](Lighting &l) {
            l.palette = palette;
            if (palette == "custom" && l.colors.isEmpty())
                l.colors = m_palette;
        });
}

void Backend::setPaletteColors(const QStringList &colors) {
    QList<QColor> list;
    for (const auto &name : colors)
        if (QColor c(name); c.isValid())
            list << c;
    if (!list.isEmpty())
        editLighting([&](Lighting &l) { l.palette = "custom"; l.colors = list.mid(0, 4); });
}

void Backend::setSpeed(double speed) {
    editLighting([&](Lighting &l) { l.speed = std::clamp(std::round(speed * 20) / 20, 0.25, 3.0); });
}

void Backend::setBrightness(double brightness) {
    editLighting([&](Lighting &l) { l.brightness = std::clamp(std::round(brightness * 100) / 100, 0.0, 1.0); });
}

void Backend::setDirection(const QString &direction) {
    static const QStringList known{"right", "left", "up", "down", "out"};
    if (known.contains(direction))
        editLighting([&](Lighting &l) { l.direction = direction; });
}

void Backend::setReactive(const QString &reactive) {
    static const QStringList known{"none", "fade", "ripple", "heatmap"};
    if (known.contains(reactive))
        editLighting([&](Lighting &l) { l.reactive = reactive; });
}

void Backend::previewPress(const QString &code) {
    if (Device *d = device())
        d->effects.press(code, m_clock.elapsed() / 1000.0);
}

bool Backend::eventFilter(QObject *watched, QEvent *event) {
    // Seen before the window's shortcuts, so every key counts; never consumed.
    if (event->type() == QEvent::KeyPress && !static_cast<QKeyEvent *>(event)->isAutoRepeat()) {
        const Device *d = device();
        if (d && d->config.lighting.reactive != QLatin1String("none")) {
            // On Wayland and X11 the native scan code is the Linux key code plus 8.
            const QString code = codeForEvdev(int(static_cast<QKeyEvent *>(event)->nativeScanCode()) - 8);
            if (!code.isEmpty())
                previewPress(code);
        }
    }
    return QObject::eventFilter(watched, event);
}

void Backend::previewTick() {
    Device *d = device();
    QVariantMap colors;
    if (d && d->config.lighting.drawn()) {
        const auto frame = d->effects.render(m_clock.elapsed() / 1000.0);
        for (auto it = frame.begin(); it != frame.end(); ++it)
            colors.insert(it.key(), it.value().name());
    }
    if (colors != m_ledColors) {
        m_ledColors = colors;
        emit ledColorsChanged();
    }
    const int frame = d && !d->frames.isEmpty() ? d->frames.frameAt(m_clock.elapsed()) : -1;
    if (frame != m_screenFrame) {
        m_screenFrame = frame;
        emit screenFrameChanged();
    }
}

void Backend::saveConfig() {
    bool needsService = false;
    for (auto &d : m_devices) {
        const DeviceConfig onDisk = DeviceConfig::load(d->configFile);
        if (!(onDisk.lighting == d->config.lighting) || !(onDisk.screen == d->config.screen))
            if (!d->config.save(d->configFile))
                setError(tr("Could not save %1").arg(d->configFile));
        needsService = needsService || d->config.lighting.drawn() || d->config.screen.active();
    }
    if (needsService)
        startService();
}

void Backend::startService() {
    if (m_serviceStarted || !m_live)
        return;
    m_serviceStarted = true;
    // The installed package ships a systemd user unit; enabling it brings
    // lighting back after a login. A build run from the source tree has no
    // unit, so it starts the service itself.
    QProcess systemctl;
    systemctl.start("systemctl", {"--user", "enable", "--now", "omni-lighting.service"});
    if (systemctl.waitForFinished(4000) && systemctl.exitStatus() == QProcess::NormalExit && systemctl.exitCode() == 0)
        return;
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {"lighting"}))
        setError(tr("Could not start the lighting service"));
}

// --- screen ---------------------------------------------------------------------------

QVariantMap Backend::screen() const {
    const Device *d = device();
    if (!d)
        return {};
    const ScreenSettings &s = d->config.screen;
    return {{"available", d->keyboard->screenSize().isValid()}, {"active", s.active()},
            {"name", QFileInfo(s.file).fileName()}, {"fit", s.fit}, {"dither", s.dither}, {"invert", s.invert},
            {"frames", d->frames.count()}, {"width", d->keyboard->screenSize().width()}, {"height", d->keyboard->screenSize().height()}};
}

QImage Backend::screenPreview() const {
    const Device *d = device();
    if (!d || d->frames.isEmpty() || m_screenFrame < 0 || m_screenFrame >= d->frames.count())
        return {};
    return d->frames.frame(m_screenFrame);
}

void Backend::loadScreenFrames(Device &d) {
    d.frames = {};
    if (d.config.screen.active() && d.keyboard->screenSize().isValid()) {
        QString error;
        if (!d.frames.load(d.config.screen.file, d.config.screen, d.keyboard->screenSize(), &error))
            setError(tr("Could not read the screen picture: %1").arg(error));
    }
    m_screenFrame = -1;
}

void Backend::editScreen(const std::function<void(ScreenSettings &)> &apply) {
    Device *d = device();
    if (!d || !d->keyboard->screenSize().isValid())
        return;
    ScreenSettings next = d->config.screen;
    apply(next);
    if (next == d->config.screen)
        return;
    remember(*d, ScreenChange);
    d->config.screen = next;
    loadScreenFrames(*d);
    emit screenChanged();
    emit historyChanged();
    m_saveTimer.start();
}

bool Backend::setScreenPicture(const QString &file) {
    Device *d = device();
    if (!d || !d->keyboard->screenSize().isValid())
        return false;
    QImageReader reader(file);
    if (!reader.canRead()) {
        setError(tr("%1 is not a picture omni can read").arg(QFileInfo(file).fileName()));
        return false;
    }
    // Keep a copy, so the service never depends on a file that might move,
    // and the original is never touched.
    const QString ext = QFileInfo(file).suffix().toLower();
    const QString stored = QFileInfo(d->configFile).absolutePath() + "/screens/"
                         + QFileInfo(d->configFile).completeBaseName() + "-" + QString::number(QDateTime::currentMSecsSinceEpoch()) + "." + ext;
    QDir().mkpath(QFileInfo(stored).absolutePath());
    if (!QFile::copy(file, stored)) {
        setError(tr("Could not copy %1").arg(QFileInfo(file).fileName()));
        return false;
    }
    setError({});
    editScreen([&](ScreenSettings &s) { s.file = stored; });
    return true;
}

void Backend::chooseScreenPicture() {
    openFileWithPortal(tr("Choose a picture for the keyboard's screen"), tr("Pictures"),
                       {"*.gif", "*.png", "*.jpg", "*.jpeg", "*.webp", "*.bmp"},
                       this, [this](const QString &file) {
                           if (!file.isEmpty())
                               setScreenPicture(file);
                       });
}

void Backend::setScreenFit(const QString &fit) {
    if (fit == "fit" || fit == "fill")
        editScreen([&](ScreenSettings &s) { s.fit = fit; });
}

void Backend::setScreenDither(bool dither) {
    editScreen([&](ScreenSettings &s) { s.dither = dither; });
}

void Backend::setScreenInvert(bool invert) {
    editScreen([&](ScreenSettings &s) { s.invert = invert; });
}

void Backend::clearScreen() {
    editScreen([&](ScreenSettings &s) { s.file.clear(); });
}
