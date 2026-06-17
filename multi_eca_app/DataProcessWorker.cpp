#include "DataProcessWorker.h"

#include <algorithm>
#include <limits>

DataProcessWorker::DataProcessWorker(QObject* parent)
    : QObject(parent)
{
    m_adcRangeV = 5.0;
    m_adcBits = 16;
    m_adcScale = 1 << (m_adcBits - 1);
}

void DataProcessWorker::setChannelMap(const ChannelMap& map) { m_map = map; }
void DataProcessWorker::setUseSevenChannels(bool on) { m_useSeven = on; }

void DataProcessWorker::setPointsPerChannel(int n)
{
    if (n < 1) n = 1;
    m_pointsPerChannel = n;
}

void DataProcessWorker::setAdcConfig(double rangeV, int bits)
{
    if (rangeV <= 0) rangeV = 5.0;
    if (bits < 8) bits = 16;
    m_adcRangeV = rangeV;
    m_adcBits = bits;
    m_adcScale = 1 << (m_adcBits - 1);
}

// ✅第二批新增
void DataProcessWorker::setRingBufferEnabled(bool on)
{
    m_ringEnabled = on;
    // 切换策略时，建议不清空，让 UI/逻辑连续；如果你想切换就重新开始，可在此 reset()
}

void DataProcessWorker::setMaxFrames(int n)
{
    if (n < 1) n = 1;
    m_maxFramesPerCh = n;
    trimChannelBuffersIfNeeded();
}

void DataProcessWorker::reset()
{
    m_ampByCh.clear();
    m_phaByCh.clear();
    m_seenChannels.clear();
    m_bufferSeq = 0;
}

double DataProcessWorker::codeToVolt(quint16 code) const
{
    const int full = 1 << m_adcBits;
    int signedCode = static_cast<int>(code);
    if (signedCode >= m_adcScale) signedCode -= full;
    return (static_cast<double>(signedCode) / static_cast<double>(m_adcScale)) * m_adcRangeV;
}

bool DataProcessWorker::acceptLogical(int logicalId) const
{
    if (logicalId < 0) return false;
    if (!m_useSeven) return logicalId <= 14;
    return (logicalId >= 0 && logicalId <= 6);
}

void DataProcessWorker::trimChannelBuffersIfNeeded()
{
    if (m_maxFramesPerCh <= 0) return;
    const int slack = 200; // 缓冲余量，减少频繁拷贝

    for (int ch : m_seenChannels) {
        auto& a = m_ampByCh[ch];
        auto& p = m_phaByCh[ch];

        if (a.size() > m_maxFramesPerCh + slack) {
            const int extra = a.size() - m_maxFramesPerCh;
            a.remove(0, extra);   // 仍会移动，但比 mid 少一次分配
        }
        if (p.size() > m_maxFramesPerCh + slack) {
            const int extra = p.size() - m_maxFramesPerCh;
            p.remove(0, extra);
        }
    }
}


void DataProcessWorker::onRawFrame(const RawFrame& f)
{
    const int logicalId = m_map.toLogical(static_cast<int>(f.channelCase));
    if (!acceptLogical(logicalId)) return;

    const double ampVolt = codeToVolt(f.ampCode);
    const double phaVolt = codeToVolt(f.phaCode);

    SamplePoint sp;
    sp.channelId = logicalId;

    // 同时带上原始码值
    sp.ampCode = f.ampCode;
    sp.phaCode = f.phaCode;

    // 同时带上电压值
    sp.ampVolt = ampVolt;
    sp.phaVolt = phaVolt;

    sp.seq = f.rxSeq;
    sp.timeNs = f.rxTimeNs;
    emit sampleReady(sp);

    auto& a = m_ampByCh[logicalId];
    auto& p = m_phaByCh[logicalId];

    // ✅ 缓存/成像：用电压
    const double ampToStore = ampVolt;
    const double phaToStore = phaVolt;

    // non-ring：满了 freeze，不追加，但流程继续
    if (!m_ringEnabled && m_maxFramesPerCh > 0) {
        if (a.size() < m_maxFramesPerCh && p.size() < m_maxFramesPerCh) {
            a.push_back(ampToStore);
            p.push_back(phaToStore);
        }
    }
    else {
        a.push_back(ampToStore);
        p.push_back(phaToStore);
    }

    m_seenChannels.insert(logicalId);

    static int s_trimTick = 0;
    if (m_ringEnabled && (++s_trimTick % 50 == 0)) { // 每 50 帧 trim 一次
        trimChannelBuffersIfNeeded();
    }

    int minCount = std::numeric_limits<int>::max();
    int maxCount = 0;
    for (int ch : m_seenChannels) {
        const int sz = m_ampByCh[ch].size();
        minCount = std::min(minCount, sz);
        maxCount = std::max(maxCount, sz);
    }
    if (minCount == std::numeric_limits<int>::max()) minCount = 0;

    emit statusUpdated(m_seenChannels.size(), m_pointsPerChannel, minCount, maxCount);
    (void)tryEmitBuffer(f.rxTimeNs);
}

bool DataProcessWorker::tryEmitBuffer(qint64 timeNs)
{
    if (m_lastEmitNs != 0 && (timeNs - m_lastEmitNs) < m_emitIntervalNs)
        return false;
    m_lastEmitNs = timeNs;

    QVector<int> chIds = m_seenChannels.values();
    std::sort(chIds.begin(), chIds.end());

    for (int ch : chIds) {
        if (m_ampByCh[ch].size() < m_pointsPerChannel) return false;
        if (m_phaByCh[ch].size() < m_pointsPerChannel) return false;
    }

    ScanBuffer buf;
    buf.channelIds = chIds;
    buf.channelCount = chIds.size();
    buf.scanCount = m_pointsPerChannel;
    buf.amp.resize(buf.channelCount * buf.scanCount);
    buf.pha.resize(buf.channelCount * buf.scanCount);
    buf.bufferSeq = m_bufferSeq++;
    buf.endTimeNs = timeNs;
    buf.startTimeNs = 0;

    for (int row = 0; row < buf.channelCount; ++row) {
        const int ch = chIds[row];
        const auto& a = m_ampByCh[ch];
        const auto& p = m_phaByCh[ch];

        const int N = buf.scanCount;
        const int start = a.size() - N;

        // 1) 先算均值（去 DC）
        double meanA = 0.0, meanP = 0.0;
        for (int i = 0; i < N; ++i) {
            meanA += a[start + i];
            meanP += p[start + i];
        }
        meanA /= double(N);
        meanP /= double(N);

        // 2) 写入 buf：减均值（条纹会明显少）
        for (int i = 0; i < N; ++i) {
            buf.amp[row * N + i] = a[start + i] - meanA;
            buf.pha[row * N + i] = p[start + i] - meanP;
        }
    }


    emit bufferReady(buf);

    // ✅非 ring：发完就清空，下一块重新攒
    if (!m_ringEnabled) {
        reset();
        return true;
    }

    // ✅ring：发完也继续滚动；额外保证不超 maxFrames
    trimChannelBuffersIfNeeded();
    return true;
}
