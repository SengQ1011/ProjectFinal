#include "environmentalcontroller.h"
#include "hardwareinterface.h"

EnvironmentalController::EnvironmentalController(QObject *parent)
    : QObject(parent) {}

void EnvironmentalController::updateLightLevel(int value) {
  if (value == m_lastValue)
    return;
  m_lastValue = value;

  QString mode = (value < 500) ? "白天" : (value < 800) ? "黃昏" : "夜晚";
  emit lightLevelChanged(value, mode);

  // 自動夜燈邏輯 (項目三)
  if (m_autoMode) {
    if (value < 500) {
      emit requestGpio(LED_YELLOW, 0);
    } else {
      emit requestGpio(LED_YELLOW, 1);
    }
  }
}

void EnvironmentalController::setAutoMode(bool enabled) {
  if (m_autoMode != enabled) {
    m_autoMode = enabled;
    emit autoModeChanged(m_autoMode);

    // 如果切換回自動模式，立即套用當前亮度的設定
    if (m_autoMode && m_lastValue != -1) {
      if (m_lastValue < 500) {
        emit requestGpio(LED_YELLOW, 0);
      } else {
        emit requestGpio(LED_YELLOW, 1);
      }
    }
  }
}

void EnvironmentalController::setManualLed(bool on) {
  if (!m_autoMode) {
    m_manualLedState = on;
    emit requestGpio(LED_YELLOW, m_manualLedState ? 1 : 0);
  }
}
