#!/bin/bash
# IMX662 攝像頭完整自動安裝腳本
# 包含 V4L2 驅動程式 + libcamera 完整整合
# 作者: AI Assistant
# 版本: 1.0

set -e  # 遇到錯誤立即退出

# 顏色定義
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 日誌函式
log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

log_step() {
    echo -e "${PURPLE}[STEP]${NC} $1"
}

# 檢查是否為 root 用戶
check_root() {
    if [[ $EUID -eq 0 ]]; then
        log_error "請不要以 root 用戶執行此腳本"
        log_info "正確用法: ./all_setup.sh"
        exit 1
    fi
}

# 檢查系統需求
check_requirements() {
    log_step "檢查系統需求..."
    
    # 檢查作業系統
    if ! grep -q "Raspberry Pi OS" /etc/os-release 2>/dev/null; then
        log_warning "未檢測到 Raspberry Pi OS，繼續安裝但可能出現相容性問題"
    fi
    
    # 檢查核心版本
    KERNEL_VERSION=$(uname -r | cut -d. -f1,2)
    KERNEL_MAJOR=$(echo $KERNEL_VERSION | cut -d. -f1)
    KERNEL_MINOR=$(echo $KERNEL_VERSION | cut -d. -f2)
    
    if [[ $KERNEL_MAJOR -lt 6 ]] || [[ $KERNEL_MAJOR -eq 6 && $KERNEL_MINOR -lt 12 ]]; then
        log_error "核心版本 $KERNEL_VERSION 過舊，需要 6.12 或更新版本"
        log_info "請執行以下指令升級系統："
        log_info "sudo apt update && sudo apt full-upgrade"
        log_info "sudo reboot"
        exit 1
    fi
    
    log_success "核心版本 $KERNEL_VERSION 符合需求"
    
    # 檢查可用空間
    AVAILABLE_SPACE=$(df /home | tail -1 | awk '{print $4}')
    if [[ $AVAILABLE_SPACE -lt 4194304 ]]; then  # 4GB in KB
        log_warning "可用空間少於 4GB，可能影響編譯過程"
    fi
    
    log_success "系統需求檢查完成"
}

# 更新套件並安裝相依性
install_dependencies() {
    log_step "安裝編譯相依套件..."
    
    # 更新套件清單
    log_info "更新套件清單..."
    sudo apt update
    
    # 安裝 V4L2 驅動程式編譯相依性
    log_info "安裝 V4L2 驅動程式相依套件..."
    sudo apt install -y linux-headers dkms git build-essential
    
    # 安裝 libcamera 編譯相依性
    log_info "安裝 libcamera 編譯相依套件..."
    sudo apt install -y python3-pip python3-jinja2 python3-ply python3-yaml
    sudo apt install -y ninja-build meson pkg-config cmake
    sudo apt install -y libdrm-dev libexif-dev libjpeg-dev libtiff-dev
    sudo apt install -y libboost-dev libqt5opengl5-dev libvulkan-dev libpng-dev
    
    log_success "所有相依套件安裝完成"
}

# 安裝 V4L2 驅動程式
install_v4l2_driver() {
    log_step "安裝 IMX662 V4L2 驅動程式..."
    
    # 確保在正確的目錄
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    cd "$SCRIPT_DIR"
    
    # 檢查必要檔案
    if [[ ! -f "setup.sh" ]]; then
        log_error "找不到 setup.sh 檔案"
        exit 1
    fi
    
    if [[ ! -f "imx662.c" ]]; then
        log_error "找不到 imx662.c 驅動程式源碼"
        exit 1
    fi
    
    # 執行驅動程式安裝
    log_info "執行 V4L2 驅動程式編譯安裝..."
    sudo ./setup.sh
    
    log_success "V4L2 驅動程式安裝完成"
}

# 配置開機設定
configure_boot() {
    log_step "配置開機設定..."
    
    # 判斷 config.txt 位置
    if [[ -f "/boot/firmware/config.txt" ]]; then
        CONFIG_FILE="/boot/firmware/config.txt"
    elif [[ -f "/boot/config.txt" ]]; then
        CONFIG_FILE="/boot/config.txt"
    else
        log_error "找不到 config.txt 檔案"
        exit 1
    fi
    
    log_info "使用設定檔: $CONFIG_FILE"
    
    # 備份原始設定檔
    BACKUP_FILE="$CONFIG_FILE.backup.$(date +%Y%m%d_%H%M%S)"
    sudo cp "$CONFIG_FILE" "$BACKUP_FILE"
    log_info "原始設定檔已備份到: $BACKUP_FILE"
    
    # 檢查並修改設定
    if grep -q "camera_auto_detect=0" "$CONFIG_FILE"; then
        log_info "camera_auto_detect=0 已存在"
    else
        log_info "添加 camera_auto_detect=0"
        echo "camera_auto_detect=0" | sudo tee -a "$CONFIG_FILE" > /dev/null
    fi
    
    if grep -q "dtoverlay=imx662" "$CONFIG_FILE"; then
        log_info "dtoverlay=imx662 已存在"
    else
        log_info "添加 dtoverlay=imx662,cam0"
        echo "dtoverlay=imx662,cam0" | sudo tee -a "$CONFIG_FILE" > /dev/null
    fi
    
    log_success "開機設定配置完成"
}

# 設置 libcamera 編譯環境
setup_libcamera_build() {
    log_step "設置 libcamera 編譯環境..."
    
    # 建立工作目錄
    BUILD_DIR="/home/pi/camera_build"
    if [[ -d "$BUILD_DIR" ]]; then
        log_warning "編譯目錄已存在，將清理重建"
        rm -rf "$BUILD_DIR"
    fi
    
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    log_info "工作目錄: $BUILD_DIR"
    
    # 下載 libcamera 源碼
    log_info "下載 libcamera 源碼..."
    if ! git clone https://git.libcamera.org/libcamera/libcamera.git; then
        log_error "libcamera 源碼下載失敗"
        exit 1
    fi
    
    # 下載 rpicam-apps 源碼
    log_info "下載 rpicam-apps 源碼..."
    if ! git clone https://github.com/raspberrypi/rpicam-apps.git; then
        log_error "rpicam-apps 源碼下載失敗"
        exit 1
    fi
    
    log_success "libcamera 編譯環境設置完成"
}

# 整合 IMX662 支援到 libcamera
integrate_imx662_support() {
    log_step "整合 IMX662 支援到 libcamera..."
    
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    BUILD_DIR="/home/pi/camera_build"
    
    # 複製相機協助程式檔案
    log_info "複製 IMX662 相機協助程式..."
    if [[ ! -f "$SCRIPT_DIR/cam_helper_imx662.cpp" ]]; then
        log_error "找不到 cam_helper_imx662.cpp 檔案"
        exit 1
    fi
    
    cp "$SCRIPT_DIR/cam_helper_imx662.cpp" \
       "$BUILD_DIR/libcamera/src/ipa/rpi/cam_helper/"
    
    # 修改 meson.build 檔案
    log_info "修改 meson.build 建置設定..."
    MESON_FILE="$BUILD_DIR/libcamera/src/ipa/rpi/cam_helper/meson.build"
    
    if ! grep -q "cam_helper_imx662.cpp" "$MESON_FILE"; then
        # 在 imx519 後面添加 imx662
        sed -i "/cam_helper_imx519.cpp/a\\    'cam_helper_imx662.cpp'," "$MESON_FILE"
        log_info "已添加 cam_helper_imx662.cpp 到建置清單"
    else
        log_info "cam_helper_imx662.cpp 已存在於建置清單"
    fi
    
    # 複製調校檔案
    log_info "複製 IMX662 調校檔案..."
    if [[ ! -f "$SCRIPT_DIR/imx662.json" ]]; then
        log_error "找不到 imx662.json 調校檔案"
        exit 1
    fi
    
    cp "$SCRIPT_DIR/imx662.json" \
       "$BUILD_DIR/libcamera/src/ipa/rpi/vc4/data/imx662.json"
    cp "$SCRIPT_DIR/imx662.json" \
       "$BUILD_DIR/libcamera/src/ipa/rpi/pisp/data/imx662.json"
    
    log_success "IMX662 支援整合完成"
}

# 編譯 libcamera
build_libcamera() {
    log_step "編譯 libcamera..."
    
    BUILD_DIR="/home/pi/camera_build"
    cd "$BUILD_DIR/libcamera"
    
    # 配置建置環境
    log_info "配置 libcamera 建置環境..."
    meson setup build \
        --buildtype=release \
        -Dcam=disabled \
        -Dlc-compliance=disabled \
        -Dqcam=disabled \
        -Dtest=false \
        -Dv4l2=true \
        -Dpipelines=rpi/vc4,rpi/pisp \
        -Dipas=rpi/vc4,rpi/pisp
    
    # 開始編譯
    log_info "開始編譯 libcamera（這可能需要 20-40 分鐘）..."
    ninja -C build
    
    # 安裝
    log_info "安裝 libcamera..."
    sudo ninja -C build install
    
    # 更新函式庫快取
    sudo ldconfig
    
    log_success "libcamera 編譯安裝完成"
}

# 編譯 rpicam-apps
build_rpicam_apps() {
    log_step "編譯 rpicam-apps..."
    
    BUILD_DIR="/home/pi/camera_build"
    cd "$BUILD_DIR/rpicam-apps"
    
    # 配置建置環境
    log_info "配置 rpicam-apps 建置環境..."
    meson setup build \
        --buildtype=release \
        -Denable_libav=disabled \
        -Denable_drm=disabled \
        -Denable_egl=disabled \
        -Denable_qt=disabled \
        -Denable_opencv=disabled \
        -Denable_tflite=disabled
    
    # 開始編譯
    log_info "開始編譯 rpicam-apps（這可能需要 10-20 分鐘）..."
    ninja -C build
    
    # 安裝
    log_info "安裝 rpicam-apps..."
    sudo ninja -C build install
    
    log_success "rpicam-apps 編譯安裝完成"
}

# 驗證安裝結果
verify_installation() {
    log_step "驗證安裝結果..."
    
    # 檢查 V4L2 驅動程式
    log_info "檢查 V4L2 驅動程式..."
    if lsmod | grep -q imx662; then
        log_success "IMX662 V4L2 驅動程式已載入"
    else
        log_warning "IMX662 V4L2 驅動程式未載入，可能需要重啟"
    fi
    
    # 檢查 DKMS 狀態
    if dkms status | grep -q imx662; then
        log_success "DKMS 中發現 IMX662 驅動程式"
    else
        log_warning "DKMS 中未發現 IMX662 驅動程式"
    fi
    
    # 檢查 libcamera 指令
    log_info "檢查 libcamera 相關指令..."
    if command -v rpicam-hello >/dev/null; then
        log_success "rpicam-hello 指令可用"
    else
        log_error "rpicam-hello 指令不可用"
    fi
    
    if command -v rpicam-still >/dev/null; then
        log_success "rpicam-still 指令可用"
    else
        log_error "rpicam-still 指令不可用"
    fi
    
    # 檢查相機協助程式檔案
    if [[ -f "/usr/local/lib/aarch64-linux-gnu/libcamera/ipa/ipa_rpi_pisp.so" ]]; then
        log_success "libcamera IPA 模組已安裝"
    else
        log_warning "libcamera IPA 模組未找到"
    fi
    
    # 檢查調校檔案
    if [[ -f "/usr/local/share/libcamera/ipa/rpi/pisp/imx662.json" ]]; then
        log_success "IMX662 調校檔案已安裝"
    else
        log_warning "IMX662 調校檔案未找到"
    fi
    
    log_success "安裝驗證完成"
}

# 顯示後續步驟
show_next_steps() {
    log_step "安裝完成！後續步驟："
    echo ""
    echo -e "${CYAN}=== 重要提醒 ===${NC}"
    echo -e "${YELLOW}1. 重新啟動系統以載入所有設定：${NC}"
    echo "   sudo reboot"
    echo ""
    echo -e "${YELLOW}2. 重啟後測試攝像頭：${NC}"
    echo "   rpicam-hello --list-cameras"
    echo "   rpicam-hello --timeout 5000"
    echo "   rpicam-still -o test.jpg"
    echo ""
    echo -e "${YELLOW}3. 預期的攝像頭偵測結果：${NC}"
    echo "   Available cameras"
    echo "   -----------------"
    echo "   0 : imx662 [1920x1080 16-bit RGGB] (/base/axi/pcie@1000120000/rp1/i2c@88000/imx662@1a)"
    echo ""
    echo -e "${YELLOW}4. 如果遇到問題：${NC}"
    echo "   - 檢查攝像頭排線連接"
    echo "   - 檢查 /boot/firmware/config.txt 設定"
    echo "   - 查看系統日誌: dmesg | grep imx662"
    echo ""
    echo -e "${GREEN}🎉 IMX662 攝像頭完整安裝完成！${NC}"
}

# 主函式
main() {
    echo -e "${CYAN}"
    echo "========================================"
    echo "   Sony IMX662 攝像頭完整安裝腳本"
    echo "========================================"
    echo "包含："
    echo "• V4L2 核心驅動程式"
    echo "• libcamera 完整整合"
    echo "• rpicam-apps 應用程式"
    echo "• 自動配置和驗證"
    echo "========================================"
    echo -e "${NC}"
    
    # 詢問用戶確認
    read -p "是否開始安裝？這個過程可能需要 60-90 分鐘 (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_info "安裝已取消"
        exit 0
    fi
    
    # 記錄開始時間
    START_TIME=$(date +%s)
    
    # 執行安裝步驟
    check_root
    check_requirements
    install_dependencies
    install_v4l2_driver
    configure_boot
    setup_libcamera_build
    integrate_imx662_support
    build_libcamera
    build_rpicam_apps
    verify_installation
    
    # 計算總時間
    END_TIME=$(date +%s)
    DURATION=$((END_TIME - START_TIME))
    MINUTES=$((DURATION / 60))
    SECONDS=$((DURATION % 60))
    
    echo ""
    log_success "所有安裝步驟完成！總耗時: ${MINUTES} 分 ${SECONDS} 秒"
    
    show_next_steps
}

# 檢查是否直接執行腳本
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
