#pragma once

#include <QObject>
#include <QSerialPort>
#include <QByteArray>
#include <QElapsedTimer>

#include "RawFrame.h"

/**
 * SerialWorker
 * - 在 worker 线程中创建/打开 QSerialPort
 * - readyRead() -> append -> tryParseFrames() -> emit rawFrameReady(RawFrame)
 *
 * 协议：固定 7 字节帧
 *   [0] 0xFF  (header)
 *   [6] 0x55  (tail)
 */
class SerialWorker : public QObject
{
    Q_OBJECT
public:
    explicit SerialWorker(QObject* parent = nullptr);
    ~SerialWorker() override;

    // ===== 串口控制（建议在 worker 所在线程调用；UI 可用 QueuedConnection 调用）=====
    Q_INVOKABLE bool openPort(const QString& portName, int baudRate);
    Q_INVOKABLE void closePort();
    bool isOpen() const;

    // 可选：下发控制命令
    Q_INVOKABLE void writeBytes(const QByteArray& data);

signals:
    // 串口状态
    void portOpened(const QString& portName, int baudRate);
    void portClosed();
    void portError(const QString& message);
    void rawBytesForUi(const QByteArray& raw);

    // 数据帧输出（给 DataProcessWorker）
    void rawFrameReady(const RawFrame& frame);

    // 可选：统计信息（给 UI 显示）
    void statsUpdated(quint64 rxBytes,
        quint64 rxFrames,
        quint64 badFrames,
        quint64 resyncDrops);

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError err);

private:
    // ===== 协议常量 =====
    static constexpr int   kFrameSize = 7;
    static constexpr quint8 kHeader = 0xFF;
    static constexpr quint8 kTail = 0x55;

    // ===== 解析入口 =====
    void appendToRing(const QByteArray& data);
    void tryParseFrames();
    bool findNextHeader();   // 找到 header(0xFF)，丢弃之前的数据

    // ===== ring buffer 操作 =====
    bool ringPeek(int offset, char& out) const;
    bool ringRead(char& out);
    int  ringSize() const;
    void ringDrop(int n);

private:
    // 串口对象（在 worker 线程使用）
    QSerialPort* m_port = nullptr;

    // 环形缓冲区（轻量）
    QByteArray m_ring;
    int m_head = 0;   // 读指针
    int m_tail = 0;   // 写指针
    int m_count = 0;  // 当前有效字节数

    // 统计
    QElapsedTimer m_timer;
    quint64 m_rxBytes = 0;
    quint64 m_rxFrames = 0;
    quint64 m_badFrames = 0;
    quint64 m_resyncDrops = 0;
    quint64 m_rxSeq = 0;

    // 配置
    int m_ringCapacity = (1 << 16); // 64KB
};
