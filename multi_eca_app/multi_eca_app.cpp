#include "multi_eca_app.h"

#include <QDebug>
#include <QSerialPortInfo>
#include <QMetaType>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>
#include <QDateTime>
#include <QFileDialog>
#include <QTextDocument>
#include <QScrollBar>
#include <QDir>
#include <QFileInfo>
#include <QDataStream>
#include <QStandardPaths>

#include "Image2D.h"

// ======================================================
// 构造函数
// ======================================================
multi_eca_app::multi_eca_app(QWidget* parent)
    : QMainWindow(parent)
{
    // ======================================================
    // 0) UI 初始化
    // ======================================================
    ui.setupUi(this);

   

    // ImagingView：初值 + 事件过滤
    Q_ASSERT(ui.imagingView);
    ui.imagingView->installEventFilter(this);
    ui.imagingView->setShowGrid(ui.chkGrid->isChecked());
    ui.imagingView->setInvert(ui.chkInvert->isChecked());
    ui.imagingView->setSmooth(ui.chkSmooth->isChecked());

    // 数据页占位显示
    ui.labelBufValue->setText("0");
    ui.labelFrameValue->setText("0");

    // ======================================================
    // 1) 元类型注册（跨线程信号）
    // ======================================================
    qRegisterMetaType<RawFrame>("RawFrame");
    qRegisterMetaType<ScanBuffer>("ScanBuffer");
    qRegisterMetaType<ImagingResult>("ImagingResult");

    // ======================================================
    // 2) 创建 Workers（必须先 new，再 connect）
    // ======================================================
    m_serialThread = new QThread(this);
    serialWorker = new SerialWorker();
    serialWorker->moveToThread(m_serialThread);
    connect(m_serialThread, &QThread::finished, serialWorker, &QObject::deleteLater);
    m_serialThread->start();

    m_saveThread = new QThread(this);
    saveWorker = new SaveWorker();
    saveWorker->moveToThread(m_saveThread);
    connect(m_saveThread, &QThread::finished, saveWorker, &QObject::deleteLater);
    connect(saveWorker, &SaveWorker::failed, this, [this](const QString& msg) {
        ui.labelAcqHint->setText(msg);
        ui.chkSaveRaw->setChecked(false);
        ui.chkSaveImaging->setChecked(false);
    }, Qt::QueuedConnection);
    connect(saveWorker, &SaveWorker::rawClosed, this, [this](quint64 chunks, quint64 bytes) {
        ui.labelAcqHint->setText(QString("Raw saved: chunks=%1, payload=%2 bytes").arg(chunks).arg(bytes));
    }, Qt::QueuedConnection);
    m_saveThread->start();
    connect(serialWorker, &SerialWorker::portOpened, this, [this](const QString&, int) {
        ui.labelSerialStatus->setText(QStringLiteral("已连接"));
        ui.labelConnValue->setText("Connected");
    }, Qt::QueuedConnection);
    connect(serialWorker, &SerialWorker::portClosed, this, [this]() {
        ui.labelSerialStatus->setText(QStringLiteral("未连接"));
        ui.labelConnValue->setText("Disconnected");
    }, Qt::QueuedConnection);
    connect(serialWorker, &SerialWorker::portError, this, [this](const QString& msg) {
        ui.labelSerialStatus->setText(msg);
    }, Qt::QueuedConnection);
    dataWorker = new DataProcessWorker(this);

    // ======================================================
    // 3) 串口 UI 初始化 + 按钮
    // ======================================================
    ui.comboPort->clear();
    for (const auto& info : QSerialPortInfo::availablePorts())
        ui.comboPort->addItem(info.portName());
    if (ui.comboPort->count() > 0) ui.comboPort->setCurrentIndex(0);

    if (ui.comboBaud->count() == 0)
        ui.comboBaud->addItems({ "115200","230400","460800","921600","1500000","2000000" });
    ui.comboBaud->setCurrentText("115200");

    ui.labelSerialStatus->setText("未连接");
    ui.labelConnValue->setText("Disconnected");

    connect(ui.btnOpenSerial, &QPushButton::clicked, this, [this]() {
        const QString port = ui.comboPort->currentText().trimmed();
        const int baud = ui.comboBaud->currentText().toInt();
        if (port.isEmpty() || baud <= 0) {
            ui.labelSerialStatus->setText("参数错误");
            ui.labelConnValue->setText("Disconnected");
            return;
        }
        ui.labelSerialStatus->setText(QStringLiteral("连接中"));
        QMetaObject::invokeMethod(serialWorker, [this, port, baud]() {
            serialWorker->openPort(port, baud);
        }, Qt::QueuedConnection);
        });

    connect(ui.btnCloseSerial, &QPushButton::clicked, this, [this]() {
        QMetaObject::invokeMethod(serialWorker, "closePort", Qt::QueuedConnection);
        ui.labelSerialStatus->setText("未连接");
        ui.labelConnValue->setText("Disconnected");
        });

    // ======================================================
    // 4) 采集参数（Acq）初始化 + 应用
    // ======================================================
    m_channelCount = 15;
    m_dx_mm = 1.5;
    m_chWidth_mm = 3.0;

    dataWorker->setUseSevenChannels(false);

    ui.spinChannelCount->setValue(m_channelCount);
    ui.spinPointsPerChannel->setValue(400);
    ui.spinAdcBits->setValue(16);
    ui.dsbVref->setValue(5.0);

    dataWorker->setPointsPerChannel(ui.spinPointsPerChannel->value());
    dataWorker->setAdcConfig(ui.dsbVref->value(), ui.spinAdcBits->value());

    connect(ui.btnApplyAcq, &QPushButton::clicked, this, [this]() {
        const int pts = ui.spinPointsPerChannel->value();
        const int bits = ui.spinAdcBits->value();
        const double vref = ui.dsbVref->value();

        dataWorker->setPointsPerChannel(pts);
        dataWorker->setAdcConfig(vref, bits);

        ui.labelAcqHint->setText(
            QString("已应用：pts=%1, ADC=%2bit, Vref=%3V")
            .arg(pts).arg(bits).arg(vref, 0, 'f', 3)
        );
        });

    // ========== 第二批：Buffer 策略（ring/非ring + max frames）==========
    ui.spinMaxFrames->setValue(2000);              // 你 UI 默认多少都行
    ui.chkRingBuffer->setChecked(true);          // 默认用 ring 更适合实时

    dataWorker->setMaxFrames(ui.spinMaxFrames->value());
    dataWorker->setRingBufferEnabled(ui.chkRingBuffer->isChecked());

    connect(ui.spinMaxFrames, QOverload<int>::of(&QSpinBox::valueChanged),
        this, [this](int v) {
            dataWorker->setMaxFrames(v);
        });

    connect(ui.chkRingBuffer, &QCheckBox::toggled,
        this, [this](bool on) {
            dataWorker->setRingBufferEnabled(on);
        });

    // labelBufValue 显示真实状态（第二批标准）
    connect(dataWorker, &DataProcessWorker::statusUpdated,
        this, [this](int chCnt, int pts, int minC, int maxC) {
            ui.labelBufValue->setText(
                QString("ch=%1  pts=%2  min=%3  max=%4")
                .arg(chCnt).arg(pts).arg(minC).arg(maxC)
            );
        });


    // ======================================================
    // 5) 成像线程 / ImagingWorker（必须先创建，再绑定按钮逻辑）
    // ======================================================
    ImagingSettings s;
    s.interpChannels = 256;
    s.interpMethod = "bicubic";
    s.channelSharp = true;
    s.sharpAlphaAmp = 10.0;
    s.sharpAlphaPha = 10.0;
    s.edgeGateFrac = 0.1;
    s.edgePower = 1.0;
    s.dx_mm = m_dx_mm;
    s.chWidth_mm = m_chWidth_mm;
    s.enableWavelet = false;
    s.waveletLevels = 4;

    m_imgThread = new QThread(this);
    imagingWorker = new ImagingWorker(nullptr);
    imagingWorker->setSettings(s);
    imagingWorker->moveToThread(m_imgThread);

    connect(m_imgThread, &QThread::finished, imagingWorker, &QObject::deleteLater);
    m_imgThread->start();

    // UI → 成像线程
    connect(this, &multi_eca_app::imagingRequest,
        imagingWorker, &ImagingWorker::onBuffer,
        Qt::QueuedConnection);

    // 成像结果 → UI
    connect(imagingWorker, &ImagingWorker::resultReady,
        this, &multi_eca_app::onImagingResult,
        Qt::QueuedConnection);

    connect(imagingWorker, &ImagingWorker::imagingError,
        this, [this](const QString& msg) {
            qWarning() << "[ImagingError]" << msg;
            m_imgBusy = false;
            m_hasPending = false;
        }, Qt::QueuedConnection);

    // ======================================================
    // 6) 成像参数应用（btnApplyImaging）
    // ======================================================
    auto applyImagingFromUi = [this]() {
        if (!imagingWorker) return;

        ImagingSettings s2;
        s2.interpChannels = ui.spinInterpChannels->value();
        s2.interpMethod = ui.comboInterpMethod->currentText().toLower();

        s2.channelSharp = ui.chkChannelSharp->isChecked();
        s2.sharpAlphaAmp = ui.dsbSharpAlphaAmp->value();
        s2.sharpAlphaPha = ui.dsbSharpAlphaPha->value();

        s2.edgeGateFrac = ui.dsbEdgeGateFrac->value();
        s2.edgePower = ui.dsbEdgePower->value();

        s2.enableWavelet = ui.chkEnableWavelet->isChecked();
        s2.waveletLevels = ui.spinWaveletLevels->value();

        s2.dx_mm = m_dx_mm;
        s2.chWidth_mm = m_chWidth_mm;

        QMetaObject::invokeMethod(imagingWorker, [this, s2]() {
            imagingWorker->setSettings(s2);
            }, Qt::QueuedConnection);

        ui.labelImagingHint->setText("已应用成像参数（对后续帧生效）");
        };

    connect(ui.btnApplyImaging, &QPushButton::clicked, this, [applyImagingFromUi]() {
        applyImagingFromUi();
        });

    // ======================================================
    // 7) 串口 → 数据处理 → 成像调度
    // ======================================================
    connect(serialWorker, &SerialWorker::rawFrameReady,
        dataWorker, &DataProcessWorker::onRawFrame, Qt::QueuedConnection);

    connect(dataWorker, &DataProcessWorker::bufferReady,
        this, [this](const ScanBuffer& buf) {

           // trySaveRaw(buf);


            if (!m_imgBusy) {
                m_imgBusy = true;
                emit imagingRequest(buf);
            }
            else {
                m_pendingBuf = buf;
                m_hasPending = true;
            }
        });

    // ======================================================
    // 8) 成像模式 + 伪彩
    // ======================================================
    ui.comboImageMode->clear();
    ui.comboImageMode->addItems({ "Amp", "Pha", "Fused" });
    ui.comboImageMode->setCurrentIndex(0);

    connect(ui.comboImageMode, &QComboBox::currentIndexChanged,
        this, [this](int idx) {
            if (idx == 0) m_imgMode = ImageMode::Amp;
            else if (idx == 1) m_imgMode = ImageMode::Pha;
            else m_imgMode = ImageMode::Fused;
            updateImagingView();
        });

    connect(ui.comboColormap, &QComboBox::currentIndexChanged,
        this, [this](int) {
            const QString s = ui.comboColormap->currentText().toLower();
            if (s.contains("gray")) m_cmap = ColorMap::Gray;
            else if (s.contains("jet")) m_cmap = ColorMap::Jet;
            else if (s.contains("hot")) m_cmap = ColorMap::Hot;
            else m_cmap = ColorMap::Turbo;
            updateImagingView();
        });

    connect(ui.chkAutoRange, &QCheckBox::toggled,
        this, [this](bool on) {
            ui.dsbRangeMin->setEnabled(!on);
            ui.dsbRangeMax->setEnabled(!on);
        });

    // Display Apply/Reset
    connect(ui.btnApplyDisplay, &QPushButton::clicked, this, [this]() {
        ui.imagingView->setShowGrid(ui.chkGrid->isChecked());
        ui.imagingView->setInvert(ui.chkInvert->isChecked());
        ui.imagingView->setSmooth(ui.chkSmooth->isChecked());
        updateImagingView();
        });

    connect(ui.btnResetDisplay, &QPushButton::clicked, this, [this]() {
        ui.comboImageMode->setCurrentIndex(0);
        ui.comboColormap->setCurrentText("Gray");
        ui.chkAutoRange->setChecked(true);

        ui.chkSmooth->setChecked(true);
        ui.chkInvert->setChecked(false);
        ui.chkGrid->setChecked(true);

        ui.chkRealtime->setChecked(true);
        ui.spinFpsLimit->setValue(20);

        ui.btnApplyDisplay->click();
        });

    // ======================================================
    // 9) ImagingView 鼠标悬停读数
    // ======================================================
    connect(ui.imagingView, &ImagingView::hoverInfo,
        this, [this](double x, double y, int gray, double v) {
            if (!m_hoverActive) return;

            const qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_lastHoverMs < 50) return;
            m_lastHoverMs = now;

            statusBar()->showMessage(
                QString("x=%1 mm   y=%2 mm   gray=%3   val=%4")
                .arg(x, 0, 'f', 1)
                .arg(y, 0, 'f', 1)
                .arg(gray)
                .arg(v, 0, 'f', 3),
                0
            );
        });

    // ======================================================
    // 10) 数据页：清空 / 目录 / Hex
    // ======================================================

    // Hex 显示区初始化
    ui.pteHex->setReadOnly(true);
    ui.pteHex->setMaximumBlockCount(300);   // 最多保留 300 行

    connect(ui.btnClearBuffer, &QPushButton::clicked, this, [this]() {
        QMetaObject::invokeMethod(dataWorker, "clearBuffer", Qt::QueuedConnection);
        ui.labelBufValue->setText("0");
        ui.pteHex->clear();                 // 顺手把 hex 也清一下更合理
        });

    connect(ui.btnBrowseDir, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, "选择保存目录", ui.lineEdit->text()
        );
        if (!dir.isEmpty()) ui.lineEdit->setText(dir);
        });

    // 原始帧 Hex 滚动显示
    connect(serialWorker, &SerialWorker::rawBytesForUi,
        this, [this](const QByteArray& raw) {

            // ===== 1) 原始字节流保存（一个大文件 append）=====
            if (m_rawSaving) saveWorker->enqueueRaw(raw);

            // ===== 2) 你的 Hex 滚动显示（照旧）=====
            static quint64 seq = 0;
            const QByteArray cut = raw.left(512);
            const QString ts = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
            const QString line = QString("[%1 #%2] %3")
                .arg(ts)
                .arg(++seq)
                .arg(QString(cut.toHex(' ').toUpper()));
            ui.pteHex->appendPlainText(line);
            auto* bar = ui.pteHex->verticalScrollBar();
            bar->setValue(bar->maximum());
            ui.labelFrameValue->setText(QString("%1 bytes").arg(raw.size()));
        });
    // ======================================================
    // 11) Charts（你原样保留）
    // ======================================================
    charts = new ChartsController(this);
    charts->bindUi(
        ui.widgetAmp1, ui.comboAmp1,
        ui.widgetPha1, ui.comboPha1,
        ui.widgetAmp2, ui.comboAmp2,
        ui.widgetPha2, ui.comboPha2
    );
    charts->setChannelCount(m_channelCount);
    charts->setXPoints(50);

    connect(dataWorker, &DataProcessWorker::sampleReady,
        this, [this](const SamplePoint& sp) {
            charts->pushSample(
                sp.channelId,
                static_cast<double>(sp.ampCode),
                static_cast<double>(sp.phaCode)
            );
        });

    chartTimer = new QTimer(this);
    connect(chartTimer, &QTimer::timeout, this, [this]() {
        charts->refresh();
        });
    chartTimer->start(33);

    // realtime/fps
    auto updateChartTimer = [this]() {
        if (!chartTimer) return;
        if (!ui.chkRealtime->isChecked()) { chartTimer->stop(); return; }
        const int fps = std::max(1, ui.spinFpsLimit->value());
        chartTimer->start(std::max(1, 1000 / fps));
        };
    connect(ui.chkRealtime, &QCheckBox::toggled, this, [updateChartTimer](bool) { updateChartTimer(); });
    connect(ui.spinFpsLimit, QOverload<int>::of(&QSpinBox::valueChanged),
        this, [updateChartTimer](int) { updateChartTimer(); });

    ui.chkRealtime->setChecked(true);
    updateChartTimer();

    connect(ui.chkSaveRaw, &QCheckBox::toggled, this, [this](bool on) {
        if (on) startRawStreamSaving();
        else    stopRawStreamSaving();
        });

    connect(ui.btnCloseSerial, &QPushButton::clicked, this, [this]() {
        if (m_rawSaving) {
            ui.chkSaveRaw->setChecked(false); // 会触发 stopRawStreamSaving()
        }
        });

    // ======================================================
    // 12) 启动时是否自动打开串口（建议先注释）
    // ======================================================
    // serialWorker->openPort("COM6", 115200);

}

// ======================================================
// 析构
// ======================================================
multi_eca_app::~multi_eca_app()
{
    if (m_serialThread && m_serialThread->isRunning()) {
        QMetaObject::invokeMethod(serialWorker, "closePort", Qt::BlockingQueuedConnection);
        m_serialThread->quit();
        m_serialThread->wait();
    }
    stopRawStreamSaving();
    if (m_imgThread) { m_imgThread->quit(); m_imgThread->wait(); }
    if (m_saveThread && m_saveThread->isRunning()) {
        // Barrier: finish already queued raw/image writes before quitting.
        QMetaObject::invokeMethod(saveWorker, [this]() { saveWorker->closeRaw(); }, Qt::BlockingQueuedConnection);
        m_saveThread->quit();
        m_saveThread->wait();
    }
}

// ======================================================
// 成像结果
// ======================================================
void multi_eca_app::onImagingResult(const ImagingResult& r)
{
    const Image2D* src = nullptr;
    if (m_imgMode == ImageMode::Amp) src = &r.ampInterp;
    else if (m_imgMode == ImageMode::Pha) src = &r.phaInterp;
    else src = (r.hasFused && r.fusedNorm.isValid()) ? &r.fusedNorm : &r.ampInterp;

    if (!src || !src->isValid()) {
        m_imgBusy = false;
        return;
    }

    m_scanPx = src->width;
    m_chanPx = src->height;

    QImage gray = image2DToGray(*src, true);
    m_lastImage = gray; // ★保留灰度用于数值/悬停

    QImage show = applyColorMap(gray, m_cmap);  // ★伪彩色显示
    m_lastPixmap = QPixmap::fromImage(show);

    updateImagingView();

    if (m_hasPending) {
        const ScanBuffer next = m_pendingBuf;
        m_hasPending = false;
        m_imgBusy = true;
        emit imagingRequest(next);
        return;
    }
   
    m_imgBusy = false;


    trySaveImaging(r);

}

// ======================================================
// ImagingView 显示
// ======================================================
void multi_eca_app::updateImagingView()
{
    if (!ui.imagingView) return;
    if (m_lastPixmap.isNull()) return;

    const double Lx_mm =
        (m_scanPx > 1) ? (m_scanPx - 1) * m_dx_mm : 1.0;
    const double Ly_mm =
        (m_channelCount > 1) ? (m_channelCount - 1) * m_chWidth_mm : 1.0;

    ui.imagingView->setGrayForProbe(m_lastImage);
    ui.imagingView->setImage(m_lastPixmap, Lx_mm, Ly_mm);
  
}

// ======================================================
// resize
// ======================================================
void multi_eca_app::resizeEvent(QResizeEvent* e)
{
    QMainWindow::resizeEvent(e);
    updateImagingView();
}

// ======================================================
// Image2D → QImage
// ======================================================
QImage multi_eca_app::image2DToGray(const Image2D& im, bool autoNormalize)
{
    if (!im.isValid() || im.width <= 0 || im.height <= 0)
        return QImage();

    const int w = im.width;
    const int h = im.height;

    double mn = 0.0, mx = 1.0;
    if (autoNormalize) {
        mn = im.data[0];
        mx = im.data[0];
        for (double v : im.data) {
            mn = std::min(mn, v);
            mx = std::max(mx, v);
        }
        if (std::abs(mx - mn) < 1e-12) mx = mn + 1.0;
    }

    QImage img(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            double t = (im.data[y * w + x] - mn) / (mx - mn);
            t = std::clamp(t, 0.0, 1.0);
            line[x] = static_cast<uchar>(std::lround(t * 255.0));
        }
    }
    return img;
}

static inline int clampi(int x, int a, int b) { return x < a ? a : (x > b ? b : x); }

QRgb multi_eca_app::jetColor(int v)
{
    // Jet: 蓝->青->黄->红
    const double x = v / 255.0;
    auto f = [](double t) { return std::clamp(t, 0.0, 1.0); };

    // 经典 jet 近似：r,g,b 分段线性
    double r = f(1.5 - std::abs(4.0 * x - 3.0));
    double g = f(1.5 - std::abs(4.0 * x - 2.0));
    double b = f(1.5 - std::abs(4.0 * x - 1.0));

    return qRgb(int(r * 255), int(g * 255), int(b * 255));
}

QRgb multi_eca_app::hotColor(int v)
{
    // Hot: 黑->红->黄->白
    const double x = v / 255.0;
    double r = std::min(1.0, 3.0 * x);
    double g = std::min(1.0, std::max(0.0, 3.0 * x - 1.0));
    double b = std::min(1.0, std::max(0.0, 3.0 * x - 2.0));
    return qRgb(int(r * 255), int(g * 255), int(b * 255));
}

QRgb multi_eca_app::turboColor(int v)
{
    // Turbo 近似（常用的平滑伪彩，比 Jet 更“自然”）
    // 用一个简单的多项式近似（够用且实现短）
    const double x = v / 255.0;
    auto f = [](double t) { return std::clamp(t, 0.0, 1.0); };

    // 经验近似：做一个平滑的“蓝->绿->黄->红”过渡
    // （这不是严格的 Google Turbo LUT，但视觉效果很接近，且代码短）
    double r = f(1.2 * x - 0.1) + f(2.0 * (x - 0.6));
    double g = f(1.8 * x) - f(2.2 * (x - 0.8));
    double b = f(1.0 - 1.6 * x) + f(1.5 * (0.35 - x));

    r = f(r);
    g = f(g);
    b = f(b);

    return qRgb(int(r * 255), int(g * 255), int(b * 255));
}

QImage multi_eca_app::applyColorMap(const QImage& gray, ColorMap cmap)
{
    if (gray.isNull()) return QImage();
    if (cmap == ColorMap::Gray) return gray.convertToFormat(QImage::Format_Grayscale8);

    QImage g8 = gray;
    if (g8.format() != QImage::Format_Grayscale8)
        g8 = gray.convertToFormat(QImage::Format_Grayscale8);

    QImage out(g8.size(), QImage::Format_RGB888);
    if (out.isNull()) return QImage();

    for (int y = 0; y < g8.height(); ++y) {
        const uchar* src = g8.constScanLine(y);
        uchar* dst = out.scanLine(y);

        for (int x = 0; x < g8.width(); ++x) {
            const int v = src[x]; // 0~255
            QRgb c;
            if (cmap == ColorMap::Jet) c = jetColor(v);
            else if (cmap == ColorMap::Hot) c = hotColor(v);
            else c = turboColor(v);

            // RGB888: 3 bytes
            dst[3 * x + 0] = qRed(c);
            dst[3 * x + 1] = qGreen(c);
            dst[3 * x + 2] = qBlue(c);
        }
    }
    return out;
}

bool multi_eca_app::eventFilter(QObject* obj, QEvent* ev)
{
    if (obj == ui.imagingView) {
        if (ev->type() == QEvent::Enter) {
            m_hoverActive = true;
            return false;
        }
        if (ev->type() == QEvent::Leave) {
            m_hoverActive = false;

            // 鼠标移出：清空 statusBar（或恢复固定文案）
            statusBar()->clearMessage();
            // 或者：statusBar()->showMessage("Ready");

            return false;
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

QString multi_eca_app::currentSaveDir() const
{
    QString dir = ui.lineEdit->text().trimmed();
    if (dir.isEmpty()) {
        // 默认：文档目录/multi_eca_app
        dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
            + "/multi_eca_app";
    }
    return QDir::fromNativeSeparators(dir);
}

bool multi_eca_app::ensureDirExists(const QString& dir) const
{
    QDir d(dir);
    if (d.exists()) return true;
    return d.mkpath(".");
}

QString multi_eca_app::makeBaseName(quint64 seq) const
{
    // 时间戳 + seq：保证唯一且好找
    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
    return QString("%1_seq%2").arg(ts).arg(seq);
}

//void multi_eca_app::trySaveRaw(const ScanBuffer& buf)
//{
//    if (!ui.chkSaveRaw->isChecked()) return;
//    if (!buf.isValid()) return;
//
//    const QString dir = currentSaveDir();
//    if (!ensureDirExists(dir)) {
//        qWarning() << "[SaveRaw] mkdir failed:" << dir;
//        return;
//    }
//
//    const QString base = makeBaseName(buf.bufferSeq);
//
//    // 目前 UI 只有 BIN，就按 BIN 存
//    const QString filePath = dir + "/" + base + "_raw.bin";
//
//    if (saveScanBufferBin(filePath, buf)) {
//        qDebug() << "[SaveRaw] OK:" << filePath;
//    }
//    else {
//        qWarning() << "[SaveRaw] Failed:" << filePath;
//    }
//}

void multi_eca_app::trySaveImaging(const ImagingResult& r)
{
    if (!ui.chkSaveImaging->isChecked()) return;

    const QString dir = currentSaveDir();
    if (!ensureDirExists(dir)) {
        ui.labelAcqHint->setText("保存失败：无法创建目录");
        ui.chkSaveRaw->setChecked(false);
        return;
    }

    const QString base = makeBaseName(r.jobSeq);

    saveWorker->enqueueImage(dir + "/" + base, m_lastPixmap.toImage(), r);
}

void multi_eca_app::startRawStreamSaving()
{
    if (m_rawSaving) return;
    const QString dir = currentSaveDir();
    if (!ensureDirExists(dir)) {
        ui.labelAcqHint->setText(QStringLiteral("保存失败：无法创建目录"));
        ui.chkSaveRaw->setChecked(false); return;
    }
    const QString path = dir + "/" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz") + "_rawstream.bin";
    const int baud = ui.comboBaud->currentText().toInt();
    const int points = ui.spinPointsPerChannel->value();
    const int channels = ui.spinChannelCount->value();
    bool ok = false;
    QMetaObject::invokeMethod(saveWorker, [this, path, baud, points, channels, &ok]() {
        ok = saveWorker->openRaw(path, baud, points, channels);
    }, Qt::BlockingQueuedConnection);
    m_rawSaving = ok;
    if (!ok) ui.chkSaveRaw->setChecked(false);
    else ui.labelAcqHint->setText(QStringLiteral("开始保存：") + path);
}

void multi_eca_app::stopRawStreamSaving()
{
    if (!m_rawSaving) return;
    m_rawSaving = false;
    QMetaObject::invokeMethod(saveWorker, [this]() { saveWorker->closeRaw(); }, Qt::BlockingQueuedConnection);
}
