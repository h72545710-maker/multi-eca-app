#pragma once
#include <QtGlobal>

/**
 * @brief 串口组帧后的原始数据帧（尚未换算电压）
 * 协议：0xFF | channelCase | amp_hi amp_lo | pha_hi pha_lo | 0x55
 */
struct RawFrame final
{
    quint8  channelCase = 0;   // 0..14（下位机 current_case）
    quint16 ampCode = 0;   // AD7606 ch0 原始 16-bit（无符号容器保存两补码）
    quint16 phaCode = 0;   // AD7606 ch7 原始 16-bit（无符号容器保存两补码）

    quint64 rxSeq = 0;      // 上位机帧序号（SerialWorker 生成）
    qint64  rxTimeNs = 0;      // 接收时间戳（可选）
};
