#include "backend.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

Backend::Backend(QObject *parent, std::function<QList<HidNode>()> scan, const QString &configDirectory, bool watchDevices)
    : QObject(parent),
      m_scan(scan ? scan : [] { return enumerateHidraw(); }),
      m_configDirectory(configDirectory.isEmpty()
                            ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/omni"
                            : configDirectory) {
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
    if (watchDevices) {
        // hidraw nodes appear and vanish in /dev as keyboards come and go.
        m_watcher.addPath("/dev");
        connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_rescanTimer.start(); });
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
    // Colours live only on the host; put them back on the keyboard.
    loadColors(*d);
    if (std::any_of(d->settings.begin(), d->settings.end(), [](const KeySettings &k) { return k.color.isValid(); })
        && !keyboard->write(d->settings, ColorChange, &error))
        setError(tr("Could not set the colours: %1").arg(error));
    m_devices.push_back(std::move(d));
    if (m_current < 0)
        m_current = 0;
    emit devicesChanged();
    emit keysChanged();
}

QVariantList Backend::devices() const {
    QVariantList list;
    for (const auto &d : m_devices)
        list << QVariantMap{{"model", d->keyboard->model()}, {"firmware", d->keyboard->firmware()}};
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
    emit historyChanged();
}

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
            {"color", k.color.isValid() ? k.color.name() : QString()},
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
        {"sensitivityMin", c.sensitivity.min}, {"sensitivityMax", c.sensitivity.max}, {"sensitivityStep", c.sensitivity.step},
        {"separateRelease", c.separateRelease}, {"perKeyColor", c.perKeyColor},
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
        const QString c = k.color.isValid() ? k.color.name() : QString();
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
    if (!extend) {
        d->selection = {code};
    } else if (d->selection.contains(code)) {
        d->selection.remove(code);
    } else {
        d->selection.insert(code);
    }
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

void Backend::edit(int changes, const std::function<void(KeySettings &)> &apply) {
    Device *d = device();
    if (!d || d->selection.isEmpty())
        return;
    Settings next = d->settings;
    const bool analogOnly = changes & (ActuationChange | RapidTriggerChange);
    for (const auto &code : d->selection)
        if (!analogOnly || d->keyboard->isAnalog(code))
            apply(next[code]);
    if (next == d->settings)
        return;
    // A slider drag is one step of undo, not one per frame.
    const bool continues = m_lastEdit.isValid() && m_lastEdit.elapsed() < 700 && m_lastChanges == changes && !d->undo.isEmpty();
    if (!continues) {
        d->undo << qMakePair(d->settings, changes);
        d->redo.clear();
    }
    m_lastEdit.restart();
    m_lastChanges = changes;
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

void Backend::setColor(const QString &color) {
    const QColor c = color.isEmpty() ? QColor() : QColor(color);
    if (!color.isEmpty() && !c.isValid())
        return;
    edit(ColorChange, [c](KeySettings &k) { k.color = c; });
}

bool Backend::canUndo() const { const Device *d = device(); return d && !d->undo.isEmpty(); }
bool Backend::canRedo() const { const Device *d = device(); return d && !d->redo.isEmpty(); }

void Backend::undo() {
    Device *d = device();
    if (!d || d->undo.isEmpty())
        return;
    const auto [settings, changes] = d->undo.takeLast();
    d->redo << qMakePair(d->settings, changes);
    d->settings = settings;
    m_lastEdit.invalidate();
    emit keysChanged();
    emit selectionChanged();
    emit historyChanged();
    queueWrite(changes);
}

void Backend::redo() {
    Device *d = device();
    if (!d || d->redo.isEmpty())
        return;
    const auto [settings, changes] = d->redo.takeLast();
    d->undo << qMakePair(d->settings, changes);
    d->settings = settings;
    m_lastEdit.invalidate();
    emit keysChanged();
    emit selectionChanged();
    emit historyChanged();
    queueWrite(changes);
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
                for (auto it = actual.begin(); it != actual.end(); ++it)
                    it->color = d->settings.value(it.key()).color;
                d->settings = actual;
                emit keysChanged();
                emit selectionChanged();
            }
            continue;
        }
        setError({});
        if (changes & ColorChange)
            saveColors(*d);
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
}

QString Backend::colorFile(const Device &d) const {
    QString name = d.keyboard->identity();
    name.replace(QRegularExpression("[^A-Za-z0-9._-]"), "_");
    return m_configDirectory + "/" + name + ".json";
}

void Backend::loadColors(Device &d) {
    QFile file(colorFile(d));
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject colors = QJsonDocument::fromJson(file.readAll()).object().value("colors").toObject();
    for (auto it = colors.begin(); it != colors.end(); ++it)
        if (d.settings.contains(it.key()))
            d.settings[it.key()].color = QColor(it.value().toString());
}

void Backend::saveColors(const Device &d) {
    QJsonObject colors;
    for (auto it = d.settings.begin(); it != d.settings.end(); ++it)
        if (it->color.isValid())
            colors.insert(it.key(), it->color.name());
    QDir().mkpath(m_configDirectory);
    // Written beside the target and renamed into place.
    QSaveFile file(colorFile(d));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{"model", d.keyboard->model()}, {"colors", colors}}).toJson());
    file.commit();
}
