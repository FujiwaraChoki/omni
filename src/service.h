#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QSocketNotifier>
#include <QTimer>
#include <memory>
#include <vector>

#include "config.h"
#include "effects.h"
#include "keyboard.h"
#include "screen.h"
#include "theme.h"

// Reads one of the keyboard's own key-report nodes and says which keys go
// down. Reports are parsed in memory and dropped; nothing is stored.
class KeyInput : public QObject {
    Q_OBJECT
public:
    explicit KeyInput(const HidNode &node, QObject *parent = nullptr);
    ~KeyInput() override;
    bool isOpen() const { return m_fd >= 0; }
    // The usages held down in one report: 8-byte boot reports and 256-bit
    // bitmaps (NKRO), with or without a leading report ID.
    static QSet<quint8> pressedIn(const QByteArray &report, bool reportIds);

signals:
    void pressed(const QString &code);

private:
    int m_fd = -1;
    bool m_reportIds = false;
    QSet<quint8> m_down;
    QSocketNotifier *m_notifier = nullptr;
};

// `omni lighting`: draws lighting and screen pictures for every connected
// keyboard, from the settings the window saves. Runs as a systemd user
// service so lighting keeps going after the window closes.
class LightingService : public QObject {
    Q_OBJECT
public:
    explicit LightingService(QObject *parent = nullptr);
    ~LightingService() override;
    // Hands lighting and screens back to the keyboards.
    void release();

private:
    struct Device {
        std::unique_ptr<Keyboard> keyboard;
        QString location;
        QString configFile;
        DeviceConfig config;
        Effects effects;
        ScreenFrames frames;
        QJsonObject framesFor; // the screen settings `frames` were made with
        std::vector<std::unique_ptr<KeyInput>> inputs;
        bool lit = false, screenOn = false;
        QHash<QString, QColor> lastFrame;
        qint64 lastLight = -1000000, lastScreen = -1000000;
        int shownFrame = -1;
        QString lastError;
    };

    void rescan();
    void reload();
    void reload(Device &d);
    void tick();
    void report(Device &d, const QString &error);

    std::vector<std::unique_ptr<Device>> m_devices;
    QFileSystemWatcher m_devWatcher, m_configWatcher;
    QTimer m_tick, m_rescanTimer, m_reloadTimer;
    QElapsedTimer m_clock;
    Theme m_theme;
    QList<QColor> m_palette;
};
