# ZLRomi NEO Music Player

ESP32-S3 音乐播放器，支持蓝牙音箱 + PCM5102A DAC 双输出。

## 硬件

- **主控**: ESP32-S3 (HW-678, 16MB Flash, 8MB PSRAM)
- **屏幕**: ST7789 240x320 SPI
- **DAC**: PCM5102A I2S (BCK=G9, LRCK=G11, DIN=G10)
- **按键**: 7键导航 (UP/DOWN/LEFT/RIGHT/OK/SET/RST)
- **蓝牙**: A2DP Source 连接音箱

## 功能

- LVGL GUI 深色主题
- 拼音输入法 (lv_ime_pinyin)
- 7键焦点导航 (电视遥控风格)
- 网易云音乐 API
- 歌词同步显示
- 双音频输出: I2S + Bluetooth A2DP

## 构建

### GitHub Actions
推送代码后自动构建，从 Actions 页面下载固件。

### 本地构建
```bash
pio run -e esp32s3
pio run -t upload -e esp32s3
```

## 接线

| 功能 | GPIO |
|------|------|
| LCD SCLK | 21 |
| LCD MOSI | 47 |
| LCD RST | 45 |
| LCD DC | 40 |
| LCD CS | 41 |
| LCD BL | 42 |
| BTN UP | 17 |
| BTN DOWN | 16 |
| BTN LEFT | 15 |
| BTN RIGHT | 4 |
| BTN OK | 5 |
| BTN SET | 6 |
| BTN RST | 7 |
| I2S BCK | 9 |
| I2S LRCK | 11 |
| I2S DIN | 10 |

## License

MIT
