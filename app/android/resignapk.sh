#!/bin/bash
# Sample usage is as follows;
# ./signapk myapp.apk styluslabs.keystore styluslabs
#
# param1, APK file: Calculator_debug.apk
# param2, keystore location: ~/.android/debug.keystore
set -x

BUILD_TOOLS="${ANDROID_HOME:-$HOME/android-sdk}/build-tools/34.0.0"

# use my debug key default
APK=$1
KEYSTORE=$2

# get the filename
APK_BASENAME=$(basename $APK)
SIGNED_APK="signed_"$APK_BASENAME

# don't modify APK contents (e.g. by deleting META-INF/ or rerunning zipalign) so that signature can be
#  copied to an unsigned rebuild for reproducible build verification; apksigner replaces existing signature
$BUILD_TOOLS/zipalign -c -p 4 $APK || exit 1

$BUILD_TOOLS/apksigner sign --ks $KEYSTORE --out $SIGNED_APK $APK

$BUILD_TOOLS/apksigner verify $SIGNED_APK
