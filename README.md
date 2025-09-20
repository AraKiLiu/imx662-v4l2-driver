# Sony IMX662 Camera V4L2 Driver for Raspberry Pi

[English](#english) | [繁體中文](#繁體中文)

## English

### Overview
High-performance V4L2 driver for Sony IMX662 CMOS image sensor, optimized for Raspberry Pi systems. Based on IMX477 (Raspberry Pi HQ Camera) architecture with enhanced features for IMX662.

### Key Features
- ✅ Linux Kernel 6.12+ support (required)
- ✅ Up to 90fps @ 1936x1100 resolution
- ✅ 16-bit color depth support
- ✅ Y-binning fix for improved image quality
- ✅ MIPI CSI-2 interface (2-lane/4-lane configurable)
- ✅ Monochrome sensor support
- ✅ Full libcamera integration

### Quick Installation
```bash
# For Standard Raspberry Pi OS (with GUI)
./all_setup.sh

# For Raspberry Pi OS Lite (CLI only)
./lite_all_setup.sh

# Reboot system
sudo reboot

# Verify installation
rpicam-hello --list-cameras
```

For detailed instructions, see the [full documentation](#detailed-installation) below.

---

## 繁體中文

### 概述
Sony IMX662 CMOS 影像感測器的高性能 V4L2 驅動程式，專為 Raspberry Pi 系統優化。基於 IMX477（Raspberry Pi HQ Camera）架構開發，並針對 IMX662 增強功能。

### 主要特色
- ✅ 支援 Linux Kernel 6.12+（必需）
- ✅ 最高 90fps @ 1936x1100 解析度
- ✅ 16-bit 色彩深度支援
- ✅ Y-binning 修復，提升影像品質
- ✅ MIPI CSI-2 介面（2-lane/4-lane 可配置）
- ✅ 支援單色感測器
- ✅ 完整的 libcamera 整合
- ✅ 雙相機同時使用支援

### 檔案說明

| 檔案 | 用途 |
|------|------|
| `imx662.c` | V4L2 核心驅動程式 |
| `imx662-overlay.dts` | 設備樹覆蓋層 |
| `cam_helper_imx662.cpp` | libcamera 輔助類 |
| `imx662.json` | libcamera 調校參數 |
| `all_setup.sh` | **標準版完整安裝（推薦）** |
| `lite_all_setup.sh` | **Lite 版安裝腳本** |
| `setup.sh` | 僅安裝驅動模組 |
| `dkms.conf` | DKMS 配置 |
| `Makefile` | 編譯配置 |

## 致謝 / Credits

- **Octopuscinema** & **Soho-enterprise** - IMX585 driver base
- **Will Whang** - Driver porting and optimization
- **Sasha Shturma** - DKMS installation script reference
- **sohonomura2020** - Y-binning fix and dual camera support

## 🚀 快速安裝（推薦）

根據您的 Raspberry Pi OS 版本選擇對應的安裝腳本：

### 📱 標準版 Raspberry Pi OS
```bash
# 解壓驅動程式套件
tar -zxf imx662-v4l2-driver_6_12_y-binning-fix.tgz
cd imx662-v4l2-driver_6_12_y-binning-fix/

# 執行完整自動安裝（包含 V4L2 + libcamera）
./all_setup.sh

# 安裝完成後重新啟動
sudo reboot

# 重啟後測試攝像頭
rpicam-hello --list-cameras
```

### 💻 Raspberry Pi OS Lite
```bash
# 解壓驅動程式套件
tar -zxf imx662-v4l2-driver_6_12_y-binning-fix.tgz
cd imx662-v4l2-driver_6_12_y-binning-fix/

# 執行 Lite 版本自動安裝（無 GUI，純命令列）
./lite_all_setup.sh

# 安裝完成後重新啟動
sudo reboot

# 重啟後測試攝像頭（無預覽）
rpicam-hello --list-cameras --nopreview
```

**自動安裝腳本功能比較：**

| 功能 | 標準版 (`all_setup.sh`) | Lite 版 (`lite_all_setup.sh`) |
|------|-------------------------|-------------------------------|
| ✅ 系統需求檢查 | ✅ 完整檢查 | ✅ 完整檢查 + Lite 版本驗證 |
| ✅ 相依套件安裝 | ✅ 包含 GUI 套件 | ✅ 純命令列套件 |
| ✅ V4L2 驅動程式 | ✅ 完整安裝 | ✅ 完整安裝 |
| ✅ 開機配置 | ✅ 標準配置 | ✅ Lite 版本最佳化 |
| ✅ libcamera 編譯 | ✅ 支援 GUI | ✅ 純命令列版本 |
| ✅ rpicam-apps | ✅ 完整功能 | ✅ 命令列工具 |
| ✅ 安裝驗證 | ✅ 完整驗證 | ✅ 命令列驗證 |

**預計時間：** 
- **標準版**: 60-90 分鐘（包含 GUI 套件編譯）
- **Lite 版**: 45-75 分鐘（節省 GUI 編譯時間）

---

## 手動安裝指南

如果您希望了解詳細步驟或需要自訂安裝，請參考以下手動安裝指南。

## 📋 系統需求與安裝前檢查

在開始安裝之前，請確保您的系統符合以下需求：

### 硬體需求

#### 檢查 Raspberry Pi 型號
```bash
cat /proc/device-tree/model
cat /proc/cpuinfo | grep "Model"
```

**必需硬體：**
- ✅ **Raspberry Pi 5**（主要支援）
- ✅ **Raspberry Pi 4B**（部分支援，性能受限）
- ❌ 較舊的 Raspberry Pi 型號（不支援）
- Sony IMX662 攝像頭模組
- CSI 攝像頭排線
- 至少 8GB microSD 卡（建議 32GB 以上）

#### 檢查架構
```bash
uname -m  # 必須顯示 aarch64 (ARM64 架構)
```
如果顯示 `armv7l`，表示您使用的是 32-bit 系統，需要重新安裝 64-bit 版本的 Raspberry Pi OS。

### 軟體需求

#### 作業系統版本檢查
```bash
cat /etc/os-release
lsb_release -a
```

**建議版本：**
- Raspberry Pi OS Bookworm (2023-12-05 或更新)
- Debian 12 (Bookworm) 或更新
- Ubuntu 22.04 LTS 或更新（ARM64 版本）

#### 核心版本檢查（關鍵！）
```bash
uname -r
```

**絕對必需：** Linux 核心 **6.12** 或更新版本
- ✅ 6.12.34+rpt-rpi-2712（已測試）
- ✅ 6.12.x 系列
- ❌ 6.11.x 或更舊版本（不支援）

**如果核心版本過舊，必須先升級：**
```bash
sudo apt update
sudo apt full-upgrade
sudo reboot
# 重新啟動後再次檢查：uname -r
```

### 記憶體和儲存空間檢查
```bash
# 檢查可用記憶體（建議 ≥ 4GB）
free -h

# 檢查可用儲存空間（需要至少 2GB 可用空間）
df -h /
```

### 開發工具安裝
編譯核心模組需要以下工具。如果尚未安裝，請使用以下指令安裝：

```bash
sudo apt update
sudo apt install linux-headers dkms git build-essential
```

**各工具說明：**
- `linux-headers`：核心標頭檔案，編譯驅動程式必需
- `dkms`：動態核心模組系統，用於自動管理驅動程式
- `git`：版本控制工具
- `build-essential`：包含 gcc 編譯器等基本開發工具

### 驗證開發環境
```bash
# 檢查編譯器
gcc --version

# 檢查 DKMS
dkms --version

# 檢查核心標頭檔是否存在
ls /usr/src/linux-headers-$(uname -r)/

# 檢查核心配置
ls /lib/modules/$(uname -r)/build/
```
**所有指令都應該有正常輸出，沒有錯誤訊息。**
## 詳細安裝步驟

### 第一步：準備安裝環境

1. **確認系統版本**
   ```bash
   cat /etc/os-release
   lsb_release -a
   ```

2. **檢查核心版本（重要！）**
   ```bash
   uname -r
   ```
   必須是 6.12 或更高版本。如果版本過舊，請執行系統升級：
   ```bash
   sudo apt update && sudo apt full-upgrade
   sudo reboot
   ```

3. **安裝必要的開發工具**
   ```bash
   sudo apt update
   sudo apt install linux-headers dkms git build-essential
   ```

4. **驗證安裝是否成功**
   ```bash
   dkms --version
   gcc --version
   ls /usr/src/linux-headers-$(uname -r)/
   ```

### 第二步：取得驅動程式原始碼

如果您是從 GitHub 下載：
```bash
git clone https://github.com/will127534/imx662-v4l2-driver.git
cd imx662-v4l2-driver/
```

如果您有壓縮檔案：
```bash
tar -zxf imx662-v4l2-driver_6_12_y-binning-fix.tgz
cd imx662-v4l2-driver_6_12_y-binning-fix/
```

### 第三步：編譯並安裝驅動程式

執行自動安裝腳本：
```bash 
sudo ./setup.sh
```

安裝腳本會自動執行以下動作：
1. 清除舊版本的 IMX662 驅動程式
2. 將原始碼複製到 DKMS 系統目錄
3. 使用 DKMS 編譯驅動程式
4. 安裝編譯好的核心模組
5. 更新核心模組相依性

**預期的成功訊息：**
```
Building module:
Cleaning build area...
make -j4 KERNELRELEASE=6.12.34+rpt-rpi-2712...
Signing module /var/lib/dkms/imx662/0.0.1/build/imx662.ko
Installation successful
```

### 第四步：設定開機配置

編輯開機配置檔案：
```bash
sudo nano /boot/firmware/config.txt
```

**對於較舊的系統：**
```bash
sudo nano /boot/config.txt
```

在檔案中找到 `camera_auto_detect` 行並修改，然後加入 IMX662 設定：

```
# 停用自動偵測攝像頭
camera_auto_detect=0

# 啟用 IMX662 驅動程式（連接到 cam0 埠）
dtoverlay=imx662,cam0
```

**重要說明：**
- `camera_auto_detect=0`：停用樹莓派的自動攝像頭偵測
- `dtoverlay=imx662,cam0`：載入 IMX662 裝置樹覆蓋層，指定連接到 cam0 埠

### 第五步：重新啟動系統

儲存設定檔案後，重新啟動系統以載入新的驅動程式：
```bash
sudo reboot
```

### 第六步：驗證安裝

重新啟動後，執行以下測試：

```bash
# 1. 檢查驅動載入
lsmod | grep imx662
dkms status | grep imx662

# 2. 檢查 I2C 連接（應在 0x1a 顯示設備）
i2cdetect -y 6

# 3. 列出相機
rpicam-hello --list-cameras

# 4. 測試預覽
rpicam-hello --timeout 5000

# 5. 測試拍照
rpicam-still -o test.jpg --width 1920 --height 1080

# 6. 測試錄影
rpicam-vid -t 5000 -o test.mp4 --width 1920 --height 1080
```

**成功輸出範例：**
```
Available cameras
-----------------
0 : imx662 [1920x1080 16-bit RGGB] (/base/axi/pcie@1000120000/rp1/i2c@88000/imx662@1a)
    Modes: 'SRGGB12_CSI2P' : 1936x1100 [90.00 fps - (0, 0)/1920x1080 crop]
           'SRGGB16' : 1928x1090 [25.00 fps - (0, 0)/1920x1080 crop]
                       3856x2180 [21.90 fps - (0, 0)/1920x1080 crop]
```


## 進階設定選項

### 攝像頭連接埠設定

#### cam0 埠（預設）
如果攝像頭連接到 cam0 埠（較靠近 USB 連接器），使用：
```
camera_auto_detect=0
dtoverlay=imx662,cam0
```

#### cam1 埠
如果攝像頭連接到 cam1 埠（較靠近 GPIO），則省略 cam0 參數：
```
camera_auto_detect=0
dtoverlay=imx662
```

#### 雙相機配置
同時使用兩個 IMX662 相機：
```
camera_auto_detect=0
dtoverlay=imx662,cam0
dtoverlay=imx662,cam1
```

### 電源管理設定

#### 持續供電模式
如果您希望攝像頭持續供電（適用於硬體除錯，會讓 CAM_GPIO 保持高電位）：
```
camera_auto_detect=0
dtoverlay=imx662,always-on
```

**使用時機：**
- 硬體除錯時
- 需要最短啟動延遲
- 連續錄影應用

### 攝像頭類型設定

#### 單色攝像頭
如果您使用的是單色（黑白）版本的 IMX662：
```
camera_auto_detect=0
dtoverlay=imx662,mono
```

### CSI 通道數設定

#### 2通道模式
如果您的系統需要使用 2 通道 CSI 連接：
```
camera_auto_detect=0
dtoverlay=imx662,2lane
```

**注意事項：**
- 4 通道模式提供更高頻寬和更快幀率
- 2 通道模式可能限制最大解析度和幀率
- 請根據您的硬體配置選擇適當的模式

### 連結頻率設定

您可以調整預設的連結頻率（預設為 1440Mbps/通道）以最佳化性能：

```
camera_auto_detect=0
dtoverlay=imx662,link-frequency=720000000
```

#### 可用頻率表

| 頻率值 | 每通道速度 | 4通道最大幀率 (4K 12bit) | 2通道最大幀率 (4K 12bit) | 建議用途 |
|--------|------------|--------------------------|--------------------------|----------|
| 297000000 | 594 Mbps | 20.8 fps | 10.4 fps | 低功耗應用 |
| 360000000 | 720 Mbps | 25.0 fps | 12.5 fps | 標準應用 |
| 445500000 | 891 Mbps | 30.0 fps | 15.0 fps | 中等性能 |
| 594000000 | 1188 Mbps | 41.7 fps | 20.8 fps | 高性能應用 |
| 720000000 | 1440 Mbps | 50.0 fps | 25.0 fps | 預設設定 |
| 891000000 | 1782 Mbps | 60.0 fps | 30.0 fps | 高幀率應用 |
| 1039500000 | 2079 Mbps | 75.0 fps | 37.5 fps | 最高性能 |

#### 重要性能注意事項

- **Raspberry Pi 5 限制**：預設情況下 RP1 晶片限制為 400Mpix/s 處理速度，在 4K 解析度下約限制為 43.8 FPS
- **HDR 模式**：使用 ClearHDR 模式時，幀率會減半
- **1080P 模式**：1080P 2x2 像素合併模式下，幀率會加倍
- **相容性**：某些高頻率設定在 Raspberry Pi 4 上可能不支援

### 組合設定

所有選項都可以同時使用，例如：

```
camera_auto_detect=0
dtoverlay=imx662,always-on,mono,cam0,link-frequency=891000000
```

這個設定組合了：
- 持續供電模式
- 單色攝像頭
- cam0 埠連接
- 高頻率連結（1782 Mbps/通道）

### 效能調校建議

#### 高幀率設定（90+ FPS）
```
camera_auto_detect=0
dtoverlay=imx662,always-on,link-frequency=1039500000
```

#### 低延遲設定
```
camera_auto_detect=0
dtoverlay=imx662,always-on,link-frequency=891000000
```

#### 節能設定
```
camera_auto_detect=0
dtoverlay=imx662,link-frequency=360000000
```

## libcamera 完整整合

### 背景說明

預設情況下，IMX662 驅動程式安裝後只提供 V4L2 層級的支援。要獲得完整的攝像頭功能（包括 `rpicam-hello`、`rpicam-still` 等工具），需要將 IMX662 整合到 libcamera 系統中。

本節提供完整的 libcamera 整合步驟，讓您能夠在全新的系統上快速部署完整的 IMX662 攝像頭解決方案。

### 整合內容

1. **編譯環境設置** - 安裝必要的編譯工具和相依套件
2. **libcamera 源碼下載** - 取得最新的 libcamera 和 rpicam-apps 源碼
3. **感測器支援添加** - 將 IMX662 添加到 libcamera 的感測器支援清單
4. **調校檔案整合** - 配置 IMX662 的影像處理參數
5. **重新編譯安裝** - 編譯並安裝修改後的 libcamera 和 rpicam-apps

### 步驟一：安裝編譯環境

執行以下指令安裝所有必要的編譯工具：

```bash
# 更新套件清單
sudo apt update

# 安裝基本編譯工具
sudo apt install -y git python3-pip python3-jinja2 python3-ply python3-yaml
sudo apt install -y ninja-build meson pkg-config cmake

# 安裝 libcamera 編譯相依套件
sudo apt install -y libdrm-dev libexif-dev libjpeg-dev libtiff-dev
sudo apt install -y libboost-dev libqt5opengl5-dev libvulkan-dev libpng-dev
```

### 步驟二：下載並配置源碼

建立編譯目錄並下載源碼：

```bash
# 建立工作目錄
mkdir -p /home/pi/camera_build
cd /home/pi/camera_build

# 下載 libcamera 源碼
git clone https://git.libcamera.org/libcamera/libcamera.git
cd libcamera

# 下載 rpicam-apps 源碼
cd /home/pi/camera_build
git clone https://github.com/raspberrypi/rpicam-apps.git
```

### 步驟三：添加 IMX662 相機協助程式

將本驅動程式套件中的 `cam_helper_imx662.cpp` 複製到 libcamera 源碼樹中：

```bash
# 複製 IMX662 相機協助程式檔案
cp /home/pi/imx662-v4l2-driver_6_12_y-binning-fix/cam_helper_imx662.cpp \
   /home/pi/camera_build/libcamera/src/ipa/rpi/cam_helper/
```

將 IMX662 添加到 meson 建置系統：

```bash
# 編輯 meson.build 檔案
nano /home/pi/camera_build/libcamera/src/ipa/rpi/cam_helper/meson.build
```

在 `rpi_ipa_cam_helper_sources` 清單中添加一行：
```
    'cam_helper_imx662.cpp',
```

範例（在 imx519 之後添加）：
```
rpi_ipa_cam_helper_sources = files([
    'cam_helper.cpp',
    'cam_helper_ov5647.cpp',
    # ... 其他檔案 ...
    'cam_helper_imx519.cpp',
    'cam_helper_imx662.cpp',    # ← 添加這一行
    'cam_helper_imx708.cpp',
    # ... 其他檔案 ...
])
```

### 步驟四：配置 IMX662 調校檔案

本驅動程式套件包含 IMX662 的專用調校檔案 `imx662.json`，請將其複製到 libcamera 的資料目錄：

```bash
# 複製調校檔案到 vc4 和 pisp 目錄
cp /home/pi/imx662-v4l2-driver_6_12_y-binning-fix/imx662.json \
   /home/pi/camera_build/libcamera/src/ipa/rpi/vc4/data/imx662.json

cp /home/pi/imx662-v4l2-driver_6_12_y-binning-fix/imx662.json \
   /home/pi/camera_build/libcamera/src/ipa/rpi/pisp/data/imx662.json
```

**調校檔案說明：**
- `imx662.json` 包含 IMX662 感測器的專用影像處理參數
- 包括黑位準 (black_level: 3200)、增益曲線、白平衡、自動曝光等設定
- 支援 HDR、夜間模式等進階功能
- 針對 pisp 管線最佳化，提供最佳影像品質

**注意：** 如果沒有使用本調校檔案，系統會使用預設參數，但影像品質可能不如專用調校檔案。

### 步驟五：編譯 libcamera

配置並編譯 libcamera：

```bash
cd /home/pi/camera_build/libcamera

# 配置建置環境
meson setup build \
    --buildtype=release \
    -Dcam=disabled \
    -Dlc-compliance=disabled \
    -Dqcam=disabled \
    -Dtest=false \
    -Dv4l2=true \
    -Dpipelines=rpi/vc4,rpi/pisp \
    -Dipas=rpi/vc4,rpi/pisp

# 編譯
ninja -C build

# 安裝
sudo ninja -C build install

# 更新函式庫快取
sudo ldconfig
```

### 步驟六：編譯 rpicam-apps

編譯並安裝 rpicam-apps：

```bash
cd /home/pi/camera_build/rpicam-apps

# 配置建置環境
meson setup build \
    --buildtype=release \
    -Denable_libav=disabled \
    -Denable_drm=disabled \
    -Denable_egl=disabled \
    -Denable_qt=disabled \
    -Denable_opencv=disabled \
    -Denable_tflite=disabled

# 編譯
ninja -C build

# 安裝
sudo ninja -C build install
```

### 步驟七：驗證整合結果

完成編譯安裝後，測試 IMX662 是否能被 libcamera 正確識別：

```bash
# 檢查攝像頭偵測
rpicam-hello --list-cameras
```

成功的輸出應該類似：
```
Available cameras
-----------------
0 : imx662 [1920x1080 16-bit RGGB] (/base/axi/pcie@1000120000/rp1/i2c@88000/imx662@1a)
    Modes: 'SRGGB12_CSI2P' : 1936x1100 [90.00 fps - (0, 0)/1920x1080 crop]
           'SRGGB16' : 1928x1090 [25.00 fps - (0, 0)/1920x1080 crop]
                       3856x2180 [21.90 fps - (0, 0)/1920x1080 crop]
```

測試攝像頭功能：
```bash
# 5秒預覽測試
rpicam-hello --timeout 5000

# 拍攝測試照片
rpicam-still -o test_imx662.jpg --width 1920 --height 1080
```

### 整合完成檢查清單

- [ ] V4L2 驅動程式已安裝且可偵測攝像頭
- [ ] libcamera 編譯環境已設置
- [ ] IMX662 相機協助程式已添加到 libcamera
- [ ] libcamera 和 rpicam-apps 編譯安裝成功
- [ ] `rpicam-hello --list-cameras` 能偵測到 IMX662
- [ ] 能夠成功拍攝照片和預覽影片

### 自動化腳本

為了簡化整合過程，您可以建立一個自動化腳本：

```bash
#!/bin/bash
# IMX662 libcamera 自動整合腳本

set -e

echo "🚀 開始 IMX662 libcamera 整合..."

# 安裝編譯環境
echo "📦 安裝編譯相依套件..."
sudo apt update
sudo apt install -y git python3-pip python3-jinja2 python3-ply python3-yaml
sudo apt install -y ninja-build meson pkg-config cmake
sudo apt install -y libdrm-dev libexif-dev libjpeg-dev libtiff-dev
sudo apt install -y libboost-dev libqt5opengl5-dev libvulkan-dev libpng-dev

# 建立工作目錄
mkdir -p /home/pi/camera_build
cd /home/pi/camera_build

# 下載源碼
echo "📥 下載 libcamera 和 rpicam-apps 源碼..."
if [ ! -d "libcamera" ]; then
    git clone https://git.libcamera.org/libcamera/libcamera.git
fi

if [ ! -d "rpicam-apps" ]; then
    git clone https://github.com/raspberrypi/rpicam-apps.git
fi

# 整合 IMX662 支援
echo "🔧 整合 IMX662 支援..."
cp /home/pi/imx662-v4l2-driver_6_12_y-binning-fix/cam_helper_imx662.cpp \
   /home/pi/camera_build/libcamera/src/ipa/rpi/cam_helper/

# 複製調校檔案
echo "📄 複製 IMX662 調校檔案..."
cp /home/pi/imx662-v4l2-driver_6_12_y-binning-fix/imx662.json \
   /home/pi/camera_build/libcamera/src/ipa/rpi/vc4/data/imx662.json
cp /home/pi/imx662-v4l2-driver_6_12_y-binning-fix/imx662.json \
   /home/pi/camera_build/libcamera/src/ipa/rpi/pisp/data/imx662.json

# 編譯 libcamera
echo "🔨 編譯 libcamera..."
cd /home/pi/camera_build/libcamera
meson setup build --buildtype=release -Dcam=disabled -Dlc-compliance=disabled \
    -Dqcam=disabled -Dtest=false -Dv4l2=true \
    -Dpipelines=rpi/vc4,rpi/pisp -Dipas=rpi/vc4,rpi/pisp
ninja -C build
sudo ninja -C build install
sudo ldconfig

# 編譯 rpicam-apps
echo "🔨 編譯 rpicam-apps..."
cd /home/pi/camera_build/rpicam-apps
meson setup build --buildtype=release -Denable_libav=disabled \
    -Denable_drm=disabled -Denable_egl=disabled -Denable_qt=disabled \
    -Denable_opencv=disabled -Denable_tflite=disabled
ninja -C build
sudo ninja -C build install

echo "✅ IMX662 libcamera 整合完成！"
echo "🧪 執行測試："
echo "   rpicam-hello --list-cameras"
echo "   rpicam-hello --timeout 5000"
```

將此腳本儲存為 `integrate_libcamera.sh`，並執行：
```bash
chmod +x integrate_libcamera.sh
./integrate_libcamera.sh
```

### 注意事項

1. **編譯時間**：完整編譯過程可能需要 30-60 分鐘，取決於您的硬體性能
2. **空間需求**：建議至少 4GB 可用空間用於源碼和編譯輸出
3. **相依性**：確保 V4L2 驅動程式已正確安裝
4. **調校檔案**：如有專用調校檔案，影像品質會更佳
5. **系統相容性**：本流程在 Raspberry Pi OS Bookworm 上測試通過

## 🔧 疑難排解

### 問題 1：編譯失敗

**症狀：** `setup.sh` 執行時出現編譯錯誤

**診斷步驟：**
```bash
# 檢查編譯日誌
sudo cat /var/lib/dkms/imx662/0.0.1/build/make.log

# 檢查核心版本
uname -r

# 檢查標頭檔
ls /usr/src/linux-headers-$(uname -r)/include/linux/
```

**常見錯誤與解決方案：**

#### 錯誤：`fatal error: linux/unaligned.h: No such file or directory`
```bash
# 檢查替代標頭檔位置
find /usr/src/linux-headers-$(uname -r) -name "*unaligned*"
```
**解決方案：** 確保使用核心 6.12+

#### 錯誤：`implicit declaration of function 'v4l2_subdev_state_get_format'`
**原因：** 核心版本過舊
**解決方案：** 升級到核心 6.12 或更新版本

#### 錯誤：`No such file or directory: /lib/modules/.../build`
```bash
# 重新安裝核心標頭檔
sudo apt install --reinstall linux-headers-$(uname -r)
```

### 問題 2：攝像頭無法偵測

**症狀：** `libcamera-hello --list-cameras` 顯示 "No cameras available!"

**診斷步驟：**

#### 2.1 檢查硬體連接
```bash
# 檢查 I2C 匯流排
i2cdetect -l

# 掃描 I2C 裝置
i2cdetect -y 6
```
**正常狀況：** 應該在地址 `1a` 看到裝置

#### 2.2 檢查驅動程式狀態
```bash
# 檢查模組是否載入
lsmod | grep imx662

# 手動載入模組
sudo modprobe imx662

# 檢查模組參數
modinfo imx662
```

#### 2.3 檢查系統日誌
```bash
# 查看相關錯誤訊息
dmesg | grep -i imx662
dmesg | grep -i camera
dmesg | grep -i i2c

# 查看即時日誌
sudo journalctl -f
```

#### 2.4 檢查配置檔設定
```bash
# 確認配置正確
grep -E "(camera_auto_detect|dtoverlay)" /boot/firmware/config.txt

# 檢查裝置樹是否載入
ls /proc/device-tree/
```

### 問題 3：影像品質或性能問題

**症狀：** 攝像頭能偵測但影像品質差或幀率低

**調校步驟：**

#### 3.1 調整連結頻率
```bash
# 修改 /boot/firmware/config.txt
sudo nano /boot/firmware/config.txt

# 嘗試不同頻率設定
dtoverlay=imx662,cam0,link-frequency=891000000
```

#### 3.2 檢查系統性能
```bash
# 檢查 CPU 使用率
htop

# 檢查記憶體使用量
free -h

# 檢查溫度
vcgencmd measure_temp
```

#### 3.3 測試不同解析度
```bash
# 測試低解析度高幀率
libcamera-hello --list-cameras
libcamera-vid -t 10000 --width 1280 --height 720 --framerate 60
```

### 問題 4：系統穩定性問題

**症狀：** 系統當機或重啟

**檢查步驟：**
```bash
# 檢查電源供應
sudo dmesg | grep -i voltage

# 檢查溫度
watch -n 1 vcgencmd measure_temp

# 檢查記憶體錯誤
sudo dmesg | grep -i memory
```

**可能解決方案：**
- 使用更強的電源供應器（建議 5V 3A 以上）
- 改善散熱
- 降低連結頻率設定

### 進階除錯

#### 啟用詳細日誌
```bash
# 重新載入模組並啟用除錯
sudo modprobe -r imx662
sudo modprobe imx662 debug=1

# 查看詳細日誌
dmesg | tail -50
```

#### 手動編譯測試
```bash
cd /home/pi/imx662-v4l2-driver_6_12_y-binning-fix/
make clean
make
sudo insmod imx662.ko
```

## ⚠️ 已知問題與限制

### 核心版本限制
- **絕對必需**：Linux 核心 6.12 或更新版本
- **原因**：使用了新的 V4L2 API（`v4l2_subdev_state_get_format` 等）
- **解決方案**：升級到支援的核心版本

### 硬體限制
- **Raspberry Pi 4**：可能遇到性能限制，最高幀率受限
- **較舊型號**：不支援，缺少必要的硬體介面

### 軟體相容性
- **32-bit 系統**：不支援，需要 64-bit ARM 系統
- **舊版 libcamera**：可能遇到相容性問題

## 📞 技術支援

### 尋求協助前的準備

在尋求技術支援時，請準備以下資訊：

#### 系統資訊收集腳本
```bash
#!/bin/bash
echo "=== 系統資訊收集 ==="
echo "日期：$(date)"
echo
echo "=== 硬體資訊 ==="
cat /proc/device-tree/model
uname -a
echo
echo "=== 作業系統資訊 ==="
cat /etc/os-release
echo
echo "=== 驅動程式狀態 ==="
dkms status | grep imx662
lsmod | grep imx662
echo
echo "=== I2C 狀態 ==="
i2cdetect -y 6
echo
echo "=== 攝像頭偵測 ==="
libcamera-hello --list-cameras
echo
echo "=== 系統日誌 ==="
dmesg | grep -i imx662 | tail -10
```

將上述腳本儲存為 `collect_info.sh` 並執行：
```bash
chmod +x collect_info.sh
./collect_info.sh > system_info.txt
```

### 支援管道

1. **GitHub Issues**：提交詳細的問題報告
2. **Raspberry Pi 論壇**：硬體相關問題
3. **Linux 攝像頭社群**：驅動程式開發討論
4. **技術文檔**：參考官方 libcamera 文檔

記住提供完整的系統資訊和錯誤日誌，這將大大提高解決問題的效率！

## 參考資源 / References

- [Raspberry Pi Camera Software Documentation](https://www.raspberrypi.com/documentation/computers/camera_software.html)
- [libcamera Documentation](https://libcamera.org/)
- [V4L2 API Documentation](https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html)