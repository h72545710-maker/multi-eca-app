// ImagingBuffer.h
#pragma once
#include <QVector>

struct ImagingBuffer
{
    int channelCount;    // 15
    int scanCount;       // 扫描步数（行）

    // 每个通道沿扫描方向的数据
    QVector<QVector<float>> amp;   // [channel][scan]
    QVector<QVector<float>> pha;   // [channel][scan]
};
