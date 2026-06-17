#pragma once

#include <QObject>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QtGlobal>

#include "RawFrame.h"
#include "SamplePoint.h"
#include "ScanBuffer.h"
#include "ChannelMap.h"

class DataProcessWorker : public QObject
{
    Q_OBJECT
public:
    explicit DataProcessWorker(QObject* parent = nullptr);

    // DataProcessWorker.h 里增加
public:
    void setRealtimeNormalizeEnabled(bool on) { m_rtNormEnabled = on; }
    void setRealtimeNormalizeUseScale(bool on) { m_rtNormUseScale = on; }

private:
    bool m_rtNormEnabled = true;     // 默认开：消条纹
    bool m_rtNormUseScale = true;    // 默认开：更强的通道一致性
    qint64 m_lastEmitNs = 0;
    qint64 m_emitIntervalNs = 50'000'000; // 50ms -> 20FPS



public slots:
    void setChannelMap(const ChannelMap& map);
    void setUseSevenChannels(bool on);
    void setPointsPerChannel(int n);
    void setAdcConfig(double rangeV, int bits);

    // ✅第二批新增：ring/非ring + 最大缓存
    void setRingBufferEnabled(bool on);
    void setMaxFrames(int n);

    // 清空缓存（按钮/重新开始）
    void reset();

    // ✅给 invokeMethod("clearBuffer") 用：必须是 slot
    void clearBuffer() { reset(); }

    void onRawFrame(const RawFrame& f);

signals:
    void sampleReady(const SamplePoint& sp);
    void bufferReady(const ScanBuffer& buf);
    void statusUpdated(int channelCount, int pointsPerChannel, int minCount, int maxCount);

private:
    double codeToVolt(quint16 code) const;
    bool acceptLogical(int logicalId) const;
    bool tryEmitBuffer(qint64 timeNs);

    // ✅第二批新增：裁剪每通道缓存
    void trimChannelBuffersIfNeeded();

private:
    ChannelMap m_map = ChannelMap::passthrough(15);
    bool m_useSeven = true;

    int m_pointsPerChannel = 1000;

    // ADC 配置
    double m_adcRangeV = 5.0;
    int    m_adcBits = 16;
    int    m_adcScale = 1 << 15;

    // 缓存：logicalId -> amp/pha
    QHash<int, QVector<double>> m_ampByCh;
    QHash<int, QVector<double>> m_phaByCh;
    QSet<int> m_seenChannels;

    // ✅第二批新增：ring/非ring + 最大缓存长度（每通道点数）
    bool m_ringEnabled = true;
    int  m_maxFramesPerCh = 200;

    quint64 m_bufferSeq = 0;
};
