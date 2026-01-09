#include "cameramanager.h"
#include <QDebug>
#include <QThread>

CameraManager::CameraManager(QObject *parent) : QObject(parent) {}

CameraManager::~CameraManager() { release(); }

std::string CameraManager::getCsiGStreamerPipeline() {
  // TX2 板載相機 (OV5693) 的標準呼叫字串
  return "nvarguscamerasrc ! video/x-raw(memory:NVMM), width=(int)1280, "
         "height=(int)720, format=(string)NV12, framerate=(fraction)30/1 ! "
         "nvvidconv flip-method=0 ! video/x-raw, width=(int)640, "
         "height=(int)480, format=(string)BGRx ! "
         "videoconvert ! video/x-raw, format=(string)BGR ! appsink";
}

std::string CameraManager::getUsbGStreamerPipeline(int deviceIndex) {
  // USB 相機 (V4L2) 的 GStreamer 呼叫字串
  return "v4l2src device=/dev/video" + std::to_string(deviceIndex) +
         " ! video/x-raw, width=640, height=480 ! "
         "videoconvert ! video/x-raw, format=BGR ! appsink";
}

bool CameraManager::openCamera() {
  // 1. 偵測是否為 Linux/ARM 架構 (TX2)
#ifdef __linux__
  qDebug() << "CameraManager: 偵測到 Linux 環境，嘗試穩定模式";

  // 定義嘗試的順序：Index 1 (通常是外接 USB), Index 0 (通常是板載)
  int retryIndices[] = {1, 0};

  for (int idx : retryIndices) {
    // 嘗試多種 GStreamer 組合
    QStringList pipelines;
    // 組合 1: 自動協商 (最通用)
    pipelines << QString(
                     "v4l2src device=/dev/video%1 ! decodebin ! videoconvert ! "
                     "video/x-raw, format=BGR ! appsink drop=1")
                     .arg(idx);
    // 組合 2: 強制 YUY2 (許多 USB 相機的原始格式)
    pipelines << QString("v4l2src device=/dev/video%1 ! video/x-raw, "
                         "width=640, height=480 ! videoconvert ! video/x-raw, "
                         "format=BGR ! appsink drop=1")
                     .arg(idx);

    for (const QString &gst_pipe : pipelines) {
      qDebug() << "CameraManager: 嘗試透過 GStreamer 開啟 USB 相機 (Index"
               << idx << ")...";

      if (cap.open(gst_pipe.toStdString(), cv::CAP_GSTREAMER)) {
        if (cap.isOpened()) {
          // 測試是否真的能讀到影格 (避免 select timeout)
          cv::Mat test_frame;
          cap >> test_frame;
          if (!test_frame.empty()) {
            qDebug()
                << "CameraManager: 成功透過 GStreamer 開啟並讀取影格 (Index"
                << idx << ")";
            return true;
          }
          cap.release();
        }
      }
    }

    // 如果 GStreamer 都失敗，嘗試直接使用 V4L2 驅動
    qDebug() << "CameraManager: GStreamer 失敗，嘗試直接 V4L2 模式 (Index"
             << idx << ")...";
    if (cap.open(idx, cv::CAP_V4L2)) {
      // 在 V4L2 模式下，設定 MJPG 往往比較穩定
      cap.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
      cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
      cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
      cap.set(cv::CAP_PROP_FPS, 30);

      if (cap.isOpened()) {
        cv::Mat test_frame;
        // 稍微等待一下硬體反應
        QThread::msleep(100);
        cap >> test_frame;
        if (!test_frame.empty()) {
          qDebug() << "CameraManager: 成功開啟 USB 相機 (Index" << idx
                   << ") [V4L2]";
          return true;
        }
        qDebug() << "CameraManager: V4L2 開啟成功但讀取影格超時 (Index" << idx
                 << ")";
        cap.release();
      }
    }
  }

  // 2. 如果 USB 都失敗且是 TX2，嘗試板載 CSI 相機
#ifdef __aarch64__
  qDebug() << "CameraManager: USB 開啟失敗，嘗試 TX2 CSI GStreamer 管道...";
  if (cap.open(getCsiGStreamerPipeline(), cv::CAP_GSTREAMER)) {
    if (cap.isOpened()) {
      cv::Mat test_frame;
      cap >> test_frame;
      if (!test_frame.empty()) {
        qDebug() << "CameraManager: 成功開啟 TX2 CSI 相機";
        return true;
      }
      cap.release();
    }
  }
#endif
#endif

  // 3. 在一般電腦 (Windows/Mac) 或 Linux 失敗時，嘗試最通用接口
  qDebug() << "CameraManager: 嘗試通用接口開啟相機 (Index 0)...";
  if (cap.open(0, cv::CAP_ANY)) {
    if (cap.isOpened()) {
      return true;
    }
  }

  return false;
}

void CameraManager::process() {
  m_running = true;
  qDebug() << "CameraManager: 背景執行緒已啟動";

  while (m_running) {
    if (!cap.isOpened()) {
      emit errorOccurred("攝影機未開啟");
      break;
    }

    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) {
      QThread::msleep(10);
      continue;
    }

    // 格式轉換 (移動到迴圈內)
    cv::Mat temp;
    cv::cvtColor(frame, temp, cv::COLOR_BGR2RGB);

    QImage qimg((const uchar *)temp.data, temp.cols, temp.rows, temp.step,
                QImage::Format_RGB888);

    if (qimg.isNull()) {
      qDebug() << "CameraManager: 警告！生成的 QImage 是空的";
    } else {
      // 每 30 幀印一次訊息，避免洗頻
      // static int frameCount = 0;
      // if (++frameCount % 30 == 0) {
      //   qDebug() << "CameraManager: 成功擷取影像 - 尺寸:" << qimg.width() <<
      //   "x"
      //            << qimg.height();
      // }
    }

    // 發送影像訊號（使用 copy() 確保跨執行緒安全）
    emit frameReady(qimg.copy());

    // 調整為 30ms 左右，這對一般攝影機比較穩定
    QThread::msleep(30);
  }
  qDebug() << "CameraManager: 背景執行緒已停止";
}

void CameraManager::stop() { m_running = false; }

void CameraManager::release() {
  m_running = false;
  if (cap.isOpened()) {
    cap.release();
  }
}
