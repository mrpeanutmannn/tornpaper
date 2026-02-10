# Torn Paper - macOS Build Instructions

## Prerequisites

1. **Xcode** - Install from the Mac App Store (free)
2. **After Effects SDK** - Download from Adobe's website

## Setup

### 1. Set the SDK Path

Before building, you need to tell Xcode where your After Effects SDK is located.

**Option A: Set Environment Variable (Recommended)**

Open Terminal and run:
```bash
export AE_SDK_PATH="/path/to/your/AfterEffectsSDK"
```

For example, if you extracted the SDK to your Documents folder:
```bash
export AE_SDK_PATH="$HOME/Documents/AfterEffectsSDK"
```

To make this permanent, add the export line to your `~/.zshrc` file.

**Option B: Edit Project Settings**

1. Open `TornPaper.xcodeproj` in Xcode
2. Click on the project in the left sidebar
3. Select "Build Settings" tab
4. Search for "Header Search Paths"
5. Replace `$(AE_SDK_PATH)` with the actual path to your SDK

### 2. Build the Plugin

1. Open `TornPaper.xcodeproj` in Xcode
2. Select "Release" configuration (Product → Scheme → Edit Scheme → Build Configuration)
3. Build (Cmd+B)

The plugin will be built to:
`~/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/TornPaper.plugin`

This is the standard Adobe plug-ins folder, so After Effects should find it automatically.

### 3. Test

1. Launch After Effects
2. Create or open a composition
3. Apply the effect: Effect → Stylize → Torn Paper

## Troubleshooting

### "SDK headers not found"
Make sure `AE_SDK_PATH` is set correctly and points to the SDK folder containing the `Headers` subfolder.

### Plugin doesn't appear in After Effects
- Check that the .plugin file was built to the correct location
- Try restarting After Effects
- Check Console.app for any loading errors

### Build errors about missing types
The SDK requires specific versions of Xcode. If you get type errors, make sure you're using a compatible Xcode version.

## Universal Binary

This project is configured to build a Universal Binary that works on both:
- Intel Macs (x86_64)
- Apple Silicon Macs (arm64)

The `ARCHS = "$(ARCHS_STANDARD)"` setting handles this automatically.
