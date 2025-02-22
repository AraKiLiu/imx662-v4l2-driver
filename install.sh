#!/usr/bin/bash

sudo apt install linux-headers dkms git
./setup.sh

# building libcamera
cd ~/

sudo apt install -y libcamera-dev libepoxy-dev libjpeg-dev libtiff5-dev libpng-dev
sudo apt install -y qtbase5-dev libqt5core5a libqt5gui5 libqt5widgets5
sudo apt install libavcodec-dev libavdevice-dev libavformat-dev libswresample-dev

sudo apt install -y python3-pip git python3-jinja2

sudo apt install -y libboost-dev
sudo apt install -y libgnutls28-dev openssl libtiff5-dev pybind11-dev
sudo apt install -y qtbase5-dev libqt5core5a libqt5gui5 libqt5widgets5
sudo apt install -y meson cmake
sudo apt install -y python3-yaml python3-ply
sudo apt install -y libglib2.0-dev libgstreamer-plugins-base1.0-dev

git clone https://github.com/raspberrypi/libcamera.git

cd libcamera

meson setup build --buildtype=release -Dpipelines=rpi/vc4,rpi/pisp -Dipas=rpi/vc4,rpi/pisp -Dv4l2=true -Dgstreamer=enabled -Dtest=false -Dlc-compliance=disabled -Dcam=disabled -Dqcam=disabled -Ddocumentation=disabled -Dpycamera=enabled

cp ~/imx662-v4l2-driver/cam_helper_imx662.cpp ~/libcamera/src/ipa/rpi/cam_helper/.
cp ~/imx662-v4l2-driver/meson.build ~/libcamera/src/ipa/rpi/cam_helper/.
cp ~/libcamera/src/ipa/rpi/vc4/data/imx290.json ~/libcamera/src/ipa/rpi/vc4/data/imx662.json
cp ~/imx662-v4l2-driver/meson.build.vc4 ~/libcamera/src/ipa/rpi/vc4/data/meson.build
cp ~/libcamera/src/ipa/rpi/pisp/data/imx290.json ~/libcamera/src/ipa/rpi/pisp/data/imx662.json
cp ~/imx662-v4l2-driver/meson.build.pisp ~/libcamera/src/ipa/rpi/pisp/data/meson.build

ninja -C build
sudo ninja -C build install

# building rpicam-apps
cd ~/

sudo apt install -y cmake libboost-program-options-dev libdrm-dev libexif-dev
sudo apt install -y meson ninja-build

git clone https://github.com/raspberrypi/rpicam-apps.git

cd rpicam-apps

meson setup build -Denable_libav=enabled -Denable_drm=enabled -Denable_egl=enabled -Denable_qt=enabled -Denable_opencv=disabled -Denable_tflite=disabled

# Lite OS
# meson setup build -Denable_libav=disabled -Denable_drm=enabled -Denable_egl=disabled -Denable_qt=disabled -Denable_opencv=disabled -Denable_tflite=disabled

meson compile -C build
sudo meson install -C build
sudo ldconfig

sudo cp /boot/firmware/config.txt /boot/firmware/config.txt.backup
sudo cp ~/imx662-v4l2-driver/config.txt /boot/firmware/.
# sudo reboot
