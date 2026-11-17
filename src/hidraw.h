#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// One /dev/hidrawN node, as the kernel describes it in sysfs.
struct HidNode {
    QString path;          // /dev/hidrawN
    quint16 vendor = 0;
    quint16 product = 0;
    int interface = -1;    // USB interface number, -1 when the bus has none
    quint16 usagePage = 0; // first usage page in the report descriptor
    QString serial;        // HID_UNIQ, often empty
    QString usbDevice;     // USB port path like "8-5"; groups the interfaces of one keyboard
    bool keyboard = false; // declares a Generic Desktop / Keyboard collection
    bool reportIds = false; // input reports start with a report ID byte
};

// Lists every hidraw node. The sysfs root is a parameter so tests can hand in
// a fake tree.
QList<HidNode> enumerateHidraw(const QString &sysRoot = "/sys/class/hidraw", const QString &devRoot = "/dev");

// The first usage page declared in a HID report descriptor, or 0.
quint16 descriptorUsagePage(const QByteArray &descriptor);
bool descriptorHasReportIds(const QByteArray &descriptor);

// A small blocking wrapper over one hidraw node. Every call is short (the
// keyboards answer within milliseconds), so the backend runs them on the GUI
// thread between frames rather than juggling a worker thread.
class HidDevice {
public:
    HidDevice() = default;
    ~HidDevice();
    HidDevice(const HidDevice &) = delete;
    HidDevice &operator=(const HidDevice &) = delete;

    bool open(const QString &path, QString *error = nullptr);
    void close();
    bool isOpen() const { return m_fd >= 0; }
    QString path() const { return m_path; }

    // Output report. `data` starts with the report ID byte (0 when unused).
    bool write(const QByteArray &data, QString *error = nullptr);
    // One input report, or an empty array after `timeoutMs`.
    QByteArray read(int timeoutMs);
    // Throws away input reports that are already queued, so the next read
    // is the answer to the next request.
    void drain();
    // Feature reports. `data`/the result start with the report ID byte.
    bool setFeature(const QByteArray &data, QString *error = nullptr);
    QByteArray getFeature(quint8 reportId, int length, QString *error = nullptr);

    // Requests and answers share one channel, so the window and the lighting
    // service take turns: each holds this lock for one request and its answer.
    // Nested locks in one process are counted. Tools that ignore the lock
    // (OpenRGB) can still interfere.
    bool lock(int timeoutMs = 1000);
    void unlock();

private:
    int m_fd = -1;
    int m_locks = 0;
    QString m_path;
};

class HidLock {
public:
    explicit HidLock(HidDevice &device) : m_device(device), m_held(device.lock()) {}
    ~HidLock() { if (m_held) m_device.unlock(); }
    bool held() const { return m_held; }
private:
    HidDevice &m_device;
    bool m_held;
};
