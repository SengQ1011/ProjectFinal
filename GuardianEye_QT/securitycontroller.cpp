#include "securitycontroller.h"
#include <QDateTime>

SecurityController::SecurityController(QObject *parent) : QObject(parent) {
  qsrand(QDateTime::currentMSecsSinceEpoch());
  emit requestLog("安全系統初始化", 0);
}

void SecurityController::verifyPassword(const QString &input) {
  bool success = false;

  // 1. 如果有隨機碼 (處於警報解鎖狀態)，優先驗證隨機碼
  if (!m_currentRandomCode.isEmpty()) {
    success = (input == m_currentRandomCode);
    if (success) {
      m_currentRandomCode = ""; // 驗證成功後清除
      m_isAlarmActive = false;  // 解除警報狀態
      emit requestLog("現場隨機碼驗證成功，警報解除", 0);
    } else {
      emit requestLog("現場隨機碼驗證失敗: " + input, 1);
    }
  }
  // 2. 如果警報中但還沒有隨機碼 (等待遠端授權)，則主密碼暫時失效
  else if (m_isAlarmActive) {
    success = false;
    emit requestLog("警報中，請先完成遠端授權再輸入隨機碼", 1);
  }
  // 3. 正常模式驗證主密碼
  else {
    success = (input == m_masterPassword);
    emit requestLog(success ? "主密碼驗證成功" : "主密碼驗證失敗",
                    success ? 0 : 1);
  }

  emit passwordVerified(success);
}

QString SecurityController::generateRandomCode(bool showInLog) {
  int code = qrand() % 900 + 100;
  m_currentRandomCode = QString::number(code);

  if (showInLog) {
    emit requestLog("生成隨機驗證碼: " + m_currentRandomCode, 0);
  } else {
    emit requestLog("已為遠端解鎖生成隨機驗證碼 (密碼已傳送至遠端)", 0);
  }

  return m_currentRandomCode;
}
