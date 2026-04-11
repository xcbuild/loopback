#!/bin/bash
# Build script for Audio Loopback Driver

set -e

echo "Building Audio Loopback Driver..."

# coreaudiod runs as arm64e on Apple Silicon on newer macOS versions, so build an arm64e slice by default.
# Override via ARCHS_OVERRIDE if needed (e.g. ARCHS_OVERRIDE=arm64 or ARCHS_OVERRIDE=x86_64).
HOST_ARCH="$(uname -m)"
DEFAULT_ARCHS=""
if [[ "$HOST_ARCH" == "arm64" ]]; then
  DEFAULT_ARCHS="arm64e"
elif [[ "$HOST_ARCH" == "x86_64" ]]; then
  DEFAULT_ARCHS="x86_64"
fi

ARCHS_TO_BUILD="${ARCHS_OVERRIDE:-$DEFAULT_ARCHS}"

XCODEBUILD_ARGS=(
  -project Driver.xcodeproj
  -scheme Driver
  -configuration Release
  build
  CODE_SIGN_IDENTITY=-
  CODE_SIGN_STYLE=Manual
  CODE_SIGNING_REQUIRED=NO
  DEVELOPMENT_TEAM=""
  PROVISIONING_PROFILE_SPECIFIER=""
)

if [[ -n "$ARCHS_TO_BUILD" ]]; then
  XCODEBUILD_ARGS+=(ARCHS="$ARCHS_TO_BUILD")
fi

xcodebuild "${XCODEBUILD_ARGS[@]}"

DERIVED_DATA=$(xcodebuild -project Driver.xcodeproj -scheme Driver -configuration Release -showBuildSettings 2>/dev/null | grep "DERIVED_FILE_PATH" | awk '{print $3}' | sed 's/\/Build.*//')
echo ""
echo "Build complete!"
echo "Driver location: $DERIVED_DATA/Build/Products/Release/Driver.driver"
echo ""
echo "To install:"
echo "  sudo cp -R $DERIVED_DATA/Build/Products/Release/Driver.driver /Library/Audio/Plug-Ins/HAL/"
echo "  sudo killall -9 coreaudiod"
