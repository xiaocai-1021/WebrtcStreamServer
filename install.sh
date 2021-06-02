#!/bin/bash -e
SCRIPT=`pwd`/$0
PATHNAME=`dirname $SCRIPT`
THIRDPARTY_DIR=$PATHNAME/thirdparty_dir
THIRDPARTY_ROOT_DIR=`pwd`

BUILD_LIB_DIR=$THIRDPARTY_DIR/build
PREFIX_DIR=$THIRDPARTY_DIR

SUDO=""

if [[ $EUID -ne 0 ]]; then
  SUDO="sudo -E"
fi

echo $PREFIX_DIR
mkdir -p $PREFIX_DIR

${SUDO} apt-get update -y
${SUDO} apt-get install make gcc g++ python-dev libglib2.0-dev docbook2x pkg-config nasm yasm m4 autoconf libtool liblzma-dev libbz2-dev automake cmake unzip wget -y

rm -rf $BUILD_LIB_DIR

install_openssl() {
  if [ -d $BUILD_LIB_DIR ]; then
    cd $BUILD_LIB_DIR
    cp $PATHNAME/3rdparty/openssl-OpenSSL_1_1_1j.tar.gz $BUILD_LIB_DIR
    tar zxvf openssl-OpenSSL_1_1_1j.tar.gz
    cd openssl-OpenSSL_1_1_1j
    ./config no-ssl3 --prefix=$PREFIX_DIR -fPIC no-shared --libdir=lib
    make
    make install
    cd $THIRDPARTY_ROOT_DIR
  else
    mkdir -p $BUILD_LIB_DIR
    install_openssl
  fi
}

install_libsrtp2(){
  if [ -d $BUILD_LIB_DIR ]; then
    cd $BUILD_LIB_DIR
    cp $PATHNAME/3rdparty/libsrtp-2.3.0.tar.gz $BUILD_LIB_DIR
    tar -zxvf libsrtp-2.3.0.tar.gz
    cd libsrtp-2.3.0
    CFLAGS="-fPIC" ./configure --enable-openssl --prefix=$PREFIX_DIR --with-openssl-dir=$PREFIX_DIR
    make && make uninstall && make install
    cd $THIRDPARTY_ROOT_DIR
  else
    mkdir -p $BUILD_LIB_DIR
    install_libsrtp2
  fi
}

install_json_hpp() {
  if [ -d $BUILD_LIB_DIR ]; then
    cp $PATHNAME/3rdparty/nlohmann_json_3_9_1_include.zip $BUILD_LIB_DIR
    cd $BUILD_LIB_DIR
    unzip nlohmann_json_3_9_1_include.zip
    mkdir -p $PREFIX_DIR/include/nlohmann
    cp -a single_include/nlohmann/json.hpp $PREFIX_DIR/include/nlohmann
    cd $THIRDPARTY_ROOT_DIR
  else
    mkdir -p $BUILD_LIB_DIR
    install_json_hpp
  fi
}

install_spdlog() {
  if [ -d $BUILD_LIB_DIR ]; then
    cp $PATHNAME/3rdparty/spdlog-1.8.5.tar.gz $BUILD_LIB_DIR
    cd $BUILD_LIB_DIR
    tar zxvf spdlog-1.8.5.tar.gz
    cd spdlog-1.8.5
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR ..
    make
    make install
    cd $THIRDPARTY_ROOT_DIR
  else
    mkdir -p $BUILD_LIB_DIR
    install_spdlog
  fi
}

install_libsdptransform() {
  if [ -d $BUILD_LIB_DIR ]; then
    cp $PATHNAME/3rdparty/libsdptransform-1.2.9.tar.gz $BUILD_LIB_DIR
    cd $BUILD_LIB_DIR
    tar zxvf libsdptransform-1.2.9.tar.gz
    cd libsdptransform-1.2.9
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR ..
    make
    make install
    cd $THIRDPARTY_ROOT_DIR
  else
    mkdir -p $BUILD_LIB_DIR
    install_libsdptransform
  fi
}

install_opus() {
  if [ -d $BUILD_LIB_DIR ]; then
    cp $PATHNAME/3rdparty/opus-1.3.1.tar.gz $BUILD_LIB_DIR
    cd $BUILD_LIB_DIR
    tar zxvf opus-1.3.1.tar.gz
    cd opus-1.3.1
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=$PREFIX_DIR ..
    make
    make install
    cd $THIRDPARTY_ROOT_DIR
  else
    mkdir -p $BUILD_LIB_DIR
    install_opus
  fi
}

install_ffmpeg(){
  if [ -d $BUILD_LIB_DIR ]; then
    cd $BUILD_LIB_DIR
    cp $PATHNAME/3rdparty/FFmpeg-n4.4.tar.gz $BUILD_LIB_DIR
    tar -zxvf FFmpeg-n4.4.tar.gz
    cd FFmpeg-n4.4
    ./configure --prefix=$PREFIX_DIR
    make && make uninstall && make install
    cd $THIRDPARTY_ROOT_DIR
  else
    mkdir -p $BUILD_LIB_DIR
    install_ffmpeg
  fi
}

install_boost() {
  if [ -d $BUILD_LIB_DIR ]; then
    cd $BUILD_LIB_DIR
    wget https://boostorg.jfrog.io/artifactory/main/release/1.76.0/source/boost_1_76_0.zip
    unzip boost_1_76_0.zip
    cd boost_1_76_0
    chmod +x bootstrap.sh
    ./bootstrap.sh
    ./b2 && ./b2 install --prefix=$PREFIX_DIR
  else
    mkdir -p $BUILD_LIB_DIR
    install_boost
  fi
}

install_spdlog
install_json_hpp
install_openssl
install_libsrtp2
install_libsdptransform
install_opus
install_ffmpeg
install_boost
