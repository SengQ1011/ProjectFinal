import sys
import os
import types

# ========== 終極 Monkey Patch：全面偽造 ultralytics 路徑 ==========
# 1. 將下載的 yolov5_src 加入系統搜尋路徑
yolo_src_path = os.path.join(os.getcwd(), 'yolov5_src')
sys.path.append(yolo_src_path)

try:
    # 確保 v6.0 的模組已經載入
    import models
    import models.common
    import models.yolo
    
    # 2. 開始建立假的 ultralytics 模組樹
    # Level 1: ultralytics
    m_ul = types.ModuleType("ultralytics")
    sys.modules["ultralytics"] = m_ul
    
    # Level 2: ultralytics.nn
    m_nn = types.ModuleType("ultralytics.nn")
    sys.modules["ultralytics.nn"] = m_nn
    m_ul.nn = m_nn
    
    # Level 3: ultralytics.nn.modules (這是你報錯的地方)
    m_modules = types.ModuleType("ultralytics.nn.modules")
    sys.modules["ultralytics.nn.modules"] = m_modules
    m_nn.modules = m_modules
    
    # Level 4: 具體組件映射 (將新版路徑指向舊版實現)
    
    # 映射 'ultralytics.nn.modules.conv' -> models.common
    sys.modules["ultralytics.nn.modules.conv"] = models.common
    m_modules.conv = models.common
    
    # 映射 'ultralytics.nn.modules.block' -> models.common (C3, Bottleneck 等在這裡)
    sys.modules["ultralytics.nn.modules.block"] = models.common
    m_modules.block = models.common

    # 映射 'ultralytics.nn.modules.head' -> models.yolo (Detect Head 在這裡)
    sys.modules["ultralytics.nn.modules.head"] = models.yolo
    m_modules.head = models.yolo
    
    # Level 3b: ultralytics.nn.tasks (模型主體)
    m_tasks = types.ModuleType("ultralytics.nn.tasks")
    sys.modules["ultralytics.nn.tasks"] = m_tasks
    m_nn.tasks = m_tasks
    
    # 映射 DetectionModel -> models.yolo.Model
    m_tasks.DetectionModel = models.yolo.Model
    
    print(">> Monkey Patch Applied: Successfully redirected ultralytics.nn modules to models.common")

except Exception as e:
    print(f">> Monkey Patch Warning: {e}")
# =========================================================================

import torch
import face_recognition
import pickle
import cv2
import numpy as np
import time

class VisionSystem:
    def __init__(self, yolo_weights='best_pig_model_v5n.pt', face_encoding_file='owner_face.pkl',
                 yolo_conf=0.5, face_tolerance=0.5, smart_mode=False, motion_threshold=500):
        
        print("=" * 60)
        print(f"Guardian Eye - Jetson TX2 Edition (Python 3.6)")
        print("=" * 60)

        # 1. 本地載入 YOLO
        print(f"[1/2] Loading YOLO model from local source...")
        try:
            # 必須使用 source='local'，這會讀取我們下載的 yolov5_src
            self.yolo = torch.hub.load('./yolov5_src', 'custom', path=yolo_weights, source='local')
            self.yolo.to('cuda')
            self.yolo_conf = yolo_conf
            self.yolo.eval()
            print("      [OK] YOLOv5nu Local Load Successful")
        except Exception as e:
            print(f"      [ERROR] Local load failed: {e}")
            print("      [TIP] If error is about 'Attribute', the model architecture might be too new for v6.0 code.")
            raise

        # 2. 載入人臉特徵
        print(f"[2/2] Loading face recognizer...")
        try:
            with open(face_encoding_file, 'rb') as f:
                self.owner_encodings = pickle.load(f)
            self.face_tolerance = face_tolerance
            print(f"      [OK] Loaded {len(self.owner_encodings)} owner features")
        except Exception as e:
            print(f"      [ERROR] Face features load failed: {e}")
            raise

        self.smart_mode = smart_mode
        self.motion_threshold = motion_threshold
        self.prev_frame = None
        self.last_result = {'pig_detected': False, 'pig_confidence': 0.0, 'pig_bbox': None,
                            'person_detected': False, 'face_result': 'NO_FACE', 
                            'face_bbox': None, 'face_confidence': 0.0}
        
        # Performance Stats
        self.stats = {'total_frames': 0, 'motion_detected': 0, 'motion_skipped': 0, 
                      'yolo_runs': 0, 'face_recognition_runs': 0, 'active_tracking': False}


    def _detect_motion(self, frame):
        gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        gray = cv2.GaussianBlur(gray, (21, 21), 0)
        if self.prev_frame is None:
            self.prev_frame = gray
            return True
        diff = cv2.absdiff(self.prev_frame, gray)
        thresh = cv2.threshold(diff, 25, 255, cv2.THRESH_BINARY)[1]
        cnt = cv2.countNonZero(cv2.dilate(thresh, None, iterations=2))
        self.prev_frame = gray
        return cnt > self.motion_threshold

    def process_frame(self, frame):
        self.stats['total_frames'] += 1
        if self.smart_mode and not self._detect_motion(frame):
            self.stats['motion_skipped'] += 1
            return self.last_result
        
        self.stats['motion_detected'] += 1
        current_res = {'pig_detected': False, 'pig_confidence': 0.0, 'pig_bbox': None,
                       'person_detected': False, 'face_result': 'NO_FACE', 
                       'face_bbox': None, 'face_confidence': 0.0}

        # YOLO 推理
        self.stats['yolo_runs'] += 1
        y_out = self.yolo(frame)
        det = y_out.xyxy[0].cpu().numpy()

        for d in det:
            x1, y1, x2, y2, conf, cls = d
            if conf >= self.yolo_conf and int(cls) == 0:
                current_res['pig_detected'] = True
                current_res['pig_confidence'] = float(conf)
                current_res['pig_bbox'] = [int(x1), int(y1), int(x2), int(y2)]
                print(f"Pig detected! Conf: {conf:.2%}")
                break

        # 人臉識別
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        locs = face_recognition.face_locations(rgb)
        if len(locs) > 0:
            self.stats['face_recognition_runs'] += 1
            current_res['person_detected'] = True
            top, right, bottom, left = locs[0]
            current_res['face_bbox'] = [left, top, right, bottom]
            
            enc = face_recognition.face_encodings(rgb, locs)
            if len(enc) > 0:
                dist = face_recognition.face_distance(self.owner_encodings, enc[0])
                if len(dist) > 0 and np.min(dist) < self.face_tolerance:
                    current_res['face_result'] = 'OWNER'
                    current_res['face_confidence'] = 1 - np.min(dist)
                    print(f"Identity: OWNER")
                else:
                    current_res['face_result'] = 'STRANGER'
                    current_res['face_confidence'] = 1 - np.min(dist) if len(dist) > 0 else 0
                    print(f"Identity: STRANGER")

        if current_res['pig_detected'] or current_res['person_detected']:
            self.last_result = current_res
            self.stats['active_tracking'] = True
        
        return self.last_result

    def draw_results(self, frame, res):
        out = frame.copy()
        if res['pig_detected']:
            x1, y1, x2, y2 = res['pig_bbox']
            cv2.rectangle(out, (x1, y1), (x2, y2), (0,0,255), 3)
            cv2.putText(out, f"Pig {res['pig_confidence']:.1%}", (x1, y1-10), 0, 0.6, (0,0,255), 2)
        if res['person_detected']:
            x1, y1, x2, y2 = res['face_bbox']
            color = (0,255,0) if res['face_result'] == 'OWNER' else (255,0,0)
            cv2.rectangle(out, (x1, y1), (x2, y2), color, 2)
            cv2.putText(out, f"{res['face_result']} {res['face_confidence']:.1%}", (x1, y1-10), 0, 0.6, color, 2)
        return out
    
    def print_stats(self):
        s = self.stats
        print("\n" + "="*30 + "\nSTATS:\n" + "="*30)
        print(f"Total Frames: {s['total_frames']}")
        print(f"Motion Detected: {s['motion_detected']}")
        print(f"YOLO Runs: {s['yolo_runs']}")
        print("="*30)

if __name__ == "__main__":
    vision = VisionSystem(smart_mode=True)
    cap = cv2.VideoCapture(0) 
    
    print("Warming up camera...")
    for _ in range(5): cap.read()

    while True:
        ret, frame = cap.read()
        if not ret: 
            print("Failed to read frame")
            break
        r = vision.process_frame(frame)
        o = vision.draw_results(frame, r)
        cv2.imshow('Guardian Eye', o)
        if cv2.waitKey(1) == ord('q'): break
    cap.release()
    cv2.destroyAllWindows()