import sys
import os
import time
import pickle
import numpy as np
import cv2
import warnings

warnings.filterwarnings("ignore", category=UserWarning)

# 嘗試載入 PyTorch 和 Face Recognition
try:
    import torch
    import face_recognition
except ImportError as e:
    print(f"[ERROR] 缺少必要套件: {e}")
    sys.exit(1)

# ========== 環境路徑設定 ==========
yolo_src_path = os.path.join(os.getcwd(), 'models/yolov5_src')
if yolo_src_path not in sys.path:
    sys.path.append(yolo_src_path)

class VisionSystem:
    def __init__(self, yolo_weights='./models/best_pig_model_v5n.pt', face_encoding_file='./models/owner_face.pkl',
                 yolo_conf=0.5, face_tolerance=0.45, motion_threshold=1000): 
        
        print("="*40)
        print("[INFO] Initializing Vision System...")
        print("="*40)

        # 1. 載入 YOLO (專門負責找豬)
        print("[INFO] Loading Pig Detection Model...")
        try:
            try:
                import models.yolo
                if not hasattr(models.yolo, 'DetectionModel'):
                    models.yolo.DetectionModel = models.yolo.Model
            except ImportError:
                pass

            self.yolo = torch.hub.load(yolo_src_path, 'custom', path=yolo_weights, source='local')
            self.yolo.to('cuda')
            self.yolo_conf = yolo_conf
            self.yolo.eval()
        except Exception as e:
            print(f"[ERROR] Failed to load YOLO: {e}")
            sys.exit(1)

        # 2. 載入人臉特徵 (專門負責找人)
        print("[INFO] Loading Human Face Database...")
        self.owner_encodings = []
        if os.path.exists(face_encoding_file):
            try:
                with open(face_encoding_file, 'rb') as f:
                    self.owner_encodings = pickle.load(f)
                print(f"[INFO] Loaded {len(self.owner_encodings)} face identities.")
            except Exception as e:
                print(f"[ERROR] Failed to load face file: {e}")
        
        self.face_tolerance = face_tolerance
        self.motion_threshold = motion_threshold
        self.prev_gray = None
        self.motion_cooldown = 0
        self.current_motion_val = 0 # [除錯用] 顯示當前移動量
        
        # 狀態緩存
        self.last_result = {
            'pig_detected': False, 'pig_conf': 0.0, 'pig_bbox': None,
            'person_detected': False, 'face_id': 'Unknown', 'face_bbox': None
        }

    def detect_motion(self, frame):
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        gray = cv2.GaussianBlur(gray, (21, 21), 0)
        
        if self.prev_gray is None:
            self.prev_gray = gray
            return False
            
        frame_delta = cv2.absdiff(self.prev_gray, gray)
        thresh = cv2.threshold(frame_delta, 25, 255, cv2.THRESH_BINARY)[1]
        thresh = cv2.dilate(thresh, None, iterations=2)
        
        # 計算變動的像素數量
        change_area = cv2.countNonZero(thresh)
        self.current_motion_val = change_area # 存起來顯示用
        self.prev_gray = gray
        
        return change_area > self.motion_threshold

    def process_frame(self, frame, frame_count):
        # [Stage 1] 動態偵測
        is_moving = self.detect_motion(frame)
        
        if is_moving:
            self.motion_cooldown = 30 # 有動靜就保持活躍 30 幀 (約1秒)
        
        if self.motion_cooldown > 0:
            self.motion_cooldown -= 1
            
            # 初始化結果
            current_res = {
                'pig_detected': False, 'pig_conf': 0.0, 'pig_bbox': None,
                'person_detected': False, 'face_id': 'Unknown', 'face_bbox': None
            }

            # --- [軌道 A] : 找豬 (每 2 幀跑一次 YOLO) ---
            if frame_count % 2 == 0:
                y_out = self.yolo(frame) 
                det = y_out.xyxy[0].cpu().numpy()
                for d in det:
                    x1, y1, x2, y2, conf, cls = d
                    if int(cls) == 0 and conf > self.yolo_conf:
                        current_res['pig_detected'] = True
                        current_res['pig_conf'] = float(conf)
                        current_res['pig_bbox'] = [int(x1), int(y1), int(x2), int(y2)]
                        break 
            else:
                # 沿用上一次結果
                current_res['pig_detected'] = self.last_result['pig_detected']
                current_res['pig_conf'] = self.last_result['pig_conf']
                current_res['pig_bbox'] = self.last_result['pig_bbox']

            # --- [軌道 B] : 找人 (每 5 幀跑一次 dlib) ---
            if frame_count % 5 == 0:
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                small_rgb = cv2.resize(rgb, (0, 0), fx=0.25, fy=0.25)
                locs = face_recognition.face_locations(small_rgb, model="hog")
                
                if len(locs) > 0:
                    current_res['person_detected'] = True
                    top, right, bottom, left = [v * 4 for v in locs[0]]
                    current_res['face_bbox'] = [left, top, right, bottom]
                    
                    if self.owner_encodings:
                        encs = face_recognition.face_encodings(rgb, [(top, right, bottom, left)])
                        if len(encs) > 0:
                            dists = face_recognition.face_distance(self.owner_encodings, encs[0])
                            min_dist = np.min(dists)
                            if min_dist < self.face_tolerance:
                                current_res['face_id'] = f"OWNER ({1-min_dist:.2f})"
                            else:
                                current_res['face_id'] = f"STRANGER ({1-min_dist:.2f})"
                    else:
                         current_res['face_id'] = "Human"
            else:
                current_res['person_detected'] = self.last_result['person_detected']
                current_res['face_id'] = self.last_result['face_id']
                current_res['face_bbox'] = self.last_result['face_bbox']

            # --- [衝突解決] ---
            if current_res['pig_detected'] and current_res['person_detected']:
                pig_box = current_res['pig_bbox']
                face_box = current_res['face_bbox']
                face_cx = (face_box[0] + face_box[2]) // 2
                face_cy = (face_box[1] + face_box[3]) // 2
                if (pig_box[0] < face_cx < pig_box[2]) and (pig_box[1] < face_cy < pig_box[3]):
                    current_res['pig_detected'] = False
            
            self.last_result = current_res
            return current_res

        else:
            return self.last_result

    def draw_hud(self, frame, res):
        vis = frame.copy()
        
        # 1. 顯示系統狀態 (Mode: Active/Idle)
        # 背景黑框 (位置加大以容納更多資訊)
        cv2.rectangle(vis, (5, 5), (320, 65), (0, 0, 0), -1)
        
        if self.motion_cooldown > 0:
            status_text = "MODE: ACTIVE"
            status_color = (0, 255, 0) # 綠色
        else:
            status_text = "MODE: IDLE"
            status_color = (150, 150, 150) # 灰色

        # 顯示模式
        cv2.putText(vis, status_text, (15, 30), 
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, status_color, 2)
        
        # [DEBUG] 顯示動態數值 (讓你知道是否達到閾值)
        motion_text = f"Motion: {self.current_motion_val} / {self.motion_threshold}"
        motion_color = (0, 255, 255) if self.current_motion_val > self.motion_threshold else (100, 100, 100)
        cv2.putText(vis, motion_text, (15, 55), 
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, motion_color, 1)

        # 2. 畫豬
        if res['pig_detected']:
            x1, y1, x2, y2 = res['pig_bbox']
            cv2.rectangle(vis, (x1, y1), (x2, y2), (0, 0, 255), 2)
            cv2.putText(vis, f"PIG {res['pig_conf']:.2f}", (x1, y1 - 10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)

        # 3. 畫人
        if res['person_detected']:
            x1, y1, x2, y2 = res['face_bbox']
            if "OWNER" in res['face_id']:
                color = (0, 255, 0)
            elif "STRANGER" in res['face_id']:
                color = (0, 0, 255)
            else:
                color = (0, 255, 255)

            cv2.rectangle(vis, (x1, y1), (x2, y2), color, 2)
            cv2.putText(vis, res['face_id'], (x1, y1 - 10),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)

        return vis

# V4L2 驅動 Pipeline (USB相機穩定版)
def get_usb_pipeline():
    # USB 相機直接用 V4L2，不走 GStreamer 比較穩
    return 1 # 假設是 /dev/video1

# CSI 驅動 Pipeline (內建相機)
def get_csi_pipeline(sensor_id=0, flip_method=2):
    return (
        "nvarguscamerasrc sensor-id=%d ! "
        "video/x-raw(memory:NVMM), width=1280, height=720, format=NV12, framerate=30/1 ! "
        "nvvidconv flip-method=%d ! "
        "video/x-raw, width=640, height=480, format=BGRx ! "
        "videoconvert ! "
        "video/x-raw, format=BGR ! appsink drop=True"
        % (sensor_id, flip_method)
    )

if __name__ == "__main__":
    print("Select Camera: [1] CSI Onboard  [2] USB Webcam")
    choice = input("Choice: ").strip()
    
    system = VisionSystem(motion_threshold=1000)
    
    cap = None
    if choice == '1':
        print("[INFO] Starting CSI Camera...")
        cap = cv2.VideoCapture(get_csi_pipeline(0, 2), cv2.CAP_GSTREAMER)
    else:
        print("[INFO] Starting USB Camera...")
        # 嘗試 /dev/video1
        cap = cv2.VideoCapture(1, cv2.CAP_V4L2)
        if not cap.isOpened():
            print("[WARN] video1 failed, trying video0...")
            cap = cv2.VideoCapture(0, cv2.CAP_V4L2)
        
        if cap.isOpened():
            # 強制鎖定解析度 (防卡頓)
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, 640)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 480)
            cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*'MJPG'))

    if not cap or not cap.isOpened():
        print("[FATAL] Camera failed.")
        sys.exit(1)

    print("[INFO] System Started.")
    print("[TIP] Press 'q' to quit program safely.")
    
    frame_counter = 0

    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                time.sleep(0.1)
                continue
            
            frame_counter += 1
            results = system.process_frame(frame, frame_counter)
            
            # 這裡確保每一幀都畫 HUD
            display = system.draw_hud(frame, results)
            
            cv2.imshow("Guardian Eye", display)
            
            if cv2.waitKey(1) & 0xFF == ord('q'):
                print("[INFO] Quitting...")
                break
                
    except KeyboardInterrupt:
        print("\n[INFO] Keyboard Interrupt received.")
    finally:
        if cap: cap.release()
        cv2.destroyAllWindows()
        print("[INFO] System Closed.")
