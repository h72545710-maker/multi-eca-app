#pragma once
#include <QtGlobal>

/**
 * @brief 解包后“单个采样点”（已换算为 Volt）
 * 注意：当前阶段不做 dB/角度映射，amp/pha 都只表示电压。
 */
struct SamplePoint final
{
    int     channelId = 0;     // 建议先等于 channelCase（0..14）；后续可映射为 7ch/15ch逻辑通道
    double  ampVolt = 0.0;   // VMAG 电压（V）
    double  phaVolt = 0.0;   // VPHS 电压（V）
    quint16 ampCode = 0;
    quint16 phaCode = 0;

    quint64 seq = 0;     // 对应 RawFrame.rxSeq
    qint64  timeNs = 0;     // 对应 RawFrame.rxTimeNs
};
