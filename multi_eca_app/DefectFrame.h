// DefectFrame.h
#pragma once
#include <Qvector>
struct DefectFrame
{
    quint32 scanIndex;   // 完成一次 0~14 扫描后的编号

    int channelId;       // 当前通道
    float amplitude;     // 幅值（或解调后）
    float phase;         // 相位
};
