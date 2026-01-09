import face_recognition
import pickle
import cv2
import sys

image_path = "face_10.jpg"
output_file = "owner_face.pkl"

print(f"Loading image: {image_path}")
image = cv2.imread(image_path)
if image is None:
    print("Error: Image not found!")
    sys.exit(1)

# 轉成 RGB
rgb = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)

# 抓特徵
boxes = face_recognition.face_locations(rgb)
encodings = face_recognition.face_encodings(rgb, boxes)

if len(encodings) > 0:
    print(f"Found {len(encodings)} faces. Saving the first one...")
    with open(output_file, "wb") as f:
        pickle.dump([encodings[0]], f) # 存成 list
    print(f"Success! Saved to {output_file}")
else:
    print("Error: No face detected in the photo.")