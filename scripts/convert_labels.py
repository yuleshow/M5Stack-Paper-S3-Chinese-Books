#!/usr/bin/env python3
"""
Convert static UI label strings to pre-rendered bitmap C headers.

Usage:
  python3 convert_labels.py [--font FONT_PATH]

Renders each label string at its specified font size using the given TTF font,
then outputs a C header file per label with the bitmap data (4-bit grayscale,
matching M5Stack Paper S3's e-ink display).

The generated headers provide:
  - const uint8_t label_XXX_bitmap[] PROGMEM  (4-bit packed pixel data)
  - const uint16_t label_XXX_w, label_XXX_h   (dimensions)

A master header "label_bitmaps.h" includes all individual headers and provides
a lookup function.
"""

import os
import sys
import re
import hashlib
from PIL import Image, ImageDraw, ImageFont, ImageOps

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))

# ── Configuration ──────────────────────────────────────────────────────────────

FONT_PATH = os.path.expanduser("~/Library/Fonts/GenYoMinTW-Regular.ttf")
KAI_FONT_PATH = os.path.expanduser("~/Library/Fonts/TW-Kai-98_1.ttf")
OUT_DIR = os.path.join("src", "labels")

# All static UI labels extracted from source code.
# Format: (text, font_size, var_suffix) or (text, font_size, var_suffix, font_path)
# var_suffix must be a valid C identifier fragment.
# font_path is optional; defaults to FONT_PATH if omitted.
#
# To add new labels, append to this list and re-run the script.

LABELS = [
    # ── Dashboard icon labels (size 32) ──
    ("電子書",       32, "ebook"),
    ("日曆",         32, "calendar"),
    ("待辦事項",     32, "todo"),
    ("採辦",         32, "shopping"),
    ("天氣",         32, "weather"),
    ("壁紙",         32, "wallpaper"),
    ("工具",         32, "tools"),
    ("設定",         32, "settings"),
    ("求籖",         32, "fortune_slips"),

    # ── Fortune Slips menu labels ──
    ("觀音靈籖",     36, "kuanyin_slips"),
    ("淺草寺靈籖",   36, "sensoji_slips"),
    ("醒世格言",     28, "sleep_motto"),

    # ── Fortune Slips shake screen labels (Kai font) ──
    ("誠心祝禱",     64, "sincere_prayer", KAI_FONT_PATH),
    ("輕搖求籖",     64, "shake_to_draw", KAI_FONT_PATH),

    # ── Individual characters for vertical rendering (Kai font, 64pt) ──
    ("誠",           64, "char_cheng", KAI_FONT_PATH),
    ("心",           64, "char_xin", KAI_FONT_PATH),
    ("祝",           64, "char_zhu", KAI_FONT_PATH),
    ("禱",           64, "char_dao", KAI_FONT_PATH),
    ("輕",           64, "char_qing", KAI_FONT_PATH),
    ("搖",           64, "char_yao", KAI_FONT_PATH),
    ("求",           64, "char_qiu", KAI_FONT_PATH),
    ("籖",           64, "char_qian", KAI_FONT_PATH),

    # ── Common buttons (actual sizes from source) ──
    ("清除",         24, "clear"),          # main.cpp: size 24
    ("確定",         24, "ok"),             # cangjie_input.cpp: size 24
    ("取消",         24, "cancel"),         # cangjie_input.cpp: size 24
    ("啟用",         32, "enable"),         # setup_ui/usb_msc: size 32
    ("關閉",         32, "disable"),        # setup_ui/usb_msc: size 32
    ("說明:",        22, "help"),           # setup_ui/usb_msc: size 22

    # ── Setup menu items Page 1 (drawn at size 32 in menu list) ──
    ("設定",         40, "settings_title"),       # setup_ui.cpp L544: size 40
    ("WiFi 設定",    32, "wifi_settings"),        # setup_ui.cpp L555: size 32
    ("時區設定",     32, "timezone_settings"),    # setup_ui.cpp L574: size 32
    ("檔案上傳伺服器", 32, "file_server"),        # setup_ui.cpp L586: size 32
    ("USB 外接磁碟", 32, "usb_msc"),              # setup_ui.cpp L604: size 32
    ("圖標來源",     32, "icon_source"),          # setup_ui.cpp L619: size 32

    # ── Setup menu items Page 2 (drawn at size 32 in menu list) ──
    ("曆法計算",     32, "calendar_calc"),        # setup_ui.cpp L712: size 32
    ("藍牙",         32, "bluetooth"),             # setup_ui.cpp L724: size 32
    ("自動休眠",     32, "auto_sleep"),            # setup_ui.cpp L734: size 32
    ("漫畫縮放模式", 32, "comic_zoom_mode"),       # setup_ui.cpp L764: size 32
    ("自由定位 - 點擊處為中心", 22, "comic_zoom_free"),  # setup_ui.cpp L767: size 22
    ("四分區 - 點擊顯示該象限", 22, "comic_zoom_quad"),  # setup_ui.cpp L769: size 22

    # ── Setup status text (drawn at size 32 and 22) ──
    ("未設定",       22, "not_set"),         # setup_ui.cpp L564: size 22
    ("狀態: ",       32, "status"),          # setup_ui/usb_msc: size 32
    ("目前: ",       32, "current"),         # setup_ui.cpp L498: size 32
    ("執行中",       32, "running"),         # setup_ui/usb_msc: size 32
    ("已啟用",       32, "enabled"),         # setup_ui.cpp L444: size 32
    ("未啟用",       32, "disabled_32"),     # setup_ui.cpp L452: size 32
    ("未啟用",       22, "disabled_22"),     # setup_ui.cpp L594/609: size 22
    ("執行中 - 裝置停用", 22, "running_disabled"),  # setup_ui.cpp L607: size 22
    ("已啟用 (需連接WiFi)", 22, "enabled_wifi"),    # setup_ui.cpp L592: size 22
    ("SD 卡優先（可自訂圖標）", 22, "sd_desc_status"),  # setup_ui.cpp L622: size 22
    ("內建圖標（速度較快）", 22, "builtin_status"),     # setup_ui.cpp L624: size 22
    ("已連接",       22, "connected_22"),       # setup_ui.cpp main menu WiFi: size 22
    ("已儲存",       22, "saved_22"),           # setup_ui.cpp main menu WiFi: size 22
    ("已設定",       22, "configured_22"),      # setup_ui.cpp main menu timezone: size 22
    ("執行中",       22, "running_22"),         # setup_ui.cpp main menu web: size 22
    ("已啟用",       22, "enabled_22"),         # setup_ui.cpp main menu web: size 22
    ("壽星天文曆（許劍偉）", 22, "sxwnl_calendar"),     # setup_ui.cpp: menu card
    ("Meeus 天文算法（精度較高）", 22, "meeus_algorithm"),  # setup_ui.cpp: menu card

    # ── Calendar setup detail page (setup_ui.cpp) ──
    ("曆法計算",                   40, "calendar_title"),    # title
    ("壽星天文曆",                 32, "sxwnl_32"),          # status
    ("Meeus 天文算法",             32, "meeus_32"),          # status
    ("切換為 Meeus 天文算法",      36, "switch_meeus_btn"),  # toggle button
    ("切換為 壽星天文曆",          36, "switch_sxwnl_btn"),  # toggle button
    ("Meeus 天文算法",             28, "meeus_28"),          # info heading
    ("壽星天文曆",                 28, "sxwnl_28"),          # info heading
    ("• 精度約 1 分鐘",            24, "meeus_desc1"),       # info
    ("• 二分搜索求解太陽黃經",     24, "meeus_desc2"),       # info
    ("• 與公佈曆書完全一致",       24, "meeus_desc3"),       # info
    ("• 許劍偉開源算法",           24, "sxwnl_desc1"),      # info
    ("• 最大誤差約 30 分鐘",       24, "sxwnl_desc2"),      # info
    ("• 計算速度較快",             24, "sxwnl_desc3"),      # info
    ("已啟用 - 10分鐘無操作自動休眠", 22, "sleep_enabled"),    # setup_ui.cpp L737: size 22
    ("未啟用 - 保持開啟", 22, "sleep_disabled"),          # setup_ui.cpp L739: size 22

    # ── Icon setup (actual sizes from setup_ui.cpp) ──
    ("SD 卡優先",        32, "sd_priority_32"),     # setup_ui.cpp: status size 32
    ("內建圖標",         32, "builtin_icons_32"),   # setup_ui.cpp: status size 32
    ("切換為 內建圖標",  36, "switch_builtin_btn"), # setup_ui.cpp: toggle button
    ("切換為 SD 卡優先", 36, "switch_sd_btn"),      # setup_ui.cpp: toggle button
    ("SD 卡優先",        28, "sd_priority_28"),     # setup_ui.cpp: info section
    ("內建圖標",         28, "builtin_icons_28"),   # setup_ui.cpp: info section
    ("• 先從 /icons/ 讀取",        24, "sd_desc1"),       # setup_ui.cpp: info
    ("• 找不到則用內建圖標",       24, "sd_desc2"),       # setup_ui.cpp: info
    ("• 可自行更換圖標",           24, "sd_desc3"),       # setup_ui.cpp: info
    ("• 直接使用韌體內的圖標",     24, "builtin_desc1"),  # setup_ui.cpp: info
    ("• 啟動速度較快",             24, "builtin_desc2"),  # setup_ui.cpp: info

    # ── USB MSC (actual sizes from usb_msc_handler.cpp) ──
    ("USB 外接磁碟",               40, "usb_title"),         # usb_msc title
    ("執行中",                     32, "msc_running"),       # usb_msc status
    ("SD 卡已連接到電腦",          24, "sd_connected"),      # usb_msc info
    ("裝置無法存取 SD 卡",         24, "sd_unavailable"),    # usb_msc info
    ("關閉將重新啟動裝置",         24, "restart_warning"),   # usb_msc warning
    ("將整張 SD 卡作為 USB 磁碟",  24, "sd_as_usb"),        # usb_msc info
    ("關閉 USB 磁碟",              36, "msc_stop_btn"),      # usb_msc button
    ("啟用 USB 磁碟",              36, "msc_start_btn"),     # usb_msc button
    ("說明",                       32, "msc_help"),          # usb_msc info title
    ("完整 SD 卡存取",             28, "full_sd_access"),    # usb_msc info
    ("可讀寫所有檔案",             28, "rw_all_files"),      # usb_msc info
    ("關閉將重新啟動",             28, "close_restart"),     # usb_msc info

    # ── Wallpaper errors (wallpaper.cpp) ──
    ("找不到檔案",                 24, "file_not_found"),    # wallpaper.cpp error
    ("檔案過大",                   24, "file_too_large"),    # wallpaper.cpp error
    ("請使用小於4MB的圖片",        24, "use_smaller_img"),   # wallpaper.cpp error
    ("無法開啟檔案",               24, "cannot_open_file"),  # wallpaper.cpp error
    ("記憶體不足",                 24, "out_of_memory"),     # wallpaper.cpp error
    ("無法載入圖片",               24, "cannot_load_img"),   # wallpaper.cpp error
    ("GIMP 匯出設定:",             20, "gimp_export"),       # wallpaper.cpp help
    ("1. 使用「匯出為」",          20, "gimp_step1"),        # wallpaper.cpp help
    ("2. 取消勾選「漸進式」",      20, "gimp_step2"),        # wallpaper.cpp help
    ("3. 或改用 BMP 格式",         20, "gimp_step3"),        # wallpaper.cpp help

    # ── Sleep screen (main.cpp) ──
    ("休眠中",                     20, "sleeping"),          # main.cpp sleep label

    # ── Web server (actual sizes from setup_ui.cpp) ──
    ("檔案上傳伺服器",             40, "webserver_title"),   # setup_ui.cpp: title
    ("關閉伺服器",                 36, "stop_server_btn"),   # setup_ui.cpp: toggle button
    ("啟用伺服器",                 36, "start_server_btn"),  # setup_ui.cpp: toggle button
    ("正在啟動伺服器...",          28, "starting_server"),   # setup_ui.cpp: status
    ("等待 WiFi 連接...",          28, "waiting_wifi"),      # setup_ui.cpp: status
    ("在電腦或手機瀏覽器中輸入上方 IP 位址", 20, "enter_ip"),  # setup_ui.cpp: hint
    ("啟用後可通過網站上傳檔案",   24, "enable_upload_desc"), # setup_ui.cpp: info
    ("支援的檔案類型",             28, "supported_types"),    # setup_ui.cpp: info
    ("• 電子書 (books/)",          24, "srv_books"),         # setup_ui.cpp: info
    ("• 字體 (fonts/)",            24, "srv_fonts"),         # setup_ui.cpp: info
    ("• 壁紙 (wallpapers/)",       24, "srv_wallpapers"),   # setup_ui.cpp: info
    ("• 待辦/購物清單",            24, "srv_lists"),         # setup_ui.cpp: info

    # ── Loading / boot (actual sizes from main.cpp) ──
    ("重新啟動中...",  32, "restarting"),          # main.cpp L1724: size 32
    ("啟動 USB 中...", 32, "starting_usb"),        # main.cpp L1739: size 32
    ("請查看序列輸出以了解詳情", 22, "see_serial"),  # main.cpp L1740: size 22
    ("✓ 成功啟動",     28, "usb_ok"),               # main.cpp L1750: size 28
    ("✗ 啟動失敗 - 請查看序列輸出", 24, "usb_fail"), # main.cpp L1752: size 24
    ("找不到",         28, "not_found"),            # main.cpp L1044: size 28

    # ── Wallpaper picker title ──
    ("壁紙選擇",     36, "wallpaper_title"),    # wallpaper.cpp: title at top of picker

    # ── Tools menu and medication reminder ──
    ("工具",         36, "tools_title"),
    ("壁紙",         36, "tools_wallpaper"),
    ("吃藥提醒器",   36, "med_reminder"),
    ("吃藥提醒器",   36, "med_title"),         # title at top
    ("已吃藥",       48, "med_taken"),
    ("未吃藥",       48, "med_not_taken"),
    ("吃藥後請按此", 28, "med_press_hint"),
    ("點擊手動復位", 22, "med_manual_reset"),
    ("輸入密碼",     36, "med_passcode_title"),
    ("設定新密碼",   36, "med_set_new"),
    ("確認密碼",     36, "med_confirm"),
    ("請輸入新的數字密碼", 22, "med_set_hint"),
    ("請再輸入一次", 22, "med_confirm_hint"),
    ("密碼錯誤",     22, "med_passcode_wrong"),
    ("吃藥時按一下按鈕",     24, "med_instr1"),
    ("忘了是否吃過，看一眼就知道", 22, "med_instr2"),
    ("切換縮圖",     24, "switch_thumbnail"),   # wallpaper.cpp: toggle button
    ("切換列表",     24, "switch_list"),        # wallpaper.cpp: toggle button
    ("SD卡中沒有壁紙",       24, "no_wallpaper"),       # wallpaper.cpp: empty state
    ("請在 /wallpapers 資料夾中", 24, "add_wallpaper_hint"), # wallpaper.cpp: empty state
    ("添加圖片檔案", 24, "add_image_files"),    # wallpaper.cpp: empty state

    # ── Book reader (actual sizes from book_reader.cpp) ──
    ("電子書列表",   36, "booklist_title"),     # book_reader.cpp L370: size 36
    ("觸控選擇書籍", 18, "touch_select"),       # book_reader.cpp L386: size 18
    ("字-",          28, "font_smaller"),       # book_reader.cpp L591: default size 28
    ("字+",          28, "font_larger"),        # book_reader.cpp L601: default size 28
    ("字型",         20, "font_type"),          # book_reader.cpp L605: size 20

    # ── Loading (boot screen) ──
    ("載入中...",         36, "loading"),

    # ── Weather ──
    ("天氣 未設定",       36, "weather_not_set"),
    ("請在 SD 卡 config.ini 中設定", 24, "weather_config_hint"),
    ("連接 WiFi...",      36, "connecting_wifi"),
    ("載入天氣中...",     36, "loading_weather"),
    ("天氣載入失敗",      36, "weather_fail"),
    ("WiFi 未連線",       24, "wifi_noconn"),
    ("請檢查 API Key 與城市名稱", 24, "check_api"),
    ("未來預報",          28, "forecast"),
    ("API Key 申請：",    22, "api_key_label"),    # weather.cpp L367: size 22
    ("city 範例：",       22, "city_label"),       # weather.cpp L373: size 22
    ("units 選項：",      22, "units_label"),      # weather.cpp L380: size 22

    # ── Weather detail labels (size 26 for detail grid) ──
    ("體感",   26, "wd_feelslike"),
    ("最低",   26, "wd_min"),
    ("最高",   26, "wd_max"),
    ("濕度",   26, "wd_humidity"),
    ("風速",   26, "wd_wind"),
    ("氣壓",   26, "wd_pressure"),
    ("能見",   26, "wd_visibility"),
    ("日出",   26, "wd_sunrise"),
    ("日落",   26, "wd_sunset"),
    ("空氣",   26, "wd_air"),

    # ── Weather detail labels (size 24 for feels like) ──
    ("體感",   24, "wd_feelslike_24"),

    # ── Weather AQI labels (size 26) ──
    ("優",     26, "aqi_excellent"),
    ("良",     26, "aqi_good"),
    ("中等",   26, "aqi_moderate"),
    ("差",     26, "aqi_poor"),
    ("極差",   26, "aqi_verypoor"),

    # ── Weather descriptions (size 40 for main display, size 24 for forecast) ──
    ("晴天", 40, "w_sunny_40"),
    ("少雲", 40, "w_fewclouds_40"),
    ("疏雲", 40, "w_scatterclouds_40"),
    ("多雲", 40, "w_cloudy_40"),
    ("陰天", 40, "w_overcast_40"),
    ("毛毛雨", 40, "w_drizzle_40"),
    ("小雨", 40, "w_lightrain_40"),
    ("中雨", 40, "w_rain_40"),
    ("大雨", 40, "w_heavyrain_40"),
    ("雷雨", 40, "w_thunder_40"),
    ("下雪", 40, "w_snow_40"),
    ("薄霧", 40, "w_mist_40"),
    ("霧",   40, "w_fog_40"),
    ("霾",   40, "w_haze_40"),
    ("下雨", 40, "w_raining_40"),
    ("晴天", 24, "w_sunny"),
    ("少雲", 24, "w_fewclouds"),
    ("疏雲", 24, "w_scatterclouds"),
    ("多雲", 24, "w_cloudy"),
    ("陰天", 24, "w_overcast"),
    ("毛毛雨", 24, "w_drizzle"),
    ("小雨", 24, "w_lightrain"),
    ("中雨", 24, "w_rain"),
    ("大雨", 24, "w_heavyrain"),
    ("雷雨", 24, "w_thunder"),
    ("下雪", 24, "w_snow"),
    ("薄霧", 24, "w_mist"),
    ("霧",   24, "w_fog"),
    ("霾",   24, "w_haze"),
    ("下雨", 24, "w_raining"),

    # ── Calendar ──
    ("今天",         24, "today"),             # calendar.cpp L670: size 24
    ("無法取得時間", 36, "no_time"),           # calendar.cpp L706: size 36

    # ── Calendar year-month popup ──
    ("選擇年月",     32, "ym_title"),
    ("清",           28, "ym_clear"),
    ("刪",           28, "ym_del"),
    ("年",           28, "ym_nian28"),
    ("一月",   28, "ym_m01"),
    ("二月",   28, "ym_m02"),
    ("三月",   28, "ym_m03"),
    ("四月",   28, "ym_m04"),
    ("五月",   28, "ym_m05"),
    ("六月",   28, "ym_m06"),
    ("七月",   28, "ym_m07"),
    ("八月",   28, "ym_m08"),
    ("九月",   28, "ym_m09"),
    ("十月",   28, "ym_m10"),
    ("十一月", 28, "ym_m11"),
    ("十二月", 28, "ym_m12"),
    ("0", 28, "d28_0"),
    ("1", 28, "d28_1"),
    ("2", 28, "d28_2"),
    ("3", 28, "d28_3"),
    ("4", 28, "d28_4"),
    ("5", 28, "d28_5"),
    ("6", 28, "d28_6"),
    ("7", 28, "d28_7"),
    ("8", 28, "d28_8"),
    ("9", 28, "d28_9"),
    ("時辰吉凶",     24, "hourly_fortune_24"),
    ("吉",           16, "lucky_16"),
    ("凶",           16, "unlucky_16"),
    ("年柱：",       22, "year_pillar"),
    ("月柱：",       22, "month_pillar"),
    ("日柱：",       22, "day_pillar"),
    ("六曜：",       22, "rokuyo"),
    ("喜神：",       22, "joy_god"),
    ("福神：",       22, "fortune_god"),
    ("財神：",       22, "wealth_god"),
    ("胎神：",       22, "fetal_god"),
    ("彭祖：",       22, "pengzu"),
    ("農曆",         22, "lunar"),

    # ── Cangjie input ──
    ("倉頡輸入",     28, "cangjie_title"),     # cangjie_input.cpp L41: size 28

    # ── Font selection ──
    ("選擇閱讀字型 Reading Font", 24, "font_select_title"),  # setup_ui.cpp L30: size 24

    # ── Icon setup page title ──
    ("圖標來源",     40, "icon_source_title"),  # setup_ui.cpp L495: size 40

    # ── Calendar: 宜 activities (10 combinations, rendered at size 24) ──
    ("祈福 出行 納采 嫁娶", 24, "yi_0"),
    ("開市 交易 立券 納財", 24, "yi_1"),
    ("祭祀 祈福 求嗣 開光", 24, "yi_2"),
    ("嫁娶 納采 訂盟 祭祀", 24, "yi_3"),
    ("安葬 啟鑽 除服 成服", 24, "yi_4"),
    ("出行 教牛馬 豎柱 上樑", 24, "yi_5"),
    ("祈福 齋醮 出行 移徙", 24, "yi_6"),
    ("嫁娶 祭祀 開市 出行", 24, "yi_7"),
    ("祭祀 祈福 求嗣 齋醮", 24, "yi_8"),
    ("修造 動土 豎柱 上樑", 24, "yi_9"),

    # ── Calendar: 宜 activities (10 combinations, rendered at size 32) ──
    ("祈福 出行 納采 嫁娶", 32, "yi32_0"),
    ("開市 交易 立券 納財", 32, "yi32_1"),
    ("祭祀 祈福 求嗣 開光", 32, "yi32_2"),
    ("嫁娶 納采 訂盟 祭祀", 32, "yi32_3"),
    ("安葬 啟鑽 除服 成服", 32, "yi32_4"),
    ("出行 教牛馬 豎柱 上樑", 32, "yi32_5"),
    ("祈福 齋醮 出行 移徙", 32, "yi32_6"),
    ("嫁娶 祭祀 開市 出行", 32, "yi32_7"),
    ("祭祀 祈福 求嗣 齋醮", 32, "yi32_8"),
    ("修造 動土 豎柱 上樑", 32, "yi32_9"),

    # ── Calendar: 忌 activities (10 combinations, rendered at size 24) ──
    ("開市 動土 破土 安葬", 24, "ji_0"),
    ("嫁娶 安葬 出行 動土", 24, "ji_1"),
    ("移徙 入宅 安門 作灶", 24, "ji_2"),
    ("開倉 出貨 安葬 破土", 24, "ji_3"),
    ("嫁娶 開市 入宅 移徙", 24, "ji_4"),
    ("祈福 嫁娶 安葬 破土", 24, "ji_5"),
    ("安葬 破土 動土 開市", 24, "ji_6"),
    ("破土 安葬 開倉 出貨", 24, "ji_7"),
    ("動土 破土 安葬 開市", 24, "ji_8"),
    ("嫁娶 入宅 移徙 出行", 24, "ji_9"),

    # ── Calendar: 忌 activities (10 combinations, rendered at size 32) ──
    ("開市 動土 破土 安葬", 32, "ji32_0"),
    ("嫁娶 安葬 出行 動土", 32, "ji32_1"),
    ("移徙 入宅 安門 作灶", 32, "ji32_2"),
    ("開倉 出貨 安葬 破土", 32, "ji32_3"),
    ("嫁娶 開市 入宅 移徙", 32, "ji32_4"),
    ("祈福 嫁娶 安葬 破土", 32, "ji32_5"),
    ("安葬 破土 動土 開市", 32, "ji32_6"),
    ("破土 安葬 開倉 出貨", 32, "ji32_7"),
    ("動土 破土 安葬 開市", 32, "ji32_8"),
    ("嫁娶 入宅 移徙 出行", 32, "ji32_9"),

    # ── Calendar: 六曜 (rendered at size 22) ──
    ("大安", 22, "liuyao_0"),
    ("留連", 22, "liuyao_1"),
    ("速喜", 22, "liuyao_2"),
    ("赤口", 22, "liuyao_3"),
    ("小吉", 22, "liuyao_4"),
    ("空亡", 22, "liuyao_5"),

    # ── Calendar: 節氣 Solar Terms (size 28, standalone on calendar detail) ──
    ("小寒", 28, "st28_xiaohan"),
    ("大寒", 28, "st28_dahan"),
    ("立春", 28, "st28_lichun"),
    ("雨水", 28, "st28_yushui"),
    ("驚蟄", 28, "st28_jingzhe"),
    ("春分", 28, "st28_chunfen"),
    ("清明", 28, "st28_qingming"),
    ("穀雨", 28, "st28_guyu"),
    ("立夏", 28, "st28_lixia"),
    ("小滿", 28, "st28_xiaoman"),
    ("芒種", 28, "st28_mangzhong"),
    ("夏至", 28, "st28_xiazhi"),
    ("小暑", 28, "st28_xiaoshu"),
    ("大暑", 28, "st28_dashu"),
    ("立秋", 28, "st28_liqiu"),
    ("處暑", 28, "st28_chushu"),
    ("白露", 28, "st28_bailu"),
    ("秋分", 28, "st28_qiufen"),
    ("寒露", 28, "st28_hanlu"),
    ("霜降", 28, "st28_shuangjian"),
    ("立冬", 28, "st28_lidong"),
    ("小雪", 28, "st28_xiaoxue"),
    ("大雪", 28, "st28_daxue"),
    ("冬至", 28, "st28_dongzhi"),

    # ── Calendar: 節氣 Solar Terms (size 34, enlarged display beside big day) ──
    ("小寒", 34, "st34_xiaohan"),
    ("大寒", 34, "st34_dahan"),
    ("立春", 34, "st34_lichun"),
    ("雨水", 34, "st34_yushui"),
    ("驚蟄", 34, "st34_jingzhe"),
    ("春分", 34, "st34_chunfen"),
    ("清明", 34, "st34_qingming"),
    ("穀雨", 34, "st34_guyu"),
    ("立夏", 34, "st34_lixia"),
    ("小滿", 34, "st34_xiaoman"),
    ("芒種", 34, "st34_mangzhong"),
    ("夏至", 34, "st34_xiazhi"),
    ("小暑", 34, "st34_xiaoshu"),
    ("大暑", 34, "st34_dashu"),
    ("立秋", 34, "st34_liqiu"),
    ("處暑", 34, "st34_chushu"),
    ("白露", 34, "st34_bailu"),
    ("秋分", 34, "st34_qiufen"),
    ("寒露", 34, "st34_hanlu"),
    ("霜降", 34, "st34_shuangjian"),
    ("立冬", 34, "st34_lidong"),
    ("小雪", 34, "st34_xiaoxue"),
    ("大雪", 34, "st34_daxue"),
    ("冬至", 34, "st34_dongzhi"),

    # ── Calendar: 六十甲子 at size 26 (for 農曆 info line GanZhi) ──
    ("甲子", 26, "gz26_00"), ("乙丑", 26, "gz26_01"), ("丙寅", 26, "gz26_02"),
    ("丁卯", 26, "gz26_03"), ("戊辰", 26, "gz26_04"), ("己巳", 26, "gz26_05"),
    ("庚午", 26, "gz26_06"), ("辛未", 26, "gz26_07"), ("壬申", 26, "gz26_08"),
    ("癸酉", 26, "gz26_09"), ("甲戌", 26, "gz26_10"), ("乙亥", 26, "gz26_11"),
    ("丙子", 26, "gz26_12"), ("丁丑", 26, "gz26_13"), ("戊寅", 26, "gz26_14"),
    ("己卯", 26, "gz26_15"), ("庚辰", 26, "gz26_16"), ("辛巳", 26, "gz26_17"),
    ("壬午", 26, "gz26_18"), ("癸未", 26, "gz26_19"), ("甲申", 26, "gz26_20"),
    ("乙酉", 26, "gz26_21"), ("丙戌", 26, "gz26_22"), ("丁亥", 26, "gz26_23"),
    ("戊子", 26, "gz26_24"), ("己丑", 26, "gz26_25"), ("庚寅", 26, "gz26_26"),
    ("辛卯", 26, "gz26_27"), ("壬辰", 26, "gz26_28"), ("癸巳", 26, "gz26_29"),
    ("甲午", 26, "gz26_30"), ("乙未", 26, "gz26_31"), ("丙申", 26, "gz26_32"),
    ("丁酉", 26, "gz26_33"), ("戊戌", 26, "gz26_34"), ("己亥", 26, "gz26_35"),
    ("庚子", 26, "gz26_36"), ("辛丑", 26, "gz26_37"), ("壬寅", 26, "gz26_38"),
    ("癸卯", 26, "gz26_39"), ("甲辰", 26, "gz26_40"), ("乙巳", 26, "gz26_41"),
    ("丙午", 26, "gz26_42"), ("丁未", 26, "gz26_43"), ("戊申", 26, "gz26_44"),
    ("己酉", 26, "gz26_45"), ("庚戌", 26, "gz26_46"), ("辛亥", 26, "gz26_47"),
    ("壬子", 26, "gz26_48"), ("癸丑", 26, "gz26_49"), ("甲寅", 26, "gz26_50"),
    ("乙卯", 26, "gz26_51"), ("丙辰", 26, "gz26_52"), ("丁巳", 26, "gz26_53"),
    ("戊午", 26, "gz26_54"), ("己未", 26, "gz26_55"), ("庚申", 26, "gz26_56"),
    ("辛酉", 26, "gz26_57"), ("壬戌", 26, "gz26_58"), ("癸亥", 26, "gz26_59"),

    # ── Calendar: 沖煞 (rendered at size 22) ──
    ("沖馬", 22, "clash_horse"),
    ("沖羊", 22, "clash_goat"),
    ("沖猴", 22, "clash_monkey"),
    ("沖雞", 22, "clash_rooster"),
    ("沖狗", 22, "clash_dog"),
    ("沖豬", 22, "clash_pig"),
    ("沖鼠", 22, "clash_rat"),
    ("沖牛", 22, "clash_ox"),
    ("沖虎", 22, "clash_tiger"),
    ("沖兔", 22, "clash_rabbit"),
    ("沖龍", 22, "clash_dragon"),
    ("沖蛇", 22, "clash_snake"),

    # ── Calendar: 煞方 (rendered at size 22) ──
    ("煞南", 22, "sha_south"),
    ("煞西", 22, "sha_west"),
    ("煞東", 22, "sha_east"),
    ("煞北", 22, "sha_north"),

    # ── Calendar: 方位 directions (rendered at size 22) ──
    ("東北", 22, "dir_ne"),
    ("西北", 22, "dir_nw"),
    ("西南", 22, "dir_sw"),
    ("正南", 22, "dir_s"),
    ("東南", 22, "dir_se"),
    ("正北", 22, "dir_n"),
    ("正東", 22, "dir_e"),
    ("正西", 22, "dir_w"),

    # ── Calendar: 納音五行 (rendered at size 22) ──
    ("海中金", 22, "ny_01"),
    ("爐中火", 22, "ny_02"),
    ("大林木", 22, "ny_03"),
    ("路旁土", 22, "ny_04"),
    ("劍鋒金", 22, "ny_05"),
    ("山頭火", 22, "ny_06"),
    ("澗下水", 22, "ny_07"),
    ("城頭土", 22, "ny_08"),
    ("白蠟金", 22, "ny_09"),
    ("楊柳木", 22, "ny_10"),
    ("泉中水", 22, "ny_11"),
    ("屋上土", 22, "ny_12"),
    ("霹靂火", 22, "ny_13"),
    ("松柏木", 22, "ny_14"),
    ("長流水", 22, "ny_15"),
    ("沙中金", 22, "ny_16"),
    ("山下火", 22, "ny_17"),
    ("平地木", 22, "ny_18"),
    ("壁上土", 22, "ny_19"),
    ("金箔金", 22, "ny_20"),
    ("覆燈火", 22, "ny_21"),
    ("天河水", 22, "ny_22"),
    ("大驛土", 22, "ny_23"),
    ("釵釧金", 22, "ny_24"),
    ("桑拓木", 22, "ny_25"),
    ("大溪水", 22, "ny_26"),
    ("沙中土", 22, "ny_27"),
    ("天上火", 22, "ny_28"),
    ("石榴木", 22, "ny_29"),
    ("大海水", 22, "ny_30"),

    # ── Calendar: 彭祖百忌 天干 (rendered at size 22) ──
    ("甲不開倉", 22, "pz_jia"),
    ("乙不栽植", 22, "pz_yi"),
    ("丙不修灶", 22, "pz_bing"),
    ("丁不剃頭", 22, "pz_ding"),
    ("戊不受田", 22, "pz_wu"),
    ("己不破券", 22, "pz_ji"),
    ("庚不經絡", 22, "pz_geng"),
    ("辛不合醬", 22, "pz_xin"),
    ("壬不汲水", 22, "pz_ren"),
    ("癸不詞訟", 22, "pz_gui"),

    # ── Calendar: 彭祖百忌 地支 (rendered at size 22) ──
    ("子不問卜", 22, "pz_zi"),
    ("丑不冠帶", 22, "pz_chou"),
    ("寅不祭祀", 22, "pz_yin"),
    ("卯不穿井", 22, "pz_mao"),
    ("辰不哭泣", 22, "pz_chen"),
    ("巳不遠行", 22, "pz_si"),
    ("午不苗蓋", 22, "pz_wuma"),
    ("未不服藥", 22, "pz_wei"),
    ("申不安床", 22, "pz_shen"),
    ("酉不會客", 22, "pz_you"),
    ("戌不吃犬", 22, "pz_xu"),
    ("亥不嫁娶", 22, "pz_hai"),

    # ── Calendar: 胎神 (rendered at size 22) ──
    ("佔門碓外東南", 22, "ts_0"),
    ("碓磨廁外東南", 22, "ts_1"),
    ("廚灶爐外正南", 22, "ts_2"),
    ("倉庫門外正南", 22, "ts_3"),
    ("房床棲外正南", 22, "ts_4"),
    ("佔門床外正南", 22, "ts_5"),
    ("佔碓磨外西南", 22, "ts_6"),
    ("廚灶廁外西南", 22, "ts_7"),
    ("倉庫爐外正西", 22, "ts_8"),
    ("房床門外正西", 22, "ts_9"),

    # ── Calendar: 宜/忌 headings (rendered at size 34, white on black) ──
    ("宜", 34, "yi_heading"),
    ("忌", 34, "ji_heading"),

    # ── Calendar: 天干 at size 22 (GanZhi in 年柱/月柱/日柱) ──
    ("甲", 22, "tg_jia"),
    ("乙", 22, "tg_yi"),
    ("丙", 22, "tg_bing"),
    ("丁", 22, "tg_ding"),
    ("戊", 22, "tg_wu"),
    ("己", 22, "tg_ji"),
    ("庚", 22, "tg_geng"),
    ("辛", 22, "tg_xin"),
    ("壬", 22, "tg_ren"),
    ("癸", 22, "tg_gui"),

    # ── Calendar: 地支 at size 22 (GanZhi in 年柱/月柱/日柱) ──
    ("子", 22, "dz22_zi"),
    ("丑", 22, "dz22_chou"),
    ("寅", 22, "dz22_yin"),
    ("卯", 22, "dz22_mao"),
    ("辰", 22, "dz22_chen"),
    ("巳", 22, "dz22_si"),
    ("午", 22, "dz22_wu"),
    ("未", 22, "dz22_wei"),
    ("申", 22, "dz22_shen"),
    ("酉", 22, "dz22_you"),
    ("戌", 22, "dz22_xu"),
    ("亥", 22, "dz22_hai"),

    # ── Calendar: 地支 at size 18 (hourly grid) ──
    ("子", 18, "dz_zi"),
    ("丑", 18, "dz_chou"),
    ("寅", 18, "dz_yin"),
    ("卯", 18, "dz_mao"),
    ("辰", 18, "dz_chen"),
    ("巳", 18, "dz_si"),
    ("午", 18, "dz_wu"),
    ("未", 18, "dz_wei"),
    ("申", 18, "dz_shen"),
    ("酉", 18, "dz_you"),
    ("戌", 18, "dz_xu"),
    ("亥", 18, "dz_hai"),

    # ── Calendar: Festival category labels (size 20, detail page) ──
    ("民俗", 20, "fest_cat_folk"),
    ("道教", 20, "fest_cat_taoist"),
    ("佛教", 20, "fest_cat_buddhist"),
    ("西方", 20, "fest_cat_western"),
    ("個人", 20, "fest_cat_custom"),

    # ── Calendar: 民俗節日 (size 20, detail page) ──
    ("春節",   20, "fest_chunjie"),
    ("破五節", 20, "fest_powu"),
    ("元宵節", 20, "fest_yuanxiao"),
    ("龍抬頭", 20, "fest_longtaitou"),
    ("上巳節", 20, "fest_shangsi"),
    ("端午節", 20, "fest_duanwu"),
    ("七夕",   20, "fest_qixi"),
    ("中元節", 20, "fest_zhongyuan"),
    ("中秋節", 20, "fest_zhongqiu"),
    ("重陽節", 20, "fest_chongyang"),
    ("寒衣節", 20, "fest_hanyi"),
    ("臘八節", 20, "fest_laba"),
    ("小年",   20, "fest_xiaonian"),
    ("除夕",   20, "fest_chuxi"),

    # ── Calendar: 道教節日 (size 20, detail page) ──
    ("玉皇誕",     20, "fest_yuhuang"),
    ("上元天官誕", 20, "fest_shangyuan"),
    ("福德正神誕", 20, "fest_fude"),
    ("文昌帝君誕", 20, "fest_wenchang"),
    ("太上老君誕", 20, "fest_taishang"),
    ("玄天上帝誕", 20, "fest_xuantian"),
    ("保生大帝誕", 20, "fest_baosheng"),
    ("天上聖母誕", 20, "fest_mazu"),
    ("呂祖誕",     20, "fest_lvzu"),
    ("神農大帝誕", 20, "fest_shennong"),
    ("關聖帝君誕", 20, "fest_guansheng"),
    ("關帝誕",     20, "fest_guandi"),
    ("中元地官誕", 20, "fest_zhongyuandg"),
    ("灶君誕",     20, "fest_zaojun"),
    ("中壇元帥誕", 20, "fest_zhongtan"),
    ("下元水官誕", 20, "fest_xiayuan"),

    # ── Calendar: 佛教節日 (size 20, detail page) ──
    ("彌勒菩薩誕",   20, "fest_mile"),
    ("釋迦出家日",   20, "fest_chujia"),
    ("釋迦涅槃日",   20, "fest_niepan"),
    ("觀音菩薩誕",   20, "fest_guanyin"),
    ("準提菩薩誕",   20, "fest_zhunti"),
    ("文殊菩薩誕",   20, "fest_wenshu"),
    ("佛誕日",       20, "fest_fodan"),
    ("韋馱菩薩誕",   20, "fest_weituo"),
    ("觀音成道日",   20, "fest_guanyincd"),
    ("大勢至菩薩誕", 20, "fest_dashizhi"),
    ("盂蘭盆節",     20, "fest_yulanpen"),
    ("地藏菩薩誕",   20, "fest_dizang"),
    ("觀音出家日",   20, "fest_guanyincj"),
    ("藥師佛誕",     20, "fest_yaoshi"),
    ("阿彌陀佛誕",   20, "fest_amituo"),
    ("釋迦成道日",   20, "fest_chengdao"),

    # ── Calendar: Festival grid names (size 18, calendar picker) ──
    ("春節",   18, "gf_chunjie"),
    ("元宵",   18, "gf_yuanxiao"),
    ("龍抬頭", 18, "gf_longtaitou"),
    ("端午",   18, "gf_duanwu"),
    ("七夕",   18, "gf_qixi"),
    ("中元",   18, "gf_zhongyuan"),
    ("中秋",   18, "gf_zhongqiu"),
    ("重陽",   18, "gf_chongyang"),
    ("臘八",   18, "gf_laba"),
    ("小年",   18, "gf_xiaonian"),
    ("除夕",   18, "gf_chuxi"),

    # ── Calendar: Lunar phase markers (朔=new moon day 1, 望=full moon day 15) ──
    ("朔",     30, "shuo"),
    ("望",     30, "wang"),
    ("朔",     16, "shuo_16"),
    ("望",     16, "wang_16"),

    # ── Calendar: 六十甲子 (60 GanZhi pairs at size 22, for 年柱/月柱/日柱) ──
    ("甲子", 22, "gz_00"),
    ("乙丑", 22, "gz_01"),
    ("丙寅", 22, "gz_02"),
    ("丁卯", 22, "gz_03"),
    ("戊辰", 22, "gz_04"),
    ("己巳", 22, "gz_05"),
    ("庚午", 22, "gz_06"),
    ("辛未", 22, "gz_07"),
    ("壬申", 22, "gz_08"),
    ("癸酉", 22, "gz_09"),
    ("甲戌", 22, "gz_10"),
    ("乙亥", 22, "gz_11"),
    ("丙子", 22, "gz_12"),
    ("丁丑", 22, "gz_13"),
    ("戊寅", 22, "gz_14"),
    ("己卯", 22, "gz_15"),
    ("庚辰", 22, "gz_16"),
    ("辛巳", 22, "gz_17"),
    ("壬午", 22, "gz_18"),
    ("癸未", 22, "gz_19"),
    ("甲申", 22, "gz_20"),
    ("乙酉", 22, "gz_21"),
    ("丙戌", 22, "gz_22"),
    ("丁亥", 22, "gz_23"),
    ("戊子", 22, "gz_24"),
    ("己丑", 22, "gz_25"),
    ("庚寅", 22, "gz_26"),
    ("辛卯", 22, "gz_27"),
    ("壬辰", 22, "gz_28"),
    ("癸巳", 22, "gz_29"),
    ("甲午", 22, "gz_30"),
    ("乙未", 22, "gz_31"),
    ("丙申", 22, "gz_32"),
    ("丁酉", 22, "gz_33"),
    ("戊戌", 22, "gz_34"),
    ("己亥", 22, "gz_35"),
    ("庚子", 22, "gz_36"),
    ("辛丑", 22, "gz_37"),
    ("壬寅", 22, "gz_38"),
    ("癸卯", 22, "gz_39"),
    ("甲辰", 22, "gz_40"),
    ("乙巳", 22, "gz_41"),
    ("丙午", 22, "gz_42"),
    ("丁未", 22, "gz_43"),
    ("戊申", 22, "gz_44"),
    ("己酉", 22, "gz_45"),
    ("庚戌", 22, "gz_46"),
    ("辛亥", 22, "gz_47"),
    ("壬子", 22, "gz_48"),
    ("癸丑", 22, "gz_49"),
    ("甲寅", 22, "gz_50"),
    ("乙卯", 22, "gz_51"),
    ("丙辰", 22, "gz_52"),
    ("丁巳", 22, "gz_53"),
    ("戊午", 22, "gz_54"),
    ("己未", 22, "gz_55"),
    ("庚申", 22, "gz_56"),
    ("辛酉", 22, "gz_57"),
    ("壬戌", 22, "gz_58"),
    ("癸亥", 22, "gz_59"),

    # ── Calendar: 農曆日名 (30 lunar day names at size 18, for calendar grid) ──
    ("初一", 18, "ld_01"),
    ("初二", 18, "ld_02"),
    ("初三", 18, "ld_03"),
    ("初四", 18, "ld_04"),
    ("初五", 18, "ld_05"),
    ("初六", 18, "ld_06"),
    ("初七", 18, "ld_07"),
    ("初八", 18, "ld_08"),
    ("初九", 18, "ld_09"),
    ("初十", 18, "ld_10"),
    ("十一", 18, "ld_11"),
    ("十二", 18, "ld_12"),
    ("十三", 18, "ld_13"),
    ("十四", 18, "ld_14"),
    ("十五", 18, "ld_15"),
    ("十六", 18, "ld_16"),
    ("十七", 18, "ld_17"),
    ("十八", 18, "ld_18"),
    ("十九", 18, "ld_19"),
    ("二十", 18, "ld_20"),
    ("廿一", 18, "ld_21"),
    ("廿二", 18, "ld_22"),
    ("廿三", 18, "ld_23"),
    ("廿四", 18, "ld_24"),
    ("廿五", 18, "ld_25"),
    ("廿六", 18, "ld_26"),
    ("廿七", 18, "ld_27"),
    ("廿八", 18, "ld_28"),
    ("廿九", 18, "ld_29"),
    ("三十", 18, "ld_30"),

    # ── Calendar picker: weekday headers (size 28) ──
    ("日", 28, "wh_sun"),
    ("一", 28, "wh_mon"),
    ("二", 28, "wh_tue"),
    ("三", 28, "wh_wed"),
    ("四", 28, "wh_thu"),
    ("五", 28, "wh_fri"),
    ("六", 28, "wh_sat"),

    # ── Calendar picker: solar day numbers 1-31 (size 32) ──
    ("1",  32, "sdn_1"),
    ("2",  32, "sdn_2"),
    ("3",  32, "sdn_3"),
    ("4",  32, "sdn_4"),
    ("5",  32, "sdn_5"),
    ("6",  32, "sdn_6"),
    ("7",  32, "sdn_7"),
    ("8",  32, "sdn_8"),
    ("9",  32, "sdn_9"),
    ("10", 32, "sdn_10"),
    ("11", 32, "sdn_11"),
    ("12", 32, "sdn_12"),
    ("13", 32, "sdn_13"),
    ("14", 32, "sdn_14"),
    ("15", 32, "sdn_15"),
    ("16", 32, "sdn_16"),
    ("17", 32, "sdn_17"),
    ("18", 32, "sdn_18"),
    ("19", 32, "sdn_19"),
    ("20", 32, "sdn_20"),
    ("21", 32, "sdn_21"),
    ("22", 32, "sdn_22"),
    ("23", 32, "sdn_23"),
    ("24", 32, "sdn_24"),
    ("25", 32, "sdn_25"),
    ("26", 32, "sdn_26"),
    ("27", 32, "sdn_27"),
    ("28", 32, "sdn_28"),
    ("29", 32, "sdn_29"),
    ("30", 32, "sdn_30"),
    ("31", 32, "sdn_31"),

    # ── Calendar detail: digit "0" at size 32 (for year display) ──
    ("0",  32, "d32_0"),

    # ── Calendar picker title: digits 0-9 + 年/月 at size 36 ──
    ("0", 36, "d36_0"),
    ("1", 36, "d36_1"),
    ("2", 36, "d36_2"),
    ("3", 36, "d36_3"),
    ("4", 36, "d36_4"),
    ("5", 36, "d36_5"),
    ("6", 36, "d36_6"),
    ("7", 36, "d36_7"),
    ("8", 36, "d36_8"),
    ("9", 36, "d36_9"),
    ("年", 36, "nian36"),
    ("月", 36, "yue36"),

    # ── Calendar detail: large day digits 0-9 (size 160) ──
    ("0", 160, "d160_0"),
    ("1", 160, "d160_1"),
    ("2", 160, "d160_2"),
    ("3", 160, "d160_3"),
    ("4", 160, "d160_4"),
    ("5", 160, "d160_5"),
    ("6", 160, "d160_6"),
    ("7", 160, "d160_7"),
    ("8", 160, "d160_8"),
    ("9", 160, "d160_9"),

    # ── Calendar picker: lunar month names (size 18) ──
    ("正月", 18, "lm18_01"),
    ("二月", 18, "lm18_02"),
    ("三月", 18, "lm18_03"),
    ("四月", 18, "lm18_04"),
    ("五月", 18, "lm18_05"),
    ("六月", 18, "lm18_06"),
    ("七月", 18, "lm18_07"),
    ("八月", 18, "lm18_08"),
    ("九月", 18, "lm18_09"),
    ("十月", 18, "lm18_10"),
    ("冬月", 18, "lm18_11"),
    ("臘月", 18, "lm18_12"),

    # ── Calendar picker: solar terms (size 16) ──
    ("小寒", 16, "st16_xiaohan"),
    ("大寒", 16, "st16_dahan"),
    ("立春", 16, "st16_lichun"),
    ("雨水", 16, "st16_yushui"),
    ("驚蟄", 16, "st16_jingzhe"),
    ("春分", 16, "st16_chunfen"),
    ("清明", 16, "st16_qingming"),
    ("穀雨", 16, "st16_guyu"),
    ("立夏", 16, "st16_lixia"),
    ("小滿", 16, "st16_xiaoman"),
    ("芒種", 16, "st16_mangzhong"),
    ("夏至", 16, "st16_xiazhi"),
    ("小暑", 16, "st16_xiaoshu"),
    ("大暑", 16, "st16_dashu"),
    ("立秋", 16, "st16_liqiu"),
    ("處暑", 16, "st16_chushu"),
    ("白露", 16, "st16_bailu"),
    ("秋分", 16, "st16_qiufen"),
    ("寒露", 16, "st16_hanlu"),
    ("霜降", 16, "st16_shuangjian"),
    ("立冬", 16, "st16_lidong"),
    ("小雪", 16, "st16_xiaoxue"),
    ("大雪", 16, "st16_daxue"),
    ("冬至", 16, "st16_dongzhi"),

    # ── Calendar detail: solar month names (size 26) ──
    ("一月",   26, "sm26_01"),
    ("二月",   26, "sm26_02"),
    ("三月",   26, "sm26_03"),
    ("四月",   26, "sm26_04"),
    ("五月",   26, "sm26_05"),
    ("六月",   26, "sm26_06"),
    ("七月",   26, "sm26_07"),
    ("八月",   26, "sm26_08"),
    ("九月",   26, "sm26_09"),
    ("十月",   26, "sm26_10"),
    ("十一月", 26, "sm26_11"),
    ("十二月", 26, "sm26_12"),

    # ── Calendar detail: 天干 at size 24 (year GanZhi display) ──
    ("甲", 24, "tg24_jia"),
    ("乙", 24, "tg24_yi"),
    ("丙", 24, "tg24_bing"),
    ("丁", 24, "tg24_ding"),
    ("戊", 24, "tg24_wu"),
    ("己", 24, "tg24_ji"),
    ("庚", 24, "tg24_geng"),
    ("辛", 24, "tg24_xin"),
    ("壬", 24, "tg24_ren"),
    ("癸", 24, "tg24_gui"),

    # ── Calendar detail: 地支 at size 24 ──
    ("子", 24, "dz24_zi"),
    ("丑", 24, "dz24_chou"),
    ("寅", 24, "dz24_yin"),
    ("卯", 24, "dz24_mao"),
    ("辰", 24, "dz24_chen"),
    ("巳", 24, "dz24_si"),
    ("午", 24, "dz24_wu"),
    ("未", 24, "dz24_wei"),
    ("申", 24, "dz24_shen"),
    ("酉", 24, "dz24_you"),
    ("戌", 24, "dz24_xu"),
    ("亥", 24, "dz24_hai"),

    # ── Calendar detail: "年" at size 24 ──
    ("年", 24, "nian24"),

    # ── Calendar detail: 天干 at size 34 (enlarged year GanZhi beside big day) ──
    ("甲", 34, "tg34_jia"),
    ("乙", 34, "tg34_yi"),
    ("丙", 34, "tg34_bing"),
    ("丁", 34, "tg34_ding"),
    ("戊", 34, "tg34_wu"),
    ("己", 34, "tg34_ji"),
    ("庚", 34, "tg34_geng"),
    ("辛", 34, "tg34_xin"),
    ("壬", 34, "tg34_ren"),
    ("癸", 34, "tg34_gui"),

    # ── Calendar detail: 地支 at size 34 ──
    ("子", 34, "dz34_zi"),
    ("丑", 34, "dz34_chou"),
    ("寅", 34, "dz34_yin"),
    ("卯", 34, "dz34_mao"),
    ("辰", 34, "dz34_chen"),
    ("巳", 34, "dz34_si"),
    ("午", 34, "dz34_wu"),
    ("未", 34, "dz34_wei"),
    ("申", 34, "dz34_shen"),
    ("酉", 34, "dz34_you"),
    ("戌", 34, "dz34_xu"),
    ("亥", 34, "dz34_hai"),

    # ── Calendar detail: "年" at size 34 ──
    ("年", 34, "nian34"),

    # ── Calendar detail: zodiac animals at size 34 ──
    ("鼠", 34, "sx34_rat"),
    ("牛", 34, "sx34_ox"),
    ("虎", 34, "sx34_tiger"),
    ("兔", 34, "sx34_rabbit"),
    ("龍", 34, "sx34_dragon"),
    ("蛇", 34, "sx34_snake"),
    ("馬", 34, "sx34_horse"),
    ("羊", 34, "sx34_goat"),
    ("猴", 34, "sx34_monkey"),
    ("雞", 34, "sx34_rooster"),
    ("狗", 34, "sx34_dog"),
    ("豬", 34, "sx34_pig"),

    # ── Calendar detail: 天干 at size 40 (enlarged year GanZhi beside big day) ──
    ("甲", 40, "tg40_jia"),  ("乙", 40, "tg40_yi"),   ("丙", 40, "tg40_bing"),
    ("丁", 40, "tg40_ding"), ("戊", 40, "tg40_wu"),   ("己", 40, "tg40_ji"),
    ("庚", 40, "tg40_geng"), ("辛", 40, "tg40_xin"),  ("壬", 40, "tg40_ren"),
    ("癸", 40, "tg40_gui"),
    # ── Calendar detail: 地支 at size 40 ──
    ("子", 40, "dz40_zi"),   ("丑", 40, "dz40_chou"), ("寅", 40, "dz40_yin"),
    ("卯", 40, "dz40_mao"),  ("辰", 40, "dz40_chen"), ("巳", 40, "dz40_si"),
    ("午", 40, "dz40_wu"),   ("未", 40, "dz40_wei"),  ("申", 40, "dz40_shen"),
    ("酉", 40, "dz40_you"),  ("戌", 40, "dz40_xu"),   ("亥", 40, "dz40_hai"),
    # ── Calendar detail: "年" at size 40 ──
    ("年", 40, "nian40"),
    # ── Calendar detail: zodiac at size 40 ──
    ("鼠", 40, "sx40_rat"),   ("牛", 40, "sx40_ox"),      ("虎", 40, "sx40_tiger"),
    ("兔", 40, "sx40_rabbit"),("龍", 40, "sx40_dragon"),  ("蛇", 40, "sx40_snake"),
    ("馬", 40, "sx40_horse"), ("羊", 40, "sx40_goat"),    ("猴", 40, "sx40_monkey"),
    ("雞", 40, "sx40_rooster"),("狗", 40, "sx40_dog"),    ("豬", 40, "sx40_pig"),

    # ── Calendar detail: zodiac animals at size 24 ──
    ("鼠", 24, "sx24_rat"),
    ("牛", 24, "sx24_ox"),
    ("虎", 24, "sx24_tiger"),
    ("兔", 24, "sx24_rabbit"),
    ("龍", 24, "sx24_dragon"),
    ("蛇", 24, "sx24_snake"),
    ("馬", 24, "sx24_horse"),
    ("羊", 24, "sx24_goat"),
    ("猴", 24, "sx24_monkey"),
    ("雞", 24, "sx24_rooster"),
    ("狗", 24, "sx24_dog"),
    ("豬", 24, "sx24_pig"),

    # ── Calendar detail: lunar date banner (size 30) ──
    ("閏",   30, "run30"),
    ("正月", 30, "lm30_01"),
    ("二月", 30, "lm30_02"),
    ("三月", 30, "lm30_03"),
    ("四月", 30, "lm30_04"),
    ("五月", 30, "lm30_05"),
    ("六月", 30, "lm30_06"),
    ("七月", 30, "lm30_07"),
    ("八月", 30, "lm30_08"),
    ("九月", 30, "lm30_09"),
    ("十月", 30, "lm30_10"),
    ("冬月", 30, "lm30_11"),
    ("臘月", 30, "lm30_12"),
    ("初一", 30, "ld30_01"),
    ("初二", 30, "ld30_02"),
    ("初三", 30, "ld30_03"),
    ("初四", 30, "ld30_04"),
    ("初五", 30, "ld30_05"),
    ("初六", 30, "ld30_06"),
    ("初七", 30, "ld30_07"),
    ("初八", 30, "ld30_08"),
    ("初九", 30, "ld30_09"),
    ("初十", 30, "ld30_10"),
    ("十一", 30, "ld30_11"),
    ("十二", 30, "ld30_12"),
    ("十三", 30, "ld30_13"),
    ("十四", 30, "ld30_14"),
    ("十五", 30, "ld30_15"),
    ("十六", 30, "ld30_16"),
    ("十七", 30, "ld30_17"),
    ("十八", 30, "ld30_18"),
    ("十九", 30, "ld30_19"),
    ("二十", 30, "ld30_20"),
    ("廿一", 30, "ld30_21"),
    ("廿二", 30, "ld30_22"),
    ("廿三", 30, "ld30_23"),
    ("廿四", 30, "ld30_24"),
    ("廿五", 30, "ld30_25"),
    ("廿六", 30, "ld30_26"),
    ("廿七", 30, "ld30_27"),
    ("廿八", 30, "ld30_28"),
    ("廿九", 30, "ld30_29"),
    ("三十", 30, "ld30_30"),

    # ── Calendar detail: 星期X at size 30 ──
    ("星期日", 30, "xq_sun"),
    ("星期一", 30, "xq_mon"),
    ("星期二", 30, "xq_tue"),
    ("星期三", 30, "xq_wed"),
    ("星期四", 30, "xq_thu"),
    ("星期五", 30, "xq_fri"),
    ("星期六", 30, "xq_sat"),

    # ── Calendar detail: zodiac at size 22 (lunar year info line) ──
    ("鼠", 22, "sx22_rat"),
    ("牛", 22, "sx22_ox"),
    ("虎", 22, "sx22_tiger"),
    ("兔", 22, "sx22_rabbit"),
    ("龍", 22, "sx22_dragon"),
    ("蛇", 22, "sx22_snake"),
    ("馬", 22, "sx22_horse"),
    ("羊", 22, "sx22_goat"),
    ("猴", 22, "sx22_monkey"),
    ("雞", 22, "sx22_rooster"),
    ("狗", 22, "sx22_dog"),
    ("豬", 22, "sx22_pig"),

    # ── Calendar detail: info line components at size 22 ──
    ("年", 22, "nian22"),
    ("月", 22, "yue22"),
    ("日", 22, "ri22"),
    ("大", 22, "da22"),
    ("小", 22, "xiao22"),

    # ── Calendar detail: 農曆 info line at size 26 ──
    ("農曆", 26, "lunar26"),
    ("年", 26, "nian26"),
    ("月", 26, "yue26"),
    ("日", 26, "ri26"),
    ("大", 26, "da26"),
    ("小", 26, "xiao26"),
    # Zodiac animals at size 26 for 農曆 info line
    ("鼠", 26, "sx26_rat"),   ("牛", 26, "sx26_ox"),      ("虎", 26, "sx26_tiger"),
    ("兔", 26, "sx26_rabbit"),("龍", 26, "sx26_dragon"),  ("蛇", 26, "sx26_snake"),
    ("馬", 26, "sx26_horse"), ("羊", 26, "sx26_goat"),    ("猴", 26, "sx26_monkey"),
    ("雞", 26, "sx26_rooster"),("狗", 26, "sx26_dog"),    ("豬", 26, "sx26_pig"),

    # ── Calendar detail: 農曆 info line at size 30 ──
    ("農曆", 30, "lunar30"),
    ("年", 30, "nian30"),
    ("月", 30, "yue30"),
    ("日", 30, "ri30"),
    ("大", 30, "da30"),
    ("小", 30, "xiao30"),
    # Zodiac animals at size 30
    ("鼠", 30, "sx30_rat"),   ("牛", 30, "sx30_ox"),      ("虎", 30, "sx30_tiger"),
    ("兔", 30, "sx30_rabbit"),("龍", 30, "sx30_dragon"),  ("蛇", 30, "sx30_snake"),
    ("馬", 30, "sx30_horse"), ("羊", 30, "sx30_goat"),    ("猴", 30, "sx30_monkey"),
    ("雞", 30, "sx30_rooster"),("狗", 30, "sx30_dog"),    ("豬", 30, "sx30_pig"),
    # 六十甲子 at size 30
    ("甲子", 30, "gz30_00"), ("乙丑", 30, "gz30_01"), ("丙寅", 30, "gz30_02"),
    ("丁卯", 30, "gz30_03"), ("戊辰", 30, "gz30_04"), ("己巳", 30, "gz30_05"),
    ("庚午", 30, "gz30_06"), ("辛未", 30, "gz30_07"), ("壬申", 30, "gz30_08"),
    ("癸酉", 30, "gz30_09"), ("甲戌", 30, "gz30_10"), ("乙亥", 30, "gz30_11"),
    ("丙子", 30, "gz30_12"), ("丁丑", 30, "gz30_13"), ("戊寅", 30, "gz30_14"),
    ("己卯", 30, "gz30_15"), ("庚辰", 30, "gz30_16"), ("辛巳", 30, "gz30_17"),
    ("壬午", 30, "gz30_18"), ("癸未", 30, "gz30_19"), ("甲申", 30, "gz30_20"),
    ("乙酉", 30, "gz30_21"), ("丙戌", 30, "gz30_22"), ("丁亥", 30, "gz30_23"),
    ("戊子", 30, "gz30_24"), ("己丑", 30, "gz30_25"), ("庚寅", 30, "gz30_26"),
    ("辛卯", 30, "gz30_27"), ("壬辰", 30, "gz30_28"), ("癸巳", 30, "gz30_29"),
    ("甲午", 30, "gz30_30"), ("乙未", 30, "gz30_31"), ("丙申", 30, "gz30_32"),
    ("丁酉", 30, "gz30_33"), ("戊戌", 30, "gz30_34"), ("己亥", 30, "gz30_35"),
    ("庚子", 30, "gz30_36"), ("辛丑", 30, "gz30_37"), ("壬寅", 30, "gz30_38"),
    ("癸卯", 30, "gz30_39"), ("甲辰", 30, "gz30_40"), ("乙巳", 30, "gz30_41"),
    ("丙午", 30, "gz30_42"), ("丁未", 30, "gz30_43"), ("戊申", 30, "gz30_44"),
    ("己酉", 30, "gz30_45"), ("庚戌", 30, "gz30_46"), ("辛亥", 30, "gz30_47"),
    ("壬子", 30, "gz30_48"), ("癸丑", 30, "gz30_49"), ("甲寅", 30, "gz30_50"),
    ("乙卯", 30, "gz30_51"), ("丙辰", 30, "gz30_52"), ("丁巳", 30, "gz30_53"),
    ("戊午", 30, "gz30_54"), ("己未", 30, "gz30_55"), ("庚申", 30, "gz30_56"),
    ("辛酉", 30, "gz30_57"), ("壬戌", 30, "gz30_58"), ("癸亥", 30, "gz30_59"),

    # ── Calendar detail: 吉/凶 at size 20 (hourly fortune enlarged) ──
    ("吉", 20, "lucky_20"),
    ("凶", 20, "unlucky_20"),

    # ── Calendar detail: 六十甲子 at size 20 (for 時辰吉凶 hourly GanZhi) ──
    ("甲子", 20, "gz20_00"), ("乙丑", 20, "gz20_01"), ("丙寅", 20, "gz20_02"),
    ("丁卯", 20, "gz20_03"), ("戊辰", 20, "gz20_04"), ("己巳", 20, "gz20_05"),
    ("庚午", 20, "gz20_06"), ("辛未", 20, "gz20_07"), ("壬申", 20, "gz20_08"),
    ("癸酉", 20, "gz20_09"), ("甲戌", 20, "gz20_10"), ("乙亥", 20, "gz20_11"),
    ("丙子", 20, "gz20_12"), ("丁丑", 20, "gz20_13"), ("戊寅", 20, "gz20_14"),
    ("己卯", 20, "gz20_15"), ("庚辰", 20, "gz20_16"), ("辛巳", 20, "gz20_17"),
    ("壬午", 20, "gz20_18"), ("癸未", 20, "gz20_19"), ("甲申", 20, "gz20_20"),
    ("乙酉", 20, "gz20_21"), ("丙戌", 20, "gz20_22"), ("丁亥", 20, "gz20_23"),
    ("戊子", 20, "gz20_24"), ("己丑", 20, "gz20_25"), ("庚寅", 20, "gz20_26"),
    ("辛卯", 20, "gz20_27"), ("壬辰", 20, "gz20_28"), ("癸巳", 20, "gz20_29"),
    ("甲午", 20, "gz20_30"), ("乙未", 20, "gz20_31"), ("丙申", 20, "gz20_32"),
    ("丁酉", 20, "gz20_33"), ("戊戌", 20, "gz20_34"), ("己亥", 20, "gz20_35"),
    ("庚子", 20, "gz20_36"), ("辛丑", 20, "gz20_37"), ("壬寅", 20, "gz20_38"),
    ("癸卯", 20, "gz20_39"), ("甲辰", 20, "gz20_40"), ("乙巳", 20, "gz20_41"),
    ("丙午", 20, "gz20_42"), ("丁未", 20, "gz20_43"), ("戊申", 20, "gz20_44"),
    ("己酉", 20, "gz20_45"), ("庚戌", 20, "gz20_46"), ("辛亥", 20, "gz20_47"),
    ("壬子", 20, "gz20_48"), ("癸丑", 20, "gz20_49"), ("甲寅", 20, "gz20_50"),
    ("乙卯", 20, "gz20_51"), ("丙辰", 20, "gz20_52"), ("丁巳", 20, "gz20_53"),
    ("戊午", 20, "gz20_54"), ("己未", 20, "gz20_55"), ("庚申", 20, "gz20_56"),
    ("辛酉", 20, "gz20_57"), ("壬戌", 20, "gz20_58"), ("癸亥", 20, "gz20_59"),

    # ── Calendar detail: "節氣：" at size 22 ──
    ("節氣：", 22, "solar_term_label"),

    # ── Calendar detail: "今日" at size 22 ──
    ("今日", 22, "today_22"),

    # ── Calendar detail: solar terms at size 22 ──
    ("小寒", 22, "st22_xiaohan"),
    ("大寒", 22, "st22_dahan"),
    ("立春", 22, "st22_lichun"),
    ("雨水", 22, "st22_yushui"),
    ("驚蟄", 22, "st22_jingzhe"),
    ("春分", 22, "st22_chunfen"),
    ("清明", 22, "st22_qingming"),
    ("穀雨", 22, "st22_guyu"),
    ("立夏", 22, "st22_lixia"),
    ("小滿", 22, "st22_xiaoman"),
    ("芒種", 22, "st22_mangzhong"),
    ("夏至", 22, "st22_xiazhi"),
    ("小暑", 22, "st22_xiaoshu"),
    ("大暑", 22, "st22_dashu"),
    ("立秋", 22, "st22_liqiu"),
    ("處暑", 22, "st22_chushu"),
    ("白露", 22, "st22_bailu"),
    ("秋分", 22, "st22_qiufen"),
    ("寒露", 22, "st22_hanlu"),
    ("霜降", 22, "st22_shuangjian"),
    ("立冬", 22, "st22_lidong"),
    ("小雪", 22, "st22_xiaoxue"),
    ("大雪", 22, "st22_daxue"),
    ("冬至", 22, "st22_dongzhi"),

    # ── Calendar detail: solar term countdown (size 20) ──
    ("小寒", 20, "st20_xiaohan"),
    ("大寒", 20, "st20_dahan"),
    ("立春", 20, "st20_lichun"),
    ("雨水", 20, "st20_yushui"),
    ("驚蟄", 20, "st20_jingzhe"),
    ("春分", 20, "st20_chunfen"),
    ("清明", 20, "st20_qingming"),
    ("穀雨", 20, "st20_guyu"),
    ("立夏", 20, "st20_lixia"),
    ("小滿", 20, "st20_xiaoman"),
    ("芒種", 20, "st20_mangzhong"),
    ("夏至", 20, "st20_xiazhi"),
    ("小暑", 20, "st20_xiaoshu"),
    ("大暑", 20, "st20_dashu"),
    ("立秋", 20, "st20_liqiu"),
    ("處暑", 20, "st20_chushu"),
    ("白露", 20, "st20_bailu"),
    ("秋分", 20, "st20_qiufen"),
    ("寒露", 20, "st20_hanlu"),
    ("霜降", 20, "st20_shuangjian"),
    ("立冬", 20, "st20_lidong"),
    ("小雪", 20, "st20_xiaoxue"),
    ("大雪", 20, "st20_daxue"),
    ("冬至", 20, "st20_dongzhi"),
    ("→",   20, "arrow20"),
    ("天",   20, "tian20"),

    # ── Calendar detail: digits 0-9 at size 20 (countdown + lucky numbers) ──
    ("0", 20, "d20_0"),
    ("1", 20, "d20_1"),
    ("2", 20, "d20_2"),
    ("3", 20, "d20_3"),
    ("4", 20, "d20_4"),
    ("5", 20, "d20_5"),
    ("6", 20, "d20_6"),
    ("7", 20, "d20_7"),
    ("8", 20, "d20_8"),
    ("9", 20, "d20_9"),

    # ── Calendar detail: lucky numbers title (size 20) ──
    ("今日吉數", 20, "lucky_title"),

    # ══════════════════════════════════════════════════════════════════════
    # ── Weather: digits + symbols for numeric display ──
    # ══════════════════════════════════════════════════════════════════════

    # ── Weather: digits 0-9 at size 64 (big temperature) ──
    ("0", 64, "d64_0"),
    ("1", 64, "d64_1"),
    ("2", 64, "d64_2"),
    ("3", 64, "d64_3"),
    ("4", 64, "d64_4"),
    ("5", 64, "d64_5"),
    ("6", 64, "d64_6"),
    ("7", 64, "d64_7"),
    ("8", 64, "d64_8"),
    ("9", 64, "d64_9"),
    ("-", 64, "neg64"),
    ("°C", 64, "degc64"),
    ("°F", 64, "degf64"),

    # ── Weather: digits 0-9 at size 26 (detail grid) ──
    ("0", 26, "d26_0"),
    ("1", 26, "d26_1"),
    ("2", 26, "d26_2"),
    ("3", 26, "d26_3"),
    ("4", 26, "d26_4"),
    ("5", 26, "d26_5"),
    ("6", 26, "d26_6"),
    ("7", 26, "d26_7"),
    ("8", 26, "d26_8"),
    ("9", 26, "d26_9"),
    (".", 26, "dot26"),
    ("-", 26, "neg26"),
    (":", 26, "colon26"),
    ("%", 26, "pct26"),
    ("°C", 26, "degc26"),
    ("°F", 26, "degf26"),
    ("m/s", 26, "ms26"),
    ("mph", 26, "mph26"),
    ("hPa", 26, "hpa26"),
    ("km", 26, "km26"),
    ("(", 26, "lp26"),
    (")", 26, "rp26"),
    ("PM2.5", 26, "pm25_26"),

    # ── Weather: digits 0-9 at size 24 (feels like, forecast) ──
    ("0", 24, "d24_0"),
    ("1", 24, "d24_1"),
    ("2", 24, "d24_2"),
    ("3", 24, "d24_3"),
    ("4", 24, "d24_4"),
    ("5", 24, "d24_5"),
    ("6", 24, "d24_6"),
    ("7", 24, "d24_7"),
    ("8", 24, "d24_8"),
    ("9", 24, "d24_9"),
    (".", 24, "dot24"),
    ("-", 24, "neg24"),
    ("~", 24, "tilde24"),
    ("°C", 24, "degc24"),
    ("°F", 24, "degf24"),
    ("/", 24, "slash24"),
    ("(", 24, "lp24"),
    (")", 24, "rp24"),
    (":", 24, "colon24"),

    # ── Weather: weekday chars at size 24 (forecast date) ──
    ("日", 24, "wk24_sun"),
    ("一", 24, "wk24_mon"),
    ("二", 24, "wk24_tue"),
    ("三", 24, "wk24_wed"),
    ("四", 24, "wk24_thu"),
    ("五", 24, "wk24_fri"),
    ("六", 24, "wk24_sat"),

    # ── Weather: °C/°F button (size 28) ──
    ("°C", 28, "degc28"),
    ("°F", 28, "degf28"),

    # ── Weather: update time (size 20) ──
    (":", 20, "colon20"),
    ("更新", 20, "updated20"),

    # ── Weather: "體感" at size 24 with space ──
    (" ", 24, "sp24"),
    (" ", 26, "sp26"),

    # ── Weather: city name at size 38 (digits for non-Chinese fallback) ──
    ("0", 38, "d38_0"),
    ("1", 38, "d38_1"),
    ("2", 38, "d38_2"),
    ("3", 38, "d38_3"),
    ("4", 38, "d38_4"),
    ("5", 38, "d38_5"),
    ("6", 38, "d38_6"),
    ("7", 38, "d38_7"),
    ("8", 38, "d38_8"),
    ("9", 38, "d38_9"),

    # ── Missing labels found by find_missing_labels.py ──
    # Size 20
    ("• BLE UART 模式", 20, "s20_ble_uart"),
    ("• 可掃描並連接裝置", 20, "s20_scan_connect"),
    ("啟用後可通過", 20, "s20_enable_via"),
    ("朔", 20, "s20_shuo"),
    ("望", 20, "s20_wang"),
    ("藍牙低功耗傳輸", 20, "s20_ble_transfer"),
    ("資料到裝置", 20, "s20_data_to_device"),
    # Size 22
    ("Silver（像素風格字體）", 22, "s22_silver_pixel"),
    ("掃描中...", 22, "s22_scanning"),
    ("未發現裝置", 22, "s22_no_device"),
    ("每10頁全刷新", 22, "s22_every10_refresh"),
    ("每頁全刷新", 22, "s22_every_refresh"),
    ("源樣明體 GenYoMinTW", 22, "s22_genyomin"),
    ("系統預設", 22, "s22_sys_default"),
    ("裝置名稱：M5Paper-BLE", 22, "s22_device_name"),
    ("說明：", 22, "s22_desc_colon"),
    ("請先轉換並上傳 .bin 檔案", 22, "s22_convert_upload"),
    ("請將 fortune_slips.bin 放至 SD 卡", 22, "s22_fortune_sd"),
    # Size 24
    ("• 像素風格點陣字體", 24, "s24_pixel_font"),
    ("• 切換後需重新啟動", 24, "s24_restart_needed"),
    ("• 標籤內建於韌體中", 24, "s24_labels_builtin"),
    ("• 標籤從 SD 卡載入", 24, "s24_labels_sd"),
    ("• 適合繁體中文閱讀", 24, "s24_tc_reading"),
    ("• 預設系統字體", 24, "s24_default_font"),
    ("圖片載入失敗", 24, "s24_img_load_fail"),
    ("此頁無圖片內容", 24, "s24_no_img_content"),
    ("清除日期", 24, "s24_clear_date"),
    ("載入中...", 24, "s24_loading"),
    ("輪播", 24, "s24_slideshow"),
    ("輪播中", 24, "s24_slideshowing"),
    ("返回", 24, "s24_return"),
    ("隨機", 24, "s24_random"),
    ("點擊螢幕繼續", 24, "s24_tap_continue"),
    # Size 26
    ("【解曰】", 26, "s26_jie_yue"),
    ("【詩意】", 26, "s26_shi_yi"),
    # Size 28
    ("Silver", 28, "s28_silver"),
    ("_", 28, "s28_underscore"),
    ("今天", 28, "s28_today"),
    ("取消", 28, "s28_cancel"),
    ("掃描", 28, "s28_scan"),
    ("掃描裝置", 28, "s28_scan_device"),
    ("時鐘", 28, "s28_clock"),
    ("源樣明體 GenYoMinTW", 28, "s28_genyomin"),
    ("無法讀取故事", 28, "s28_cant_read_story"),
    ("無法讀取籖文", 28, "s28_cant_read_slip"),
    ("無法開啟籖檔", 28, "s28_cant_open_slip"),
    ("確定", 28, "s28_confirm"),
    ("示例書籍1.txt", 28, "s28_sample1"),
    ("示例書籍2.txt", 28, "s28_sample2"),
    ("示例書籍3.txt", 28, "s28_sample3"),
    ("籖圖資料異常", 28, "s28_slip_data_err"),
    ("籖檔格式錯誤", 28, "s28_slip_fmt_err"),
    ("籖檔為空", 28, "s28_slip_empty"),
    ("觸控選擇書籍", 28, "s28_touch_select"),
    ("記憶體不足", 28, "s28_out_of_mem"),
    ("讀取籖圖失敗", 28, "s28_slip_read_fail"),
    ("連接", 28, "s28_connect"),
    ("附近藍牙裝置：", 28, "s28_nearby_ble"),
    # Size 32
    ("Silver", 32, "s32_silver"),
    ("僅顯示每頁第一張圖片", 32, "s32_show_first_img"),
    ("已連接", 32, "s32_connected"),
    ("格言", 32, "s32_motto"),
    ("此EPUB每頁含多張圖片", 32, "s32_multi_img"),
    ("源樣明體", 32, "s32_genyomin"),
    ("狀態：", 32, "s32_status"),
    ("目前：", 32, "s32_current"),
    ("系統字體", 32, "s32_sys_font"),
    ("翻頁刷新模式", 32, "s32_page_refresh"),
    ("跳到頁面", 32, "s32_jump_page"),
    ("醒世", 32, "s32_wake"),
    # Size 36
    ("切換為 Silver", 36, "s36_switch_silver"),
    ("切換為 源樣明體", 36, "s36_switch_genyomin"),
    ("安全模式", 36, "s36_safe_mode"),
    ("求籖", 36, "s36_fortune"),
    ("選擇年月", 36, "s36_select_ym"),
    # Size 40
    ("系統字體", 40, "s40_sys_font"),
    ("藍牙", 40, "s40_bluetooth"),
    # SD card absent labels
    ("未插入 SD 卡", 22, "s22_no_sd"),
    ("未插入 SD 卡", 32, "s32_no_sd"),
    ("請插入 SD 卡後重新啟動", 24, "s24_insert_sd"),
]


def render_label(text, font_size, font):
    """Render text to a grayscale image, return cropped Image."""
    # Estimate canvas size (generous)
    est_w = len(text) * font_size + font_size * 2
    est_h = font_size * 2
    img = Image.new("L", (est_w, est_h), 255)
    draw = ImageDraw.Draw(img)
    draw.text((font_size // 2, font_size // 4), text, font=font, fill=0)

    # Find bounding box of text pixels (dark pixels).
    # getbbox() finds non-zero pixels, but our bg=255 is also non-zero.
    # Invert first so white bg → 0 (ignored) and dark text → non-zero (found).
    inverted = ImageOps.invert(img)
    bbox = inverted.getbbox()
    if bbox is None:
        # Empty — return 1x1 white
        return Image.new("L", (1, 1), 255)

    # Crop with 2px padding to preserve antialiased edges
    x0 = max(0, bbox[0] - 2)
    y0 = max(0, bbox[1] - 2)
    x1 = min(img.width, bbox[2] + 2)
    y1 = min(img.height, bbox[3] + 2)
    return img.crop((x0, y0, x1, y1))


def image_to_4bit(img):
    """Convert 8-bit grayscale image to packed 4-bit array.
    
    Two pixels per byte: high nibble = left pixel, low nibble = right pixel.
    Pixel values 0-255 are mapped to 0-15 (0=black, 15=white).
    """
    w, h = img.size
    data = []
    for y in range(h):
        row_bytes = []
        for x in range(0, w, 2):
            p0 = img.getpixel((x, y))
            p0_4 = p0 >> 4  # Map 0-255 to 0-15
            if x + 1 < w:
                p1 = img.getpixel((x + 1, y))
                p1_4 = p1 >> 4
            else:
                p1_4 = 0x0F  # Pad with white
            row_bytes.append((p0_4 << 4) | p1_4)
        data.extend(row_bytes)
    return bytes(data)


def write_header(var_name, data_bytes, width, height, header_path, text, font_size):
    """Write a C header file with the bitmap data."""
    with open(header_path, "w", encoding="utf-8") as f:
        f.write(f'// Auto-generated label bitmap: "{text}" (size {font_size})\n')
        f.write("#pragma once\n\n")
        f.write("#include <pgmspace.h>\n")
        f.write("#include <stdint.h>\n\n")
        f.write(f"const uint16_t {var_name}_w = {width};\n")
        f.write(f"const uint16_t {var_name}_h = {height};\n\n")
        f.write(f"const uint8_t {var_name}_bitmap[] PROGMEM = {{\n")

        for i in range(0, len(data_bytes), 16):
            chunk = data_bytes[i:i + 16]
            hex_vals = ", ".join(f"0x{b:02x}" for b in chunk)
            comma = "," if i + 16 < len(data_bytes) else ""
            f.write(f"    {hex_vals}{comma}\n")

        f.write("};\n")


def write_master_header(labels_info, out_dir):
    """Write the master header that includes all label headers and provides lookup."""
    master_path = os.path.join(out_dir, "label_bitmaps.h")
    with open(master_path, "w", encoding="utf-8") as f:
        f.write("// Auto-generated master label bitmap header\n")
        f.write("// Do not edit — regenerate with convert_labels.py\n")
        f.write("#pragma once\n\n")
        f.write("#include <pgmspace.h>\n")
        f.write("#include <stdint.h>\n")
        f.write("#include <string.h>\n\n")

        # Include all individual headers
        for text, size, var_suffix, filename in labels_info:
            f.write(f'#include "{filename}"\n')

        f.write("\n// Label bitmap entry for lookup\n")
        f.write("struct LabelBitmap {\n")
        f.write("  const char* text;\n")
        f.write("  uint16_t fontSize;\n")
        f.write("  uint16_t w;\n")
        f.write("  uint16_t h;\n")
        f.write("  const uint8_t* bitmap;\n")
        f.write("};\n\n")

        f.write(f"const int kLabelBitmapCount = {len(labels_info)};\n\n")
        f.write("const LabelBitmap kLabelBitmaps[] PROGMEM = {\n")
        for text, size, var_suffix, filename in labels_info:
            var = f"label_{var_suffix}"
            # Escape the text for C string
            c_text = text.replace("\\", "\\\\").replace('"', '\\"')
            f.write(f'  {{"{c_text}", {size}, {var}_w, {var}_h, {var}_bitmap}},\n')
        f.write("};\n\n")

        # Lookup function
        f.write("// Find a pre-rendered label bitmap by text and font size.\n")
        f.write("// Returns nullptr if not found.\n")
        f.write("inline const LabelBitmap* findLabelBitmap(const char* text, uint16_t fontSize) {\n")
        f.write("  for (int i = 0; i < kLabelBitmapCount; i++) {\n")
        f.write("    if (kLabelBitmaps[i].fontSize == fontSize && strcmp(kLabelBitmaps[i].text, text) == 0) {\n")
        f.write("      return &kLabelBitmaps[i];\n")
        f.write("    }\n")
        f.write("  }\n")
        f.write("  return nullptr;\n")
        f.write("}\n")

    print(f"\nMaster header: {master_path}")


def main():
    font_path = FONT_PATH

    # Allow --font override
    for i, arg in enumerate(sys.argv[1:], 1):
        if arg == "--font" and i < len(sys.argv) - 1:
            font_path = sys.argv[i + 1]

    if not os.path.exists(font_path):
        print(f"ERROR: Font not found: {font_path}")
        print("Usage: python3 convert_labels.py [--font /path/to/font.ttf]")
        sys.exit(1)

    os.makedirs(OUT_DIR, exist_ok=True)
    print(f"Font: {font_path}")
    print(f"Output: {OUT_DIR}")
    print(f"Labels: {len(LABELS)}")
    print()

    # Cache fonts by size
    font_cache = {}
    labels_info = []
    total_bytes = 0

    for entry in LABELS:
        text, font_size, var_suffix = entry[0], entry[1], entry[2]
        label_font_path = entry[3] if len(entry) > 3 else font_path

        cache_key = (font_size, label_font_path)
        if cache_key not in font_cache:
            font_cache[cache_key] = ImageFont.truetype(label_font_path, font_size)

        font = font_cache[cache_key]
        img = render_label(text, font_size, font)
        w, h = img.size
        data = image_to_4bit(img)
        total_bytes += len(data)

        var_name = f"label_{var_suffix}"
        filename = f"{var_name}.h"
        header_path = os.path.join(OUT_DIR, filename)

        write_header(var_name, data, w, h, header_path, text, font_size)
        labels_info.append((text, font_size, var_suffix, filename))

        print(f"  {text:30s}  size={font_size:2d}  {w:3d}x{h:<3d}  {len(data):5d} bytes  -> {filename}")

    write_master_header(labels_info, OUT_DIR)

    print(f"\nTotal: {len(LABELS)} labels, {total_bytes:,} bytes ({total_bytes/1024:.1f} KB)")
    print("Done!")


if __name__ == "__main__":
    main()
