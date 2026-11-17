#pragma once

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include <functional>
#include <memory>
#include <vector>

#include "keyboard.h"

// The app's state: the keyboards that are plugged in, the one being edited,
// its keys and settings, and which keys are selected. QML reads it through
// properties and changes it through the invokables; writes reach the keyboard
// shortly after the last change, so dragging a slider sends a handful of
// frames rather than hundreds.
class Backend : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int current READ current WRITE setCurrent NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList keys READ keys NOTIFY keysChanged)
    Q_PROPERTY(QVariantMap capabilities READ capabilities NOTIFY devicesChanged)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

public:
    // `scan` lists the hidraw nodes and `configDirectory` keeps what the
    // keyboards cannot store themselves (per-key colours); tests replace both.
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
    Q_INVOKABLE void setColor(const QString &color); // "" hands lighting back to the keyboard
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    // Sends anything still waiting for the debounce; the window calls it on close.
    Q_INVOKABLE void flush();

    // Adds a keyboard directly, for tests and for drivers found outside a scan.
    void adopt(Keyboard *keyboard, const QString &location);

signals:
    void devicesChanged();
    void keysChanged();
    void selectionChanged();
    void errorChanged();
    void historyChanged();

private:
    struct Device {
        std::unique_ptr<Keyboard> keyboard;
        QString location; // usb port + product, to notice a replug
        Settings settings;
        QList<KeyGeometry> layout;
        QSet<QString> selection;
        QList<QPair<Settings, int>> undo, redo; // snapshot and what it touched
        int pending = 0;
    };

    Device *device();
    const Device *device() const;
    // Applies `edit` to every selected key, records it for undo and queues the write.
    void edit(int changes, const std::function<void(KeySettings &)> &edit);
    void queueWrite(int changes);
    void writePending();
    void setError(const QString &error);
    void loadColors(Device &d);
    void saveColors(const Device &d);
    QString colorFile(const Device &d) const;

    std::function<QList<HidNode>()> m_scan;
    QString m_configDirectory;
    std::vector<std::unique_ptr<Device>> m_devices;
    int m_current = -1;
    QString m_error;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
    QTimer m_writeTimer;
    QElapsedTimer m_lastEdit;
    int m_lastChanges = 0;
};
