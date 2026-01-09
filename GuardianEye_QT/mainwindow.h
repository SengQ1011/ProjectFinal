#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QShortcut>
#include <QStringList>
#include <QStringListModel>
#include <QThread>
#include <QTimer>

class PythonAiManager;
class SecurityController;
class EnvironmentalController;
class BlackboxInterface;
class Mcp3008Interface;
class EmergencyController;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();

private slots:
  void updateFrame(QImage img);
  void handlePasswordInput();
  void handleShortcut(int keyId);
  void pollSensors();                   // 定期輪詢感測器
  void simulateAiTrigger(QString type); // 新增：模擬 AI 觸發接口
  void sendDiscordNotification(QString type,
                               QString priority); // 新增：Discord 推播接口
  void sendDiscordCode(QString code); // 新增：發送驗證碼到 Discord
  void updateAlarmJsonWithCountdown(int seconds,
                                    QString formatted); // 新增：同步倒數到 JSON

protected:
  void keyPressEvent(QKeyEvent *event) override; // 處理連按快捷鍵

private:
  Ui::MainWindow *ui;

  // 核心硬體介面
  BlackboxInterface *blackbox;
  Mcp3008Interface *adc;

  // 業務邏輯控制器
  PythonAiManager *camera;
  SecurityController *security;
  EnvironmentalController *env;
  EmergencyController *emergency;

  // UI 模型
  QStringListModel *eventModel;
  QStringList m_logHistory;

  // 執行緒管理
  QThread *cameraThread;
  QThread *logicThread; // 邏輯共用執行緒
  bool m_isMuted = false;
  bool m_isAutoLight = true;                // 是否為自動燈光模式
  bool m_manualYellowLed = false;           // 手動模式下的黃燈狀態
  QMap<QString, QDateTime> m_lastAlertTime; // 新增：紀錄上次各類警報的時間
  QElapsedTimer m_f12Timer;                 // 用於偵測 F12 連按

  void setupShortcuts();
};

#endif // MAINWINDOW_H
