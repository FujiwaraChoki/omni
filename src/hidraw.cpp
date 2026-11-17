#include "hidraw.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

static QByteArray readSmall(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool descriptorHasReportIds(const QByteArray &d) {
    for (int i = 0; i < d.size();) {
        const quint8 prefix = quint8(d[i]);
        if (prefix == 0xFE) {
            if (i + 1 >= d.size()) break;
            i += 3 + quint8(d[i + 1]);
            continue;
        }
        static const int sizes[4] = {0, 1, 2, 4};
        if ((prefix & 0xFC) == 0x84) // global Report ID
            return true;
        i += 1 + sizes[prefix & 0x03];
    }
    return false;
}

quint16 descriptorUsagePage(const QByteArray &d) {
    // Walk short items until the first global Usage Page (tag 0x04, type 1).
    for (int i = 0; i < d.size();) {
        const quint8 prefix = quint8(d[i]);
        if (prefix == 0xFE) { // long item: size in the next byte
            if (i + 1 >= d.size()) break;
            i += 3 + quint8(d[i + 1]);
            continue;
        }
        static const int sizes[4] = {0, 1, 2, 4};
        const int size = sizes[prefix & 0x03];
        if ((prefix & 0xFC) == 0x04 && i + size < d.size()) {
            quint32 value = 0;
            for (int b = 0; b < size; ++b)
                value |= quint32(quint8(d[i + 1 + b])) << (8 * b);
            return quint16(value);
        }
        i += 1 + size;
    }
    return 0;
}

QList<HidNode> enumerateHidraw(const QString &sysRoot, const QString &devRoot) {
    QList<HidNode> nodes;
    const auto entries = QDir(sysRoot).entryList({"hidraw*"}, QDir::Dirs | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto &name : entries) {
        const QString device = sysRoot + "/" + name + "/device";
        HidNode node;
        node.path = devRoot + "/" + name;
        for (const auto &line : QString::fromUtf8(readSmall(device + "/uevent")).split('\n')) {
            if (line.startsWith("HID_ID=")) {
                // HID_ID=0003:00001038:00001642
                const auto parts = line.mid(7).split(':');
                if (parts.size() == 3) {
                    node.vendor = quint16(parts[1].toUInt(nullptr, 16));
                    node.product = quint16(parts[2].toUInt(nullptr, 16));
                }
            } else if (line.startsWith("HID_UNIQ=")) {
                node.serial = line.mid(9).trimmed();
            }
        }
        // The HID device sits under its USB interface directory, named like 8-5:1.3.
        const QString interfaceDir = QFileInfo(QFileInfo(device).canonicalFilePath()).path();
        const QString interfaceName = QFileInfo(interfaceDir).fileName();
        const int dot = interfaceName.lastIndexOf('.');
        if (interfaceName.contains(':') && dot > 0) {
            node.interface = interfaceName.mid(dot + 1).toInt();
            node.usbDevice = interfaceName.section(':', 0, 0);
        }
        // Fake trees in tests store the interface number directly.
        const QByteArray stored = readSmall(device + "/interface_number").trimmed();
        if (!stored.isEmpty()) {
            node.interface = stored.toInt();
            node.usbDevice = QString::fromUtf8(readSmall(device + "/usb_device").trimmed());
        }
        const QByteArray descriptor = readSmall(device + "/report_descriptor");
        node.usagePage = descriptorUsagePage(descriptor);
        // 05 01 09 06: Generic Desktop, Keyboard. 85: a Report ID item.
        node.keyboard = descriptor.startsWith(QByteArray::fromHex("05010906"));
        node.reportIds = descriptorHasReportIds(descriptor);
        if (node.vendor)
            nodes << node;
    }
    return nodes;
}

HidDevice::~HidDevice() { close(); }

static QString systemError(const QString &what) {
    return QStringLiteral("%1: %2").arg(what, QString::fromLocal8Bit(strerror(errno)));
}

bool HidDevice::open(const QString &path, QString *error) {
    close();
    m_fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_CLOEXEC);
    if (m_fd < 0) {
        if (error) *error = systemError(path);
        return false;
    }
    m_path = path;
    return true;
}

void HidDevice::close() {
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
    m_locks = 0;
}

bool HidDevice::lock(int timeoutMs) {
    if (m_fd < 0)
        return false;
    if (m_locks > 0) {
        ++m_locks;
        return true;
    }
    for (int waited = 0;; waited += 2) {
        if (::flock(m_fd, LOCK_EX | LOCK_NB) == 0) {
            m_locks = 1;
            return true;
        }
        if (waited >= timeoutMs)
            return false;
        ::usleep(2000);
    }
}

void HidDevice::unlock() {
    if (m_locks > 0 && --m_locks == 0)
        ::flock(m_fd, LOCK_UN);
}

bool HidDevice::write(const QByteArray &data, QString *error) {
    const ssize_t written = ::write(m_fd, data.constData(), size_t(data.size()));
    if (written != data.size()) {
        if (error) *error = systemError(QStringLiteral("write"));
        return false;
    }
    return true;
}

QByteArray HidDevice::read(int timeoutMs) {
    pollfd p{m_fd, POLLIN, 0};
    if (::poll(&p, 1, timeoutMs) <= 0 || !(p.revents & POLLIN))
        return {};
    QByteArray buffer(1024, 0);
    const ssize_t n = ::read(m_fd, buffer.data(), size_t(buffer.size()));
    return n > 0 ? buffer.left(int(n)) : QByteArray();
}

void HidDevice::drain() {
    while (!read(0).isEmpty()) {}
}

bool HidDevice::setFeature(const QByteArray &data, QString *error) {
    QByteArray buffer = data;
    if (::ioctl(m_fd, HIDIOCSFEATURE(buffer.size()), buffer.data()) < 0) {
        if (error) *error = systemError(QStringLiteral("set feature"));
        return false;
    }
    return true;
}

QByteArray HidDevice::getFeature(quint8 reportId, int length, QString *error) {
    QByteArray buffer(length, 0);
    buffer[0] = char(reportId);
    const int n = ::ioctl(m_fd, HIDIOCGFEATURE(buffer.size()), buffer.data());
    if (n < 0) {
        if (error) *error = systemError(QStringLiteral("get feature"));
        return {};
    }
    return buffer.left(n);
}
