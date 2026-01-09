import sys
import os
import time
import pickle
import numpy as np
import cv2
import warnings
import json
import base64
import argparse
import signal

warnings.filterwarnings("ignore", category=UserWarning)

# ========== 訊號處理：確保優雅退出 ==========
def handle_signal(signum, frame):
    sys.exit(0)

signal.signal(signal.SIGTERM, handle_signal)
signal.signal(signal.SIGINT, handle_signal)

# ========== 參數解析 ==========
parser = argparse.ArgumentParser()
parser.add_argument('--qt_mode', action='store_true', help='Enable Qt integration mode (JSON output)')
parser.add_argument('--camera', type=str, default='0', help='Camera index (default: 0)')
args, unknown = parser.parse_known_args()

# 嘗試載入 PyTorch 和 Face Recognition
if args.qt_mode:
    print(json.dumps({"status": "loading", "msg": "正在載入 AI 模型庫 (PyTorch/OpenCV)..."}))
    sys.stdout.flush()

print(f"[DEBUG] Python Version: {sys.version}")
print(f"[DEBUG] Working Directory: {os.getcwd()}")

try:
    import torch
    import face_recognition
    print(f"[DEBUG] PyTorch Version: {torch.__version__}")
    print(f"[DEBUG] OpenCV Version: {cv2.__version__}")
    print(f"[DEBUG] CUDA Available: {torch.cuda.is_available()}")
    if torch.cuda.is_available():
        print(f"[DEBUG] CUDA Device: {torch.cuda.get_device_name(0)}")
except ImportError as e:
    if args.qt_mode:
        print(json.dumps({"error": f"缺少必要套件: {e}"}))
        sys.stdout.flush()
    else:
        print(f"[ERROR] 缺少必要套件: {e}")
    sys.exit(1)

# ========== 環境路徑設定 ==========
yolo_src_path = os.path.join(os.getcwd(), 'models/yolov5_src')
if yolo_src_path not in sys.path:
    sys.path.append(yolo_src_path)

# 新增：針對 TX2 優化的 USB GStreamer Pipeline
def get_usb_gst_pipeline(dev_idx=0):
    # 增加 videoconvert 與 videoscale 確保格式相容
    return (
        f"v4l2src device=/dev/video{dev_idx} ! "
        "video/x-raw, width=640, height=480 ! "
        "videoconvert ! video/x-raw, format=BGR ! appsink drop=True"
    )

# CSI 驅動 Pipeline
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

class VisionSystem:
    def __init__(self, yolo_weights='./models/best_pig_model_v5n.pt', face_encoding_file='./models/owner_face.pkl',
                 yolo_conf=0.6, face_tolerance=0.45, motion_threshold=1000): 
        
        print("="*40)
        print("[INFO] Initializing Vision System...")
        print("="*40)

        if args.qt_mode:
            print(json.dumps({"status": "loading", "msg": "正在初始化 YOLO 權重 (CUDA)..."}))
            sys.stdout.flush()

        # 1. 載入 YOLO
        try:
            import models.yolo
            if not hasattr(models.yolo, 'DetectionModel'):
                models.yolo.DetectionModel = models.yolo.Model
            
            print(f"[DEBUG] Loading YOLO weights from: {yolo_weights}")
            t0 = time.time()
            self.yolo = torch.hub.load(yolo_src_path, 'custom', path=yolo_weights, source='local')
            print(f"[DEBUG] YOLO loaded in {time.time() - t0:.2f} seconds")
            
            if torch.cuda.is_available():
                self.yolo.to('cuda')
                print("[INFO] YOLO moved to CUDA")
            else:
                print("[INFO] CUDA not available, using CPU")
            self.yolo_conf = yolo_conf
            self.yolo.eval()
        except Exception as e:
            if args.qt_mode:
                print(json.dumps({"error": f"YOLO 載入失敗: {e}"}))
                sys.stdout.flush()
            print(f"[ERROR] Failed to load YOLO: {e}")
            import traceback
            traceback.print_exc()
            sys.exit(1)

        # 2. 載入人臉特徵
        if args.qt_mode:
            print(json.dumps({"status": "loading", "msg": "正在載入人臉資料庫..."}))
            sys.stdout.flush()
        
        print(f"[DEBUG] Loading face encodings from: {face_encoding_file}")
        self.owner_encodings = []
        if os.path.exists(face_encoding_file):
            try:
                with open(face_encoding_file, 'rb') as f:
                    self.owner_encodings = pickle.load(f)
                print(f"[INFO] Loaded {len(self.owner_encodings)} face identities.")
            except Exception as e:
                print(f"[ERROR] Failed to load face file: {e}")
                import traceback
                traceback.print_exc()
        
        self.face_tolerance = face_tolerance
        self.motion_threshold = motion_threshold
        self.prev_gray = None
        self.motion_cooldown = 0
        self.current_motion_val = 0
        
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
        change_area = cv2.countNonZero(thresh)
        self.current_motion_val = change_area
        self.prev_gray = gray
        return change_area > self.motion_threshold

    def process_frame(self, frame, frame_count):
        is_moving = self.detect_motion(frame)
        if is_moving:
            self.motion_cooldown = 30 
        
        if self.motion_cooldown > 0:
            self.motion_cooldown -= 1
            current_res = {
                'pig_detected': False, 'pig_conf': 0.0, 'pig_bbox': None,
                'person_detected': False, 'face_id': 'Unknown', 'face_bbox': None
            }

            # [軌道 A] : 找豬 (每 2 幀)
            if frame_count % 2 == 0:
                t_yolo = time.time()
                y_out = self.yolo(frame) 
                det = y_out.xyxy[0].cpu().numpy()
                print(f"[DEBUG] Frame {frame_count}: YOLO inference took {time.time() - t_yolo:.3f}s, found {len(det)} objects")
                for d in det:
                    x1, y1, x2, y2, conf, cls = d
                    if int(cls) == 0 and conf > self.yolo_conf:
                        current_res['pig_detected'] = True
                        current_res['pig_conf'] = float(conf)
                        current_res['pig_bbox'] = [int(x1), int(y1), int(x2), int(y2)]
                        print(f"[DEBUG] >> PIG detected! Conf: {conf:.2f}")
                        break 
            else:
                current_res['pig_detected'] = self.last_result['pig_detected']
                current_res['pig_conf'] = self.last_result['pig_conf']
                current_res['pig_bbox'] = self.last_result['pig_bbox']

            # [軌道 B] : 找人 (每 5 幀)
            if frame_count % 5 == 0:
                t_face = time.time()
                rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                small_rgb = cv2.resize(rgb, (0, 0), fx=0.25, fy=0.25)
                locs = face_recognition.face_locations(small_rgb, model="hog")
                print(f"[DEBUG] Frame {frame_count}: Face detection took {time.time() - t_face:.3f}s, found {len(locs)} faces")
                if len(locs) > 0:
                    current_res['person_detected'] = True
                    top, right, bottom, left = [v * 4 for v in locs[0]]
                    current_res['face_bbox'] = [left, top, right, bottom]
                    if self.owner_encodings:
                        t_enc = time.time()
                        encs = face_recognition.face_encodings(rgb, [(top, right, bottom, left)])
                        if len(encs) > 0:
                            dists = face_recognition.face_distance(self.owner_encodings, encs[0])
                            min_dist = np.min(dists)
                            current_res['face_id'] = "OWNER" if min_dist < self.face_tolerance else "STRANGER"
                            print(f"[DEBUG] >> Person identified: {current_res['face_id']} (Dist: {min_dist:.3f}, took {time.time() - t_enc:.3f}s)")
                    else:
                         current_res['face_id'] = "Human"
                         print(f"[DEBUG] >> Person identified: Human (No encoding database)")
            else:
                current_res['person_detected'] = self.last_result['person_detected']
                current_res['face_id'] = self.last_result['face_id']
                current_res['face_bbox'] = self.last_result['face_bbox']

            # [衝突解決：優先相信人臉]
            if current_res['person_detected']:
                current_res['pig_detected'] = False
            
            self.last_result = current_res
            return current_res
        else:
            idle = {'pig_detected': False, 'person_detected': False, 'face_id': 'Idle'}
            self.last_result = idle
            return idle

    def draw_hud(self, frame, res):
        vis = frame.copy()
        cv2.rectangle(vis, (5, 5), (320, 65), (0, 0, 0), -1)
        status_text = "MODE: ACTIVE" if self.motion_cooldown > 0 else "MODE: IDLE"
        status_color = (0, 255, 0) if self.motion_cooldown > 0 else (150, 150, 150)
        cv2.putText(vis, status_text, (15, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, status_color, 2)
        
        if res.get('pig_detected'):
            x1, y1, x2, y2 = res['pig_bbox']
            cv2.rectangle(vis, (x1, y1), (x2, y2), (0, 0, 255), 2)
            cv2.putText(vis, f"PIG {res['pig_conf']:.2f}", (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 0, 255), 2)

        if res.get('person_detected'):
            x1, y1, x2, y2 = res['face_bbox']
            color = (0, 255, 0) if "OWNER" in res['face_id'] else (0, 0, 255)
            cv2.rectangle(vis, (x1, y1), (x2, y2), color, 2)
            cv2.putText(vis, res['face_id'], (x1, y1 - 10), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color, 2)
        return vis

if __name__ == "__main__":
    choice = args.camera if args.qt_mode else None
    if choice is None:
        print("Select Camera: [1] CSI Onboard  [2] USB Webcam")
        choice = input("Choice: ").strip()
    
    if args.qt_mode:
        print(json.dumps({"status": "loading", "msg": "正在開啟攝影機..."}))
        sys.stdout.flush()

    system = VisionSystem()
    cap = None
    if choice == '1':
        cap = cv2.VideoCapture(get_csi_pipeline(), cv2.CAP_GSTREAMER)
    else:
        # 1. 嘗試使用強健的 GStreamer Pipeline
        idx = int(choice) if not args.qt_mode else int(args.camera)
        gst_str = get_usb_gst_pipeline(idx)
        print(f"[INFO] Trying GStreamer: {gst_str}")
        cap = cv2.VideoCapture(gst_str, cv2.CAP_GSTREAMER)
        
        # 2. 如果失敗，嘗試最基礎的 V4L2 模式
        if not cap or not cap.isOpened():
            print(f"[WARN] GStreamer failed for /dev/video{idx}, falling back to V4L2")
            cap = cv2.VideoCapture(idx, cv2.CAP_V4L2)
            
        # 3. 如果還是失敗，嘗試自動尋找下一個索引 (例如 0 失敗找 1)
        if not cap or not cap.isOpened():
            alt_idx = 1 if idx == 0 else 0
            print(f"[WARN] /dev/video{idx} failed, trying /dev/video{alt_idx}")
            cap = cv2.VideoCapture(alt_idx, cv2.CAP_V4L2)

    if not cap or not cap.isOpened():
        if args.qt_mode:
            print(json.dumps({"error": "無法開啟攝影機"}))
            sys.stdout.flush()
        else:
            print("[ERROR] Camera failed")
        sys.exit(1)

    if args.qt_mode:
        print(json.dumps({"status": "running", "msg": "系統已啟動"}))
        sys.stdout.flush()

    frame_counter = 0
    t_start = time.time()
    try:
        while True:
            ret, frame = cap.read()
            if not ret:
                print("[WARN] Camera read failed")
                time.sleep(0.01)
                continue
            
            frame_counter += 1
            if frame_counter % 100 == 0:
                fps = 100 / (time.time() - t_start)
                print(f"[DEBUG] Average FPS (last 100 frames): {fps:.2f}")
                t_start = time.time()

            results = system.process_frame(frame, frame_counter)
            display = system.draw_hud(frame, results)
            
            if args.qt_mode:
                _, buffer = cv2.imencode('.jpg', display, [cv2.IMWRITE_JPEG_QUALITY, 80])
                img_base64 = base64.b64encode(buffer).decode('utf-8')
                output = {
                    "img": img_base64,
                    "pig_detected": results['pig_detected'],
                    "person_detected": results['person_detected'],
                    "face_id": results['face_id']
                }
                print(json.dumps(output))
                sys.stdout.flush()
            else:
                cv2.imshow("Guardian Eye", display)
                if cv2.waitKey(1) & 0xFF == ord('q'): break
    finally:
        if cap: cap.release()
        cv2.destroyAllWindows()
