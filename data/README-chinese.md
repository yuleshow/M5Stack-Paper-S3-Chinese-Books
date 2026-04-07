# 資料

![資料](https://img.shields.io/badge/類型-資料-purple)
![建置輸入](https://img.shields.io/badge/用途-建置輸入-green)

建置腳本和韌體使用的資料檔案。

## 檔案

| 檔案 | 說明 |
|------|------|
| `cangjie5.dict.yaml` | 倉頡第五代輸入法字典，YAML 格式。包含中文字元對字根的對應關係。由 `scripts/convert_cangjie.py` 轉換為二進位格式（`assets/cangjie5.bin`）。 |

## 倉頡字典格式

YAML 檔案將倉頡字根碼（小寫字母 a–z）對應至中文字元：

```yaml
a 日
aa 昌
aaa 晶
ab 旦
...
```

每行包含一個倉頡碼，後接空格和對應的字元。二進位轉換將其壓縮為 ESP32 高效查詢格式。
