# Font Download Instructions for NTerminal

This directory contains embedded programming font files.

## Current Status

### Downloaded Fonts (Available)
- CascadiaCode.ttf (648 KB) - Microsoft open source font
- JetBrainsMono-Regular.ttf (274 KB) - JetBrains open source font

### Missing Fonts (Optional)
- FiraCode-Regular.ttf
- SourceCodePro-Regular.ttf

## Quick Download

### Method 1: Using PowerShell Script

Run the following command in the project root directory:

```powershell
powershell -ExecutionPolicy Bypass -File simple_download_fonts.ps1
```

Or use the full download script:

```powershell
powershell -ExecutionPolicy Bypass -File download_fonts.ps1
```

### Method 2: Manual Download

Download fonts from the following links:

1. **Fira Code**
   - URL: https://github.com/tonsky/FiraCode/releases
   - Download: Fira_Code_6.2.zip
   - Extract: ttf/FiraCode-Regular.ttf

2. **Source Code Pro**
   - URL: https://github.com/adobe-fonts/source-code-pro/releases
   - Download: SourceCodePro-2.038R-ro_1.058R-it.zip
   - Extract: Fonts/SourceCodePro-Regular.ttf

## Enable Fonts in Project

After downloading font files:

1. Place the `.ttf` files in this directory (`resources/fonts/`)

2. Edit `resources/resources.qrc` and uncomment the font file lines:
   ```xml
   <file>fonts/FiraCode-Regular.ttf</file>
   <file>fonts/SourceCodePro-Regular.ttf</file>
   ```

3. Rebuild the project:
   - In Qt Creator: Build → Rebuild All
   - Or run: qmake && mingw32-make

## Font Information

### Cascadia Code (Included)
- **License**: SIL Open Font License 1.1
- **Features**: Microsoft terminal default font, supports ligatures

### JetBrains Mono (Included)
- **License**: SIL Open Font License 1.1
- **Features**: Designed for reading code

### Fira Code (Optional)
- **License**: SIL Open Font License 1.1
- **Features**: Popular programming font with ligatures

### Source Code Pro (Optional)
- **License**: SIL Open Font License 1.1
- **Features**: Adobe's classic programming font

## Notes

- Font files are optional. The program will use system fonts if no embedded fonts are found.
- All recommended fonts use the SIL Open Font License and can be freely used and distributed.
