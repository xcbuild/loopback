# Audio Loopback Driver

Audio Loopback Driver is an M1 compatible virtual audio loopback driver for macOS. It creates a virtual audio device that allows you to route audio between applications without needing a paid Apple Developer Program membership.

This driver supports macOS Apple Silicon (arm64) and macOS Intel (x86_64) platforms. It uses Apple's native AudioServerPlugIn API (HAL) to create a virtual audio device that appears in macOS like any other audio interface. The driver provides 2 channel audio at 48kHz sample rate (both configurable), with zero-latency loopback between input and output streams. Volume and mute controls are built-in for easy audio management.

To install, copy the driver bundle to the HAL plugins folder and restart CoreAudio:

```bash
sudo cp -R Driver.driver /Library/Audio/Plug-Ins/HAL/
sudo killall -9 coreaudiod
```

Once installed, open Audio MIDI Setup and you will see "Audio Loopback" as an available audio device. To use it, configure an app to output to "Audio Loopback" (like Safari or Spotify), then configure a recording app to input from "Audio Loopback" (like Audacity or GarageBand). Audio will flow from the output app directly to the input app through the virtual device.

To build from source:

```bash
xcodebuild -project Driver.xcodeproj -scheme Driver -configuration Release build CODE_SIGN_IDENTITY="-" CODE_SIGNING_REQUIRED=NO
```

To uninstall:

```bash
sudo rm -rf /Library/Audio/Plug-Ins/HAL/Driver.driver
sudo killall -9 coreaudiod
```