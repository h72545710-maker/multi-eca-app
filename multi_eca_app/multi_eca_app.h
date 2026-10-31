#pragma once

#include <QtWidgets/QMainWindow>
#include <QImage>
#include <QPixmap>
#include <QTimer>
#include <QThread>
#include <QString>
#include <QFile>

#include "ui_multi_eca_app.h"
#include "SerialWorker.h"
#include "SaveWorker.h"
#include "DataProcessWorker.h"
#include "ImagingWorker.h"
#include "ScanBuffer.h"
#include "ImagingResult.h"
#include "ChartsController.h"

class multi_eca_app : public QMainWindow
{
    Q_OBJECT

public:
    explicit multi_eca_app(QWidget* parent = nullptr);
    ~multi_eca_app();

signals:
    // UI线程 → 成像线程（方案A）
    void imagingRequest(const ScanBuffer& buf);

private slots:
    void onImagingResult(const ImagingResult& r);

protected:
    void resizeEvent(QResizeEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    // ========== UI ==========
    Ui::multi_eca_appClass ui;

    // ========== Workers ==========
    SerialWorker* serialWorker = nullptr;
    DataProcessWorker* dataWorker = nullptr;
    ImagingWorker* imagingWorker = nullptr;

    QThread* m_imgThread = nullptr;
    QThread* m_serialThread = nullptr;
    QThread* m_saveThread = nullptr;
    SaveWorker* saveWorker = nullptr;

    // ========== 成像调度（latest-only queue） ==========
    bool m_imgBusy = false;
    bool m_hasPending = false;
    ScanBuffer m_pendingBuf;

    // ========== 成像显示 ==========
    enum class ImageMode { Amp, Pha, Fused };
    ImageMode m_imgMode = ImageMode::Amp;

    QImage  m_lastImage;
    QPixmap m_lastPixmap;

    int m_scanPx = 0;   // 扫描方向像素
    int m_chanPx = 0;   // 通道方向像素

    int m_channelCount = 7;
    double m_dx_mm = 1.5;
    double m_chWidth_mm = 6.0;

    // ========== 实时曲线 ==========
    ChartsController* charts = nullptr;
    QTimer* chartTimer = nullptr;

private:
    void updateImagingView();
    static QImage image2DToGray(const Image2D& im, bool autoNormalize = true);

    enum class ColorMap { Gray, Jet, Hot, Turbo };
    ColorMap m_cmap = ColorMap::Turbo;   // 先默认 Turbo（更自然），你也可以改 Jet

    static QImage applyColorMap(const QImage& gray, ColorMap cmap);
    static QRgb jetColor(int v);   // v: 0~255
    static QRgb hotColor(int v);
    static QRgb turboColor(int v);

  
    bool m_hoverActive = false;    // 当前是否在 imagingView 内
    qint64 m_lastHoverMs = 0;      // hover 限频（可选）

private:
    QString currentSaveDir() const;
    QString makeBaseName(quint64 seq) const;
    bool ensureDirExists(const QString& dir) const;

   // void trySaveRaw(const ScanBuffer& buf);
    void trySaveImaging(const ImagingResult& r);


private:
    bool m_rawSaving = false;
    void startRawStreamSaving();
    void stopRawStreamSaving();
};
