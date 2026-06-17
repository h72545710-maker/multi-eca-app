#pragma once
#include <QtGlobal>
#include <QVector>
#include <QMetaType>

struct ScanBuffer final
{
    QVector<int> channelIds;
    int channelCount = 0;
    int scanCount = 0;

    QVector<double> amp;
    QVector<double> pha;

    quint64 bufferSeq = 0;
    qint64  startTimeNs = 0;
    qint64  endTimeNs = 0;

    // ---------- quick stats ----------
    int frameCount()  const { return scanCount; }
    int sampleCount() const { return scanCount; }
    int totalPoints() const { return channelCount * scanCount; }
    bool empty() const { return channelCount <= 0 || scanCount <= 0; }
    qint64 durationNs() const { return endTimeNs - startTimeNs; }

    void allocate(int ch, int scan)
    {
        channelCount = ch;
        scanCount = scan;
        channelIds.resize(ch);
        amp.resize(ch * scan);
        pha.resize(ch * scan);
    }

    bool isValid() const
    {
        return channelCount > 0
            && scanCount > 0
            && channelIds.size() == channelCount
            && amp.size() == channelCount * scanCount
            && pha.size() == channelCount * scanCount;
    }

    double& ampAt(int chIndex, int i) { return amp[chIndex * scanCount + i]; }
    double& phaAt(int chIndex, int i) { return pha[chIndex * scanCount + i]; }
    double  ampAt(int chIndex, int i) const { return amp[chIndex * scanCount + i]; }
    double  phaAt(int chIndex, int i) const { return pha[chIndex * scanCount + i]; }

    const double& ampAtRef(int chIndex, int i) const { return amp[chIndex * scanCount + i]; }
    const double& phaAtRef(int chIndex, int i) const { return pha[chIndex * scanCount + i]; }
};

Q_DECLARE_METATYPE(ScanBuffer)
