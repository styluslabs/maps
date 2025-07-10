#!/bin/sh
set -e
SCRIPTPATH="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"

if ! command -v desktop-file-edit >/dev/null 2>&1 ; then
  echo "\nIf you would like to create a desktop entry, please install the desktop-file-utils package and run this script again"
else
  DESKTOPFILE=$SCRIPTPATH/Maps.desktop
  desktop-file-edit --set-key=Exec --set-value=$SCRIPTPATH/../ascend $DESKTOPFILE
  desktop-file-edit --set-key=Icon --set-value=$SCRIPTPATH/Maps144x144.png $DESKTOPFILE
  #sudo desktop-file-install $DESKTOPFILE  # to install for all users
  desktop-file-install --dir=$HOME/.local/share/applications $DESKTOPFILE
  echo "\nCreated desktop entry for Maps"
fi

echo "*** Setup succeeded ***"
