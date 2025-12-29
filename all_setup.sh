#!/bin/bash
# IMX662 Camera Complete Auto-Installation Script
# Includes V4L2 driver + full libcamera integration
# Author: Liu
# Version: 1.0

set -e  # Exit immediately on error

# Color definitions
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Save source directory (where the script and source files are located)
SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Logging functions
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

# Check if running as root
check_root() {
    if [[ $EUID -eq 0 ]]; then
        log_error "Please do not run this script as root"
        log_info "Correct usage: ./all_setup.sh"
        exit 1
    fi
}

# Check system requirements
check_requirements() {
    log_step "Checking system requirements..."

    # Check operating system
    if ! grep -q "Raspberry Pi OS" /etc/os-release 2>/dev/null; then
        log_warning "Raspberry Pi OS not detected, continuing but compatibility issues may occur"
    fi

    # Check kernel version
    KERNEL_VERSION=$(uname -r | cut -d. -f1,2)
    KERNEL_MAJOR=$(echo $KERNEL_VERSION | cut -d. -f1)
    KERNEL_MINOR=$(echo $KERNEL_VERSION | cut -d. -f2)

    if [[ $KERNEL_MAJOR -lt 6 ]] || [[ $KERNEL_MAJOR -eq 6 && $KERNEL_MINOR -lt 12 ]]; then
        log_error "Kernel version $KERNEL_VERSION is too old, requires 6.12 or newer"
        log_info "Please run the following commands to upgrade:"
        log_info "sudo apt update && sudo apt full-upgrade"
        log_info "sudo reboot"
        exit 1
    fi

    log_success "Kernel version $KERNEL_VERSION meets requirements"

    # Check available space
    AVAILABLE_SPACE=$(df /home | tail -1 | awk '{print $4}')
    if [[ $AVAILABLE_SPACE -lt 4194304 ]]; then  # 4GB in KB
        log_warning "Less than 4GB available space, may affect compilation"
    fi

    log_success "System requirements check completed"
}

# Update packages and install dependencies
install_dependencies() {
    log_step "Installing build dependencies..."

    # Update package list
    log_info "Updating package list..."
    sudo apt update

    # Install V4L2 driver build dependencies
    log_info "Installing V4L2 driver dependencies..."
    sudo apt install -y linux-headers-rpi-2712 dkms git build-essential

    # Install libcamera build dependencies
    log_info "Installing libcamera build dependencies..."
    sudo apt install -y python3-pip python3-jinja2 python3-ply python3-yaml
    sudo apt install -y ninja-build meson pkg-config cmake
    sudo apt install -y libdrm-dev libexif-dev libjpeg-dev libtiff-dev
    sudo apt install -y libboost-dev libqt5opengl5-dev libvulkan-dev libpng-dev

    log_success "All dependencies installed"
}

# Install V4L2 driver
install_v4l2_driver() {
    log_step "Installing IMX662 V4L2 driver..."

    # Ensure we're in the correct directory
    cd "$SOURCE_DIR"

    # Check required files
    if [[ ! -f "setup.sh" ]]; then
        log_error "setup.sh file not found"
        exit 1
    fi

    if [[ ! -f "imx662.c" ]]; then
        log_error "imx662.c driver source not found"
        exit 1
    fi

    # Execute driver installation
    log_info "Executing V4L2 driver build and installation..."
    sudo ./setup.sh

    log_success "V4L2 driver installation completed"
}

# Configure boot settings
configure_boot() {
    log_step "Configuring boot settings..."

    # Determine config.txt location
    if [[ -f "/boot/firmware/config.txt" ]]; then
        CONFIG_FILE="/boot/firmware/config.txt"
    elif [[ -f "/boot/config.txt" ]]; then
        CONFIG_FILE="/boot/config.txt"
    else
        log_error "config.txt file not found"
        exit 1
    fi

    log_info "Using config file: $CONFIG_FILE"

    # Backup original config file
    BACKUP_FILE="$CONFIG_FILE.backup.$(date +%Y%m%d_%H%M%S)"
    sudo cp "$CONFIG_FILE" "$BACKUP_FILE"
    log_info "Original config file backed up to: $BACKUP_FILE"

    # Check and modify settings
    if grep -q "camera_auto_detect=0" "$CONFIG_FILE"; then
        log_info "camera_auto_detect=0 already exists"
    else
        log_info "Adding camera_auto_detect=0"
        echo "camera_auto_detect=0" | sudo tee -a "$CONFIG_FILE" > /dev/null
    fi

    if grep -q "dtoverlay=imx662" "$CONFIG_FILE"; then
        log_info "dtoverlay=imx662 already exists"
    else
        log_info "Adding dtoverlay=imx662,cam0"
        echo "dtoverlay=imx662,cam0" | sudo tee -a "$CONFIG_FILE" > /dev/null
    fi

    log_success "Boot configuration completed"
}

# Setup libcamera build environment
setup_libcamera_build() {
    log_step "Setting up libcamera build environment..."

    # Create build directory
    BUILD_DIR="$HOME/camera_build"
    if [[ -d "$BUILD_DIR" ]]; then
        log_warning "Build directory already exists, will clean and rebuild"
        rm -rf "$BUILD_DIR"
    fi

    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    log_info "Working directory: $BUILD_DIR"

    # Download libcamera source
    log_info "Downloading libcamera source..."
    if ! git clone https://git.libcamera.org/libcamera/libcamera.git; then
        log_error "libcamera source download failed"
        exit 1
    fi

    # Download rpicam-apps source
    log_info "Downloading rpicam-apps source..."
    if ! git clone https://github.com/raspberrypi/rpicam-apps.git; then
        log_error "rpicam-apps source download failed"
        exit 1
    fi

    log_success "libcamera build environment setup completed"
}

# Apply patches to libcamera and rpicam-apps
apply_patches() {
    log_step "Applying patches for kernel 6.12+ compatibility..."

    BUILD_DIR="$HOME/camera_build"

    # Check if patches directory exists
    if [[ ! -d "$SOURCE_DIR/patches" ]]; then
        log_error "Patches directory not found at $SOURCE_DIR/patches"
        exit 1
    fi

    # Apply pisp.patch to libcamera
    log_info "Applying pisp.patch (device node naming fix)..."
    cd "$BUILD_DIR/libcamera"
    if patch -p1 --dry-run < "$SOURCE_DIR/patches/pisp.patch" 2>/dev/null; then
        patch -p1 < "$SOURCE_DIR/patches/pisp.patch"
        log_success "pisp.patch applied"
    else
        log_warning "pisp.patch already applied or failed to apply"
    fi

    # Apply camera_sensor_properties.patch
    log_info "Applying camera_sensor_properties.patch..."
    if patch -p1 --dry-run < "$SOURCE_DIR/patches/camera_sensor_properties.patch" 2>/dev/null; then
        patch -p1 < "$SOURCE_DIR/patches/camera_sensor_properties.patch"
        log_success "camera_sensor_properties.patch applied"
    else
        log_warning "camera_sensor_properties.patch already applied or failed to apply"
    fi

    # Apply meson_data.patch
    log_info "Applying meson_data.patch..."
    if patch -p1 --dry-run < "$SOURCE_DIR/patches/meson_data.patch" 2>/dev/null; then
        patch -p1 < "$SOURCE_DIR/patches/meson_data.patch"
        log_success "meson_data.patch applied"
    else
        log_warning "meson_data.patch already applied or failed to apply"
    fi

    # Apply rpicam_apps_libcamera060.patch
    log_info "Applying rpicam_apps_libcamera060.patch..."
    cd "$BUILD_DIR/rpicam-apps"
    if patch -p1 --dry-run < "$SOURCE_DIR/patches/rpicam_apps_libcamera060.patch" 2>/dev/null; then
        patch -p1 < "$SOURCE_DIR/patches/rpicam_apps_libcamera060.patch"
        log_success "rpicam_apps_libcamera060.patch applied"
    else
        log_warning "rpicam_apps_libcamera060.patch already applied or failed to apply"
    fi

    log_success "All patches applied"
}

# Integrate IMX662 support into libcamera
integrate_imx662_support() {
    log_step "Integrating IMX662 support into libcamera..."

    BUILD_DIR="$HOME/camera_build"

    # Copy camera helper file
    log_info "Copying IMX662 camera helper..."
    if [[ ! -f "$SOURCE_DIR/cam_helper_imx662.cpp" ]]; then
        log_error "cam_helper_imx662.cpp file not found"
        exit 1
    fi

    cp "$SOURCE_DIR/cam_helper_imx662.cpp" \
       "$BUILD_DIR/libcamera/src/ipa/rpi/cam_helper/"

    # Modify meson.build file
    log_info "Modifying meson.build configuration..."
    MESON_FILE="$BUILD_DIR/libcamera/src/ipa/rpi/cam_helper/meson.build"

    if ! grep -q "cam_helper_imx662.cpp" "$MESON_FILE"; then
        # Add imx662 after imx519
        sed -i "/cam_helper_imx519.cpp/a\\    'cam_helper_imx662.cpp'," "$MESON_FILE"
        log_info "Added cam_helper_imx662.cpp to build list"
    else
        log_info "cam_helper_imx662.cpp already exists in build list"
    fi

    # Copy tuning file
    log_info "Copying IMX662 tuning file..."
    if [[ ! -f "$SOURCE_DIR/imx662.json" ]]; then
        log_error "imx662.json tuning file not found"
        exit 1
    fi

    cp "$SOURCE_DIR/imx662.json" \
       "$BUILD_DIR/libcamera/src/ipa/rpi/vc4/data/imx662.json"
    cp "$SOURCE_DIR/imx662.json" \
       "$BUILD_DIR/libcamera/src/ipa/rpi/pisp/data/imx662.json"

    log_success "IMX662 support integration completed"
}

# Build libcamera
build_libcamera() {
    log_step "Building libcamera..."

    BUILD_DIR="$HOME/camera_build"
    cd "$BUILD_DIR/libcamera"

    # Configure build environment
    log_info "Configuring libcamera build environment..."
    meson setup build \
        --buildtype=release \
        -Dcam=disabled \
        -Dlc-compliance=disabled \
        -Dqcam=disabled \
        -Dtest=false \
        -Dv4l2=true \
        -Dpipelines=rpi/vc4,rpi/pisp \
        -Dipas=rpi/vc4,rpi/pisp

    # Start build
    log_info "Starting libcamera build (this may take 20-40 minutes)..."
    ninja -C build

    # Install
    log_info "Installing libcamera..."
    sudo ninja -C build install

    # Install IMX662 tuning files to system directories
    log_info "Installing IMX662 tuning files..."
    sudo cp "$SOURCE_DIR/imx662.json" /usr/local/share/libcamera/ipa/rpi/pisp/
    sudo cp "$SOURCE_DIR/imx662.json" /usr/local/share/libcamera/ipa/rpi/vc4/

    # Update library cache
    sudo ldconfig

    log_success "libcamera build and installation completed"
}

# Build rpicam-apps
build_rpicam_apps() {
    log_step "Building rpicam-apps..."

    BUILD_DIR="$HOME/camera_build"
    cd "$BUILD_DIR/rpicam-apps"

    # Configure build environment
    log_info "Configuring rpicam-apps build environment..."
    meson setup build \
        --buildtype=release \
        -Denable_libav=disabled \
        -Denable_drm=disabled \
        -Denable_egl=disabled \
        -Denable_qt=disabled \
        -Denable_opencv=disabled \
        -Denable_tflite=disabled

    # Start build
    log_info "Starting rpicam-apps build (this may take 10-20 minutes)..."
    ninja -C build

    # Install
    log_info "Installing rpicam-apps..."
    sudo ninja -C build install

    log_success "rpicam-apps build and installation completed"
}

# Verify installation
verify_installation() {
    log_step "Verifying installation..."

    # Check V4L2 driver
    log_info "Checking V4L2 driver..."
    if lsmod | grep -q imx662; then
        log_success "IMX662 V4L2 driver is loaded"
    else
        log_warning "IMX662 V4L2 driver not loaded, may need reboot"
    fi

    # Check DKMS status
    if dkms status | grep -q imx662; then
        log_success "IMX662 driver found in DKMS"
    else
        log_warning "IMX662 driver not found in DKMS"
    fi

    # Check libcamera commands
    log_info "Checking libcamera commands..."
    if command -v rpicam-hello >/dev/null; then
        log_success "rpicam-hello command available"
    else
        log_error "rpicam-hello command not available"
    fi

    if command -v rpicam-still >/dev/null; then
        log_success "rpicam-still command available"
    else
        log_error "rpicam-still command not available"
    fi

    # Check camera helper file
    if [[ -f "/usr/local/lib/aarch64-linux-gnu/libcamera/ipa/ipa_rpi_pisp.so" ]]; then
        log_success "libcamera IPA module installed"
    else
        log_warning "libcamera IPA module not found"
    fi

    # Check tuning file
    if [[ -f "/usr/local/share/libcamera/ipa/rpi/pisp/imx662.json" ]]; then
        log_success "IMX662 tuning file installed"
    else
        log_warning "IMX662 tuning file not found"
    fi

    log_success "Installation verification completed"
}

# Show next steps
show_next_steps() {
    log_step "Installation complete! Next steps:"
    echo ""
    echo -e "${CYAN}=== Important Notes ===${NC}"
    echo -e "${YELLOW}1. Reboot the system to load all settings:${NC}"
    echo "   sudo reboot"
    echo ""
    echo -e "${YELLOW}2. Test the camera after reboot:${NC}"
    echo "   rpicam-hello --list-cameras"
    echo "   rpicam-hello --timeout 5000"
    echo "   rpicam-still -o test.jpg"
    echo ""
    echo -e "${YELLOW}3. Expected camera detection result:${NC}"
    echo "   Available cameras"
    echo "   -----------------"
    echo "   0 : imx662 [1920x1080 16-bit RGGB] (/base/axi/pcie@1000120000/rp1/i2c@88000/imx662@1a)"
    echo ""
    echo -e "${YELLOW}4. If you encounter issues:${NC}"
    echo "   - Check camera ribbon cable connection"
    echo "   - Check /boot/firmware/config.txt settings"
    echo "   - View system log: dmesg | grep imx662"
    echo ""
    echo -e "${GREEN}IMX662 camera complete installation finished!${NC}"
}

# Main function
main() {
    echo -e "${CYAN}"
    echo "========================================"
    echo "   Sony IMX662 Camera Complete Setup"
    echo "========================================"
    echo "Includes:"
    echo "- V4L2 kernel driver"
    echo "- Full libcamera integration"
    echo "- rpicam-apps applications"
    echo "- Automatic configuration and verification"
    echo "========================================"
    echo -e "${NC}"

    # Ask for user confirmation
    read -p "Start installation? This process may take 60-90 minutes (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_info "Installation cancelled"
        exit 0
    fi

    # Record start time
    START_TIME=$(date +%s)

    # Execute installation steps
    check_root
    check_requirements
    install_dependencies
    install_v4l2_driver
    configure_boot
    setup_libcamera_build
    apply_patches
    integrate_imx662_support
    build_libcamera
    build_rpicam_apps
    verify_installation

    # Calculate total time
    END_TIME=$(date +%s)
    DURATION=$((END_TIME - START_TIME))
    MINUTES=$((DURATION / 60))
    SECONDS=$((DURATION % 60))

    echo ""
    log_success "All installation steps completed! Total time: ${MINUTES} min ${SECONDS} sec"

    show_next_steps
}

# Check if script is being executed directly
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    main "$@"
fi
