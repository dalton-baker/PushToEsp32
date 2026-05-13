# Web UI Files

This folder contains the static assets served by the ESP32's web server (uploaded to LittleFS).

## Files
- `index.html` - Single-page web UI: position, location, time, alignment, finalize
- `stars.json` - Star catalog used by the alignment star picker

## Uploading to ESP32

The files need to be uploaded to the ESP32's LittleFS filesystem:

### Method 1: Using PlatformIO CLI
```bash
pio run --target uploadfs --environment seeed_xiao_esp32s3
```

### Method 2: Using VS Code PlatformIO Extension
1. Click on the PlatformIO icon in the left sidebar
2. Expand your environment (`seeed_xiao_esp32s3`)
3. Under "Platform", click **Upload Filesystem Image**

## Editing

Edit `index.html` directly with full syntax highlighting. After making changes, run `uploadfs` again to push them to the device. No firmware rebuild required for UI-only changes.
