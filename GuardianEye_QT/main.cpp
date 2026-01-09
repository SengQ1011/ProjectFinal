#include "mainwindow.h"
#include <QApplication>
#include <QTextCodec>

int main(int argc, char *argv[]) {
  // 強制使用 xcb 平台插件，避免 Wayland 相關錯誤
  qputenv("QT_QPA_PLATFORM", "xcb");

  // 強制設定全局編碼為 UTF-8 (解決 Qt 5 在部分系統上的亂碼問題)
  QTextCodec *codec = QTextCodec::codecForName("UTF-8");
  QTextCodec::setCodecForLocale(codec);

  QApplication a(argc, argv);
  MainWindow w;
  w.show();
  return a.exec();
}
