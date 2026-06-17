#include "SerialWorker.h"

#include <QDebug>

static inline quint16 u16_from_be(quint8 hi, quint8 lo)
{
    return (static_cast<quint16>(hi) << 8) | static_cast<quint16>(lo);
}

SerialWorker::SerialWorker(QObject* parent)
    : QObject(parent)
{
    m_ring.resize(m_ringCapacity);
    m_timer.start();
}

SerialWorker::~SerialWorker()
{
    closePort();
}

// ======================================================
// 串口控制
// ======================================================
bool SerialWorker::openPort(const QString& portName, int baudRate)
{
    closePort();   // 确保干净状态

    m_port = new QSerialPort(this);
    m_port->setPortName(portName);
    m_port->setBaudRate(baudRate);
    m_port->setDataBits(QSerialPort::Data8);
    m_port->setParity(QSerialPort::NoParity);
    m_port->setStopBits(QSerialPort::OneStop);
    m_port->setFlowControl(QSerialPort::NoFlowControl);

    connect(m_port, &QSerialPort::readyRead,
        this, &SerialWorker::onReadyRead);
    connect(m_port, &QSerialPort::errorOccurred,
        this, &SerialWorker::onErrorOccurred);

    if (!m_port->open(QIODevice::ReadWrite)) {
        const QString msg = QString("Open %1 failed: %2")
            .arg(portName, m_port->errorString());
        emit portError(msg);

        m_port->deleteLater();
        m_port = nullptr;
        return false;
    }

    // ---------- Reset stats ----------
    m_rxSeq = 0;
    m_rxBytes = 0;
    m_rxFrames = 0;
    m_badFrames = 0;
    m_resyncDrops = 0;

    // ---------- Reset ring ----------
    m_head = m_tail = m_count = 0;

    emit portOpened(portName, baudRate);
    return true;
}

void SerialWorker::closePort()
{
    if (!m_port) return;

    if (m_port->isOpen())
        m_port->close();

    emit portClosed();

    m_port->deleteLater();
    m_port = nullptr;
}

bool SerialWorker::isOpen() const
{
    return (m_port && m_port->isOpen());
}

void SerialWorker::writeBytes(const QByteArray& data)
{
    if (!m_port || !m_port->isOpen()) return;
    m_port->write(data);
}

// ======================================================
// 串口事件
// ======================================================
void SerialWorker::onReadyRead()
{
    if (!m_port) return;

    const QByteArray data = m_port->readAll();
    if (data.isEmpty()) return;

    emit rawBytesForUi(data);

    m_rxBytes += static_cast<quint64>(data.size());
    appendToRing(data);
    tryParseFrames();

    // 简单起见：每次读都发一次统计
    emit statsUpdated(m_rxBytes, m_rxFrames, m_badFrames, m_resyncDrops);
}

void SerialWorker::onErrorOccurred(QSerialPort::SerialPortError err)
{
    if (!m_port) return;
    if (err == QSerialPort::NoError) return;

    emit portError(QString("Serial error: %1")
        .arg(m_port->errorString()));
}

// ======================================================
// Ring Buffer
// ======================================================
int SerialWorker::ringSize() const
{
    return m_count;
}

bool SerialWorker::ringPeek(int offset, char& out) const
{
    if (offset < 0 || offset >= m_count) return false;
    int idx = m_head + offset;
    if (idx >= m_ringCapacity) idx -= m_ringCapacity;
    out = m_ring[idx];
    return true;
}

bool SerialWorker::ringRead(char& out)
{
    if (m_count <= 0) return false;
    out = m_ring[m_head];
    m_head = (m_head + 1) % m_ringCapacity;
    m_count--;
    return true;
}

void SerialWorker::ringDrop(int n)
{
    if (n <= 0) return;
    if (n > m_count) n = m_count;

    m_head = (m_head + n) % m_ringCapacity;
    m_count -= n;

    m_resyncDrops += static_cast<quint64>(n);
}

void SerialWorker::appendToRing(const QByteArray& data)
{
    const int incoming = data.size();
    const int freeSpace = m_ringCapacity - m_count;

    if (incoming > freeSpace) {
        // 丢弃最旧数据，避免溢出
        ringDrop(incoming - freeSpace);
    }

    for (int i = 0; i < incoming; ++i) {
        m_ring[m_tail] = data.at(i);
        m_tail = (m_tail + 1) % m_ringCapacity;
        m_count++;
    }
}

// ======================================================
// 帧解析（7 字节协议）
// ======================================================
bool SerialWorker::findNextHeader()
{
    for (int i = 0; i < m_count; ++i) {
        char c{};
        ringPeek(i, c);
        if (static_cast<quint8>(c) == kHeader) {
            if (i > 0) ringDrop(i);
            return true;
        }
    }
    return false;
}

void SerialWorker::tryParseFrames()
{
    while (ringSize() >= 1) {

        // 1) 对齐 header
        char c0{};
        ringPeek(0, c0);
        if (static_cast<quint8>(c0) != kHeader) {
            if (!findNextHeader()) {
                ringDrop(ringSize());
                return;
            }
        }

        // 2) 长度检查
        if (ringSize() < kFrameSize) return;

        // 3) peek frame
        char b[kFrameSize];
        for (int i = 0; i < kFrameSize; ++i)
            ringPeek(i, b[i]);

        if (static_cast<quint8>(b[0]) != kHeader ||
            static_cast<quint8>(b[6]) != kTail) {
            m_badFrames++;
            ringDrop(1);   // 丢掉当前 header，重新对齐
            continue;
        }

        // 4) 解析
        RawFrame f;
        f.channelCase = static_cast<quint8>(b[1]);
        f.ampCode = u16_from_be(static_cast<quint8>(b[2]),
            static_cast<quint8>(b[3]));
        f.phaCode = u16_from_be(static_cast<quint8>(b[4]),
            static_cast<quint8>(b[5]));
        f.rxSeq = m_rxSeq++;
        f.rxTimeNs = m_timer.nsecsElapsed();

        // 5) consume
        ringDrop(kFrameSize);

        m_rxFrames++;
        emit rawFrameReady(f);
    }
}
