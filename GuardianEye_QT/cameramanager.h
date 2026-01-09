#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QImage>
#include <QObject>
#include <opencv2/opencv.hpp>
#include <string>

class CameraManager : public QObject {
  Q_OBJECT
public:
  explicit CameraManager(QObject *parent = nullptr);
  ~CameraManager();

  bool openCamera();
  void release();

public slots:
  void process(); // 執行緒的主循環函式
  void stop();    // 停止執行緒

signals:
  void frameReady(QImage img);     // 當新影像準備好時發送訊號
  void errorOccurred(QString msg); // 當發生錯誤時發送

private:
  cv::VideoCapture cap;
  bool m_running = false;
  std::string getCsiGStreamerPipeline();
  std::string getUsbGStreamerPipeline(int deviceIndex = 1);
};

#endif // CAMERAMANAGER_H
