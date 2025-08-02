#!/bin/bash

# refs:
# - Github Copilot GPT-4o ... basic skeleton
# - https://stackoverflow.com/questions/53733781
# - https://issuetracker.google.com/issues/240007217 - SIGSEGV signaling in ART

set -o xtrace

NDK="$HOME/android-sdk/ndk/26.3.11579264"
# lldb.sh setss LD_LIBRARY_PATH so required libpython3.10 is found
LLDB_PATH="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/lldb.sh"
LLDB_SERVER_PATH="$NDK/toolchains/llvm/prebuilt/linux-x86_64/lib/clang/17/lib/linux/aarch64/lldb-server"

# Define the package name of the Android app and the device's architecture
PACKAGE_NAME="com.styluslabs.maps"
DEVICE_ARCH="arm64-v8a"

# Define the path where lldb-server will be uploaded on the device
LLDB_SERVER_DEVICE_PATH="/data/data/$PACKAGE_NAME/lldb-server"
LLDB_SERVER_UPLOAD_PATH="/data/local/tmp/lldb-server"

# Path to the unstripped version of the native library on the host machine
UNSTRIPPED_LIBRARY_PATH="$HOME/maps/explore/build/AndroidRelease/libdroidmaps.so"

# Function to upload lldb-server to the device
upload_lldb_server() {
    adb push "$LLDB_SERVER_PATH" "$LLDB_SERVER_UPLOAD_PATH"
    adb shell "chmod +x $LLDB_SERVER_UPLOAD_PATH"
    echo "Uploaded lldb-server to the device."
}

# Function to start lldb-server on the device
start_lldb_server() {
    adb shell "run-as $PACKAGE_NAME cp $LLDB_SERVER_UPLOAD_PATH $LLDB_SERVER_DEVICE_PATH"
    adb shell "run-as $PACKAGE_NAME killall -9 lldb-server"
    adb shell "run-as $PACKAGE_NAME sh -c '$LLDB_SERVER_DEVICE_PATH platform --server --listen unix-abstract:///data/data/$PACKAGE_NAME/debug.socket'" &  #*:12345 &" &
    echo "Started lldb-server on the device."
}

# Function to forward the device port to the host
forward_port() {
    adb forward tcp:12345 tcp:12345
    echo "Forwarded port 12345 from device to host."
}

# Function to get the PID of the running app
get_app_pid() {
    local pid
    pid=$(adb shell pidof "$PACKAGE_NAME")
    echo "$pid"
}

# Main script
upload_lldb_server
start_lldb_server
#forward_port
PID=$(get_app_pid)

if [ -z "$PID" ]; then
    echo "Failed to find PID of the app. Make sure the app is running."
    exit 1
fi

echo "Found PID of the app: $PID"

# Start LLDB and attach to the running app
# ... seems to have stopped working with script - have to start lldb and run commands manually; try without '-b' arg?
#platform connect connect://localhost:12345  -- using TCP works sometimes, but usually hangs, requiring toggling of USB debugging on device to reset
# Android devs thought it would be a good idea to use SIGSEGV for routine signaling in ART ... WTF?
"$LLDB_PATH" -b <<EOF
platform select remote-android
platform connect unix-abstract-connect:///data/data/$PACKAGE_NAME/debug.socket
process handle SIGSEGV --pass true --stop false --notify true
target create $UNSTRIPPED_LIBRARY_PATH
process attach --pid $PID
EOF
