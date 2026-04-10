#!/bin/bash
# Build script for Audio Loopback Driver

set -e

echo "Building Audio Loopback Driver..."
xcodebuild -project Driver.xcodeproj -scheme Driver -configuration Release build CODE_SIGN_IDENTITY="-" CODE_SIGNING_REQUIRED=NO

DERIVED_DATA=$(xcodebuild -project Driver.xcodeproj -scheme Driver -configuration Release -showBuildSettings 2>/dev/null | grep "DERIVED_FILE_PATH" | awk '{print $3}' | sed 's/\/Build.*//')
echo ""
echo "Build complete!"
echo "Driver location: $DERIVED_DATA/Build/Products/Release/Driver.driver"
echo ""
echo "To install:"
echo "  sudo cp -R $DERIVED_DATA/Build/Products/Release/Driver.driver /Library/Audio/Plug-Ins/HAL/"
echo "  sudo killall -9 coreaudiod"