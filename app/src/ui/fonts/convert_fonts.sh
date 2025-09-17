#!/bin/bash

# LVGL Font Conversion Script for HealthyPi Move
# Based on recommendations from google-fonts-recommendations.md

set -e

# Paths
FONT_DIR="/Users/akw/Documents/GitHub/wrkspc-move/healthypi-move-fw/app/src/ui/fonts"
TTF_DIR="$FONT_DIR/ttf"
LVGL_DIR="$FONT_DIR/lvgl"
CONVERTER="lv_font_conv"

# Ensure output directory exists
mkdir -p "$LVGL_DIR"

echo "🎨 Converting fonts for HealthyPi Move AMOLED Display..."
echo "======================================================"

# Phase 1: Core System Fonts (Essential)
echo ""
echo "📱 Phase 1: Core System Fonts"
echo "------------------------------"

# Inter Regular 16px - General UI text
echo "Converting Inter Regular 16px..."
$CONVERTER \
    --no-compress \
    --font "$TTF_DIR/Inter-Regular.ttf" \
    --size 16 \
    --range 0x20-0x7F \
    --format lvgl \
    --bpp 4 \
    --lv-font-name inter_regular_16 \
    --output "$LVGL_DIR/inter_regular_16.c"

# Inter SemiBold 18px - Metric values (steps, HR)  
echo "Converting Inter SemiBold 18px..."
$CONVERTER \
    --font "$TTF_DIR/Inter-SemiBold.ttf" \
    --size 18 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --output "$LVGL_DIR/inter_semibold_18.c"

# JetBrains Mono Regular 16px - Time display
echo "Converting JetBrains Mono Regular 16px..."
$CONVERTER \
    --font "$TTF_DIR/JetBrainsMono-Regular.ttf" \
    --size 16 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --output "$LVGL_DIR/jetbrains_mono_regular_16.c"

# Additional essential sizes
echo ""
echo "📱 Phase 2: Additional Essential Sizes"
echo "---------------------------------------"

# Inter Regular 14px - Secondary text
echo "Converting Inter Regular 14px..."
$CONVERTER \
    --font "$TTF_DIR/Inter-Regular.ttf" \
    --size 14 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --output "$LVGL_DIR/inter_regular_14.c"

# Inter Regular 12px - Small text, captions
echo "Converting Inter Regular 12px..."
$CONVERTER \
    --font "$TTF_DIR/Inter-Regular.ttf" \
    --size 12 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --output "$LVGL_DIR/inter_regular_12.c"

# JetBrains Mono Regular 24px - Large time display
echo "Converting JetBrains Mono Regular 24px..."
$CONVERTER \
    --font "$TTF_DIR/JetBrainsMono-Regular.ttf" \
    --size 24 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --output "$LVGL_DIR/jetbrains_mono_regular_24.c"

# Inter Regular 24px - Large body text, headlines
echo "Converting Inter Regular 24px..."
$CONVERTER \
    --no-compress \
    --font "$TTF_DIR/Inter-Regular.ttf" \
    --size 24 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --lv-font-name inter_regular_24 \
    --output "$LVGL_DIR/inter_regular_24.c"

# Inter SemiBold 24px - Large emphasis text, metric values
echo "Converting Inter SemiBold 24px..."
$CONVERTER \
    --no-compress \
    --font "$TTF_DIR/Inter-SemiBold.ttf" \
    --size 24 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --lv-font-name inter_semibold_24 \
    --output "$LVGL_DIR/inter_semibold_24.c"

# Inter SemiBold 80px - Large time display (digits only for minimal size)
echo "Converting Inter SemiBold 80px for time display..."
$CONVERTER \
    --no-compress \
    --font "$TTF_DIR/Inter-SemiBold.ttf" \
    --size 80 \
    --bpp 4 \
    --range 0x20,0x30-0x39,0x3A,0x41,0x4D,0x50 \
    --format lvgl \
    --lv-include lvgl.h \
    --lv-font-name inter_semibold_80_time \
    --output "$LVGL_DIR/inter_semibold_80_time.c"

echo ""
echo "✅ Font conversion complete!"
echo ""
echo "📊 Generated LVGL fonts:"
echo "========================"
ls -la "$LVGL_DIR"/*.c

echo ""
echo "🎯 Usage in your code:"
echo "======================"
echo "Add to move_ui.h:"
echo "  LV_FONT_DECLARE(inter_regular_16);"
echo "  LV_FONT_DECLARE(inter_semibold_18);"
echo "  LV_FONT_DECLARE(inter_semibold_80_time);  // Large time display"
echo "  LV_FONT_DECLARE(jetbrains_mono_regular_16);"
echo ""
echo "Use in styles:"
echo "  lv_obj_set_style_text_font(obj, &inter_regular_16, LV_PART_MAIN);"