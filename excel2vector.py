import pandas as pd
import os

def load_xlsx_map(filename):
    # 檢查檔案是否存在
    if not os.path.exists(filename):
        print(f"錯誤: 找不到檔案 '{filename}'")
        return []

    print(f"正在讀取檔案: {filename} ...")
    
    # 直接讀取 Excel 檔，強制將所有內容讀為字串 (dtype=str)，避免 0000 被轉成 0
    try:
        df = pd.read_excel(filename, header=None, dtype=str)
    except ValueError as e:
        print("讀取錯誤: 請確認檔案是否為有效的 .xlsx 格式。")
        print(f"詳細錯誤: {e}")
        return []

    map_array = []
    
    # 遍歷每一列 (Row)
    for index, row in df.iterrows():
        processed_row = []
        for cell in row:
            # 取得儲存格內容字串，去除前後空白
            # 如果格子是 NaN (空值)，轉為 "00000000"
            if pd.isna(cell):
                binary_str = "00000000"
            else:
                binary_str = str(cell).strip()
            
            # 處理可能因為 Excel 格式跑到的小數點 (例如 "1010.0")
            if "." in binary_str:
                binary_str = binary_str.split(".")[0]

            # 確保長度足夠 (補滿 8 位)
            # 你的格式是 8 位：前4位(物件) + 後4位(牆壁)
            binary_str = binary_str.zfill(8)

            try:
                # 核心轉換邏輯
                # 這裡將整個 8 位二進制字串轉為十進制數字
                # 例如: '00001010' (牆壁上下) -> 10
                # 例如: '00010000' (門) -> 16
                decimal_value = int(binary_str, 2)
                processed_row.append(decimal_value)
            except ValueError:
                print(f"警告: 發現無法轉換的內容 '{cell}'，已預設為 0")
                processed_row.append(0)
        
        map_array.append(processed_row)
            
    return map_array

# --- 主程式區 ---

# 請將這裡改成你真正的 .xlsx 檔名 (不要用 .csv)
file_path = "map_excel.xlsx" 

decimal_map = load_xlsx_map(file_path)

if decimal_map:
    print(f"\n成功轉換！地圖大小: {len(decimal_map)}x{len(decimal_map[0])}")
    print("前 5 列數據:")
    for row in decimal_map[:5]:
        print(row)
        
    # 如果你想把這個陣列存成文字檔給其他程式用
    with open("map_output.txt", "w", encoding="utf-8") as f:
        f.write(str(decimal_map))
    print("\n已將陣列內容輸出至 map_output.txt")