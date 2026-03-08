# Web UI Files

This folder contains the HTML files served by the ESP32 captive portal.

## Files
- `index.html` - Main dashboard page
- `config.html` - Site configuration with auto-detect location feature
- `align.html` - 2-star alignment interface
- `diagnostics.html` - System diagnostics

## Uploading to ESP32

The HTML files need to be uploaded to the ESP32's LittleFS filesystem:

### Method 1: Using PlatformIO (Recommended)
```bash
# Upload the filesystem
pio run --target uploadfs --environment seeed_xiao_esp32s3
```

### Method 2: Using VS Code PlatformIO Extension
1. Click on the PlatformIO icon in the left sidebar
2. Expand your environment (seeed_xiao_esp32s3)
3. Under "Platform", click "Upload Filesystem Image"

## Editing Files

You can now edit these HTML files directly with full syntax highlighting and they will be served by the ESP32 after uploading the filesystem.

**Note:** After making changes to these files, you need to run `uploadfs` again to update the files on the ESP32.

## Benefits of This Approach

✅ Proper syntax highlighting when editing  
✅ Easier to maintain and debug  
✅ Smaller compiled firmware size  
✅ Separate CSS/JS files possible  
✅ Can update UI without recompiling firmware (just re-upload filesystem)
