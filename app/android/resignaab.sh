#!/bin/bash
# Sample usage is as follows;
# ./signapk myapp.apk styluslabs.keystore styluslabs
#
# param1, APK file: Calculator_debug.apk
# param2, keystore location: ~/.android/debug.keystore
set -x

BUILD_TOOLS="$HOME/android-sdk/build-tools/34.0.0"

# use my debug key default
AAB=$1
KEYSTORE=$2
KEYALIAS=$3

# get the filename
AAB_BASENAME=$(basename $AAB)
SIGNED_AAB="signed_"$AAB_BASENAME

# delete META-INF folder
zip -d $AAB META-INF/\*

$BUILD_TOOLS/zipalign -v -p 4 $AAB $SIGNED_AAB

jarsigner -verbose -sigalg SHA256withRSA -digestalg SHA-256 -keystore $KEYSTORE $SIGNED_AAB $KEYALIAS

jarsigner -verbose -verify $SIGNED_AAB
