#pragma once
#include <QObject>
#include <QPointer>

QT_FORWARD_DECLARE_CLASS(QWidget)
QT_FORWARD_DECLARE_CLASS(QComboBox)

QT_FORWARD_DECLARE_CLASS(QChart)
QT_FORWARD_DECLARE_CLASS(QChartView)
QT_FORWARD_DECLARE_CLASS(QLineSeries)
QT_FORWARD_DECLARE_CLASS(QValueAxis)

/**
 * @brief 管一张图：Chart + Series + Axis + 通道下拉框
 * - 只显示一条曲线
 * - 下拉框选择当前通道
 */
    class ChartPanel : public QObject
{
    Q_OBJECT
public:
    enum class Kind { Amp, Pha };

    explicit ChartPanel(QObject* parent = nullptr);

    // 把图挂到 UI 容器 widget 上，把通道下拉框绑定上
    void bind(QWidget* container, QComboBox* combo,
        Kind kind, const QString& title);

    // 设置通道数（用于填充 CH0..CH6）
    void setChannelCount(int n);

    // 选择当前通道（0..n-1）
    void setCurrentChannel(int ch);
    int  currentChannel() const { return m_currentCh; }

    // 设置 X 轴显示点数（例如你现在 50）
    void setXRange(int points);

    // 刷新曲线数据（x=0..N-1）
    void updateSeries(const QVector<double>& y);

signals:
    void channelChanged(int ch);

private slots:
    void onComboChanged(int index);

private:
    void ensureChartBuilt();
    void rebuildComboItems();

private:
    QPointer<QWidget>   m_container;
    QPointer<QComboBox> m_combo;

    Kind m_kind = Kind::Amp;
    QString m_title;

    int m_channelCount = 7;
    int m_currentCh = 0;
    int m_xPoints = 50;

    // Qt Charts objects
    QChart* m_chart = nullptr;
    QChartView* m_view = nullptr;
    QLineSeries* m_series = nullptr;
    QValueAxis* m_axisX = nullptr;
    QValueAxis* m_axisY = nullptr;
};
