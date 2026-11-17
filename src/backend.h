#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include <functional>
#include <memory>
#include <vector>

#include "config.h"
#include "effects.h"
#include "keyboard.h"
#include "screen.h"

// The app's state: the keyboards that are plugged in, the one being edited,
// its keys, settings, lighting and screen picture, and which keys are
// selected. QML reads it through properties and changes it through the
// invokables.
//
// Key settings (actuation, Rapid Trigger) go straight to the keyboard, shortly
// after the last change. Lighting and the screen picture are saved to the
// keyboard's settings file, which the lighting service draws from; the window
// draws the same lighting itself as a preview.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList keys READ keys NOTIFY keysChanged)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY devicesChanged)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap lighting READ lighting NOTIFY lightingChanged)
    Q_PROPERTY(QVariantMap ledColors READ ledColors NOTIFY ledColorsChanged)
    Q_PROPERTY(QVariantMap screen READ screen NOTIFY screenChanged)
    Q_PROPERTY(int screenFrame READ screenFrame NOTIFY screenFrameChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

public:
    // `scan` lists the hidraw nodes and `configDirectory` holds the per-keyboard
    // settings files; tests replace both and keep the service from starting.
    explicit Backend(QObject *parent = nullptr, std::function<QList<HidNode>()> scan = {},
                     const QString &configDirectory = {}, bool watchDevices = true);
    ~Backend() override;

    QVariantList devices() const;
    int current() const { return m_current; }
    void setCurrent(int index);
    QVariantList keys() const;
    QVariantMap capabilities() const;
    QStringList selection() const;
    QVariantMap selected() const;
    QVariantMap lighting() const;
    QVariantMap ledColors() const { return m_ledColors; }
    QVariantMap screen() const;
    int screenFrame() const { return m_screenFrame; }
    QImage screenPreview() const;
    QString error() const { return m_error; }
    bool canUndo() const;
    bool canRedo() const;

    Q_INVOKABLE void rescan();
    Q_INVOKABLE void select(const QString &code, bool extend);
    Q_INVOKABLE void setSelection(const QStringList &codes, bool extend);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void selectNone();
    Q_INVOKABLE void setActuation(double mm);
    Q_INVOKABLE void setRapidTrigger(bool on);
    Q_INVOKABLE void setPress(double mm);
    Q_INVOKABLE void setRelease(double mm);

    Q_INVOKABLE void setEffect(const QString &effect);
    Q_INVOKABLE void setColor(const QString &color); // static colour of the selected keys; "" turns them off
    Q_INVOKABLE void setPalette(const QString &palette); // theme, rainbow, custom
    Q_INVOKABLE void setPaletteColors(const QStringList &colors);
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void setBrightness(double brightness);
    Q_INVOKABLE void setDirection(const QString &direction);
    Q_INVOKABLE void setReactive(const QString &reactive);

    Q_INVOKABLE void chooseScreenPicture();          // through the desktop's file chooser
    // Copies the file and shows it; false if it is not a picture. `name` defaults to the file's own name;
    // `fit` ("fit" or "fill") replaces the current choice when given.
    Q_INVOKABLE bool setScreenPicture(const QString &file, const QString &name = {}, const QString &fit = {});
    Q_INVOKABLE void setScreenFit(const QString &fit);
    Q_INVOKABLE void setScreenDither(bool dither);
    Q_INVOKABLE void setScreenInvert(bool invert);
    Q_INVOKABLE void clearScreen();

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Sends and saves anything still waiting; the window calls it on close.
    Q_INVOKABLE void flush();

    // Adds a keyboard directly, for tests and for drivers found outside a scan.
    void adopt(Keyboard *keyboard, const QString &location);
    // Tells the preview a key went down (reactive effects).
    void previewPress(const QString &code);
    // Picks up a new Omarchy theme's colours for the "theme" palette.
    void reloadTheme();

signals:
    void devicesChanged();
    void keysChanged();
    void selectionChanged();
    void lightingChanged();
    void ledColorsChanged();
    void screenChanged();
    void screenFrameChanged();
    void errorChanged();
    void historyChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum { LightingChange = 8, ScreenChange = 16 };
    struct Snapshot {
        Settings settings;
        Lighting lighting;
        ScreenSettings screen;
        int changes = 0;
    };
    struct Device {
        std::unique_ptr<Keyboard> keyboard;
        QString location; // usb port + product, to notice a replug
        QString configFile;
        Settings settings;
        DeviceConfig config;
        Effects effects;
        ScreenFrames frames;
        QList<KeyGeometry> layout;
        QSet<QString> selection;
        QList<Snapshot> undo, redo;
        int pending = 0;
    };

    Device *device();
    const Device *device() const;
    Snapshot snapshot(const Device &d, int changes) const;
    // Records an undo step (one per drag) before an edit of the given kind.
    void remember(Device &d, int changes);
    // Applies `edit` to every selected key, records it for undo and queues the write.
    void edit(int changes, const std::function<void(KeySettings &)> &edit);
    void editLighting(const std::function<void(Lighting &)> &edit);
    void editScreen(const std::function<void(ScreenSettings &)> &edit);
    void restore(Device &d, const Snapshot &s);
    void queueWrite(int changes);
    void writePending();
    void commitAll();
    void saveConfig();
    void loadScreenFrames(Device &d);
    void startService();
    void previewTick();
    void setError(const QString &error);

    std::function<QList<HidNode>()> m_scan;
    QString m_configDirectory;
    bool m_live; // a real session: start the service, watch devices
    std::vector<std::unique_ptr<Device>> m_devices;
    int m_current = -1;
    QString m_error;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer, m_writeTimer, m_commitTimer, m_saveTimer, m_previewTimer;
    QElapsedTimer m_lastEdit, m_clock;
    int m_lastChanges = 0;
    bool m_serviceStarted = false;
    QVariantMap m_ledColors;
    QList<QColor> m_palette;
    int m_screenFrame = -1;
};
