#pragma once
#include <QtGlobal>
#include <QVector>

/**
 * @brief 通道映射：把下位机 channelCase 映射到“逻辑通道 index”
 *
 * 用法示例：
 * - 纯透传：case -> case（0..14）
 * - 7通道模式：把 0..14 映射到 0..6（你可后续填规则）
 */
struct ChannelMap final
{
    // map[case] = logicalId；如果 = -1 表示丢弃该 case
    QVector<int> map;

    static ChannelMap passthrough(int caseCount = 15)
    {
        ChannelMap m;
        m.map.resize(caseCount);
        for (int i = 0; i < caseCount; ++i) m.map[i] = i;
        return m;
    }

    inline int toLogical(int channelCase) const
    {
        if (channelCase < 0 || channelCase >= map.size()) return -1;
        return map[channelCase];
    }
};
