import sys
import os
import cv2
import face_recognition
import pickle
import glob

# ========== 設定 ==========
input_folder = "face_dataset"       # 你的照片資料夾名稱
output_file = "owner_face.pkl"
# ========================

print(f"Checking folder: {input_folder}...")
if not os.path.exists(input_folder):
    print(f"[ERROR] Folder '{input_folder}' not found!")
    print("Please create a folder named 'faces' and put your 20 photos inside.")
    sys.exit(1)

# 抓取所有 jpg, jpeg, png
image_paths = []
for ext in ['*.jpg', '*.jpeg', '*.png', '*.JPG', '*.PNG']:
    image_paths.extend(glob.glob(os.path.join(input_folder, ext)))

if len(image_paths) == 0:
    print("[ERROR] No images found in the folder!")
    sys.exit(1)

print(f"Found {len(image_paths)} images. Starting processing...")
print("="*50)

known_encodings = []
success_count = 0

for i, img_path in enumerate(image_paths):
    filename = os.path.basename(img_path)
    print(f"[{i+1}/{len(image_paths)}] Processing: {filename}...")
    
    # 使用 OpenCV 讀取 (相容性最好)
    img = cv2.imread(img_path)
    if img is None:
        print(f"  -> [WARN] Could not read image. Skipping.")
        continue
        
    # 轉 RGB
    rgb = cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
    
    # 偵測人臉 (使用 hog 模型，比較快且省記憶體)
    boxes = face_recognition.face_locations(rgb, model="hog")
    
    if len(boxes) > 0:
        # 轉特徵向量
        encodings = face_recognition.face_encodings(rgb, boxes)
        if len(encodings) > 0:
            known_encodings.append(encodings[0])
            success_count += 1
            print(f"  -> [OK] Face encoded.")
        else:
            print(f"  -> [WARN] Face detected but encoding failed.")
    else:
        print(f"  -> [WARN] No face found in this photo.")

print("="*50)

if success_count > 0:
    print(f"Saving {success_count} face features to {output_file}...")
    with open(output_file, "wb") as f:
        # 關鍵：使用 protocol=2 讓 Python 3.6 絕對能讀
        pickle.dump(known_encodings, f, protocol=2)
    print("\n✅ [SUCCESS] Database built successfully!")
    print(f"Now you can run: python3 vision_system.py")
else:
    print("\n❌ [ERROR] No valid faces were extracted from any photos.")
