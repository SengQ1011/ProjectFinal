# Guardian Eye - 軟體架構設計文件 (OOP Design)

本文件說明 `GuardianEye` 智慧門禁系統的軟體設計架構。系統採用 **物件導向 (OOP)** 與 **分層架構 (Layered Architecture)** 設計，旨在實現高內聚、低耦合的專業開發標準。

---

## 🏗️ 系統架構總覽

系統分為三大層級：**硬體抽象層 (HAL)**、**業務邏輯層 (Logic)** 與 **協調層 (Orchestration)**。

### 1. 硬體抽象層 (Hardware Abstraction Layer)

直接與底層硬體（Kernel Driver、SPI 裝置）通訊的模組。

- **`BlackboxInterface`**:
  - **職責**：專職與 `/dev/blackbox` 字元裝置溝通。
  - **功能**：實作 `ioctl` 呼叫來寫入系統日誌與控制 GPIO 電位（LED、蜂鳴器）。
- **`Mcp3008Interface`**:
  - **職責**：封裝 SPI 通訊協定。
  - **功能**：透過 `/dev/spidev0.0` 讀取 MCP3008 的 10-bit 類比數值（ADC）。
- **`hardwareinterface.h`**:
  - **職責**：定義共用的硬體常數。
  - **內容**：包含 GPIO 腳位定義、`ioctl` 命令碼與 `event_data` 結構。

### 2. 業務邏輯層 (Business Logic Layer)

不直接操作硬體，僅處理判斷與演算法，透過訊號與外部聯繫。

- **`SecurityController`**:
  - **職責**：門禁安全邏輯。
  - **功能**：密碼驗證、隨機驗證碼產生。
- **`EnvironmentalController`**:
  - **職責**：環境感知與自動化邏輯。
  - **功能**：解析亮度數值（白天/黃昏/夜晚），並決定是否觸發「自動夜燈」需求。
- **`CameraManager`**:
  - **職責**：非同步影像處理。
  - **功能**：利用 OpenCV 進行背景影像擷取，並將影格轉換為 Qt 格式。

### 3. 協調層 (Orchestration Layer)

- **`MainWindow`**:
  - **職責**：系統總調度與 UI 呈現。
  - **功能**：
    1.  初始化所有模組與執行緒（Threads）。
    2.  **訊號連結 (Wiring)**：將 Logic 層發出的「需求訊號」連結到 HAL 層的「執行 Slot」。
    3.  處理使用者介面互動（快捷鍵、密碼輸入）。

---

## 📡 模組通訊流程 (Signals & Slots)

系統利用 Qt 的訊號槽機制實現解耦，典型的通訊流程如下：

1.  **感測器觸發自動動作**：

    - `Mcp3008Interface` (讀取 ADC) -> `MainWindow` (接收數值)
    - `MainWindow` -> `EnvironmentalController::updateLightLevel(value)`
    - `EnvironmentalController` 發出 `requestGpio(LED_YELLOW, 1)` 訊號
    - `MainWindow` 將該訊號導向 `BlackboxInterface::setGpio`

2.  **安全驗證紀錄**：
    - `SecurityController` 驗證密碼後發出 `requestLog("密碼成功", 0)`
    - `MainWindow` 將其導向 `BlackboxInterface::logEvent`

---

## 🚀 技術亮點

- **多執行緒並行運算**：影像擷取 (Camera) 與 業務邏輯 (Logic) 分別運行於獨立執行緒，確保 UI 介面流暢不卡頓。
- **介面與實作分離**：Logic 模組不關心硬體如何操作，僅透過 `request` 訊號表達需求。若硬體更換，只需更換 HAL 模組，不需修改邏輯代碼。
- **內核整合**：深度整合自定義 Kernel Driver，展現從應用層到內核層的完整技術棧。

---

## 🛠️ 未來擴充性

- **新增感測器**：只需繼承 `QObject` 建立新的 `SensorController` 並在 `MainWindow` 連結訊號即可。
- **加入 AI 視覺**：可在 `logicThread` 中加入 YOLO 推理模組，並透過 `requestGpio` 觸發入侵警報。
