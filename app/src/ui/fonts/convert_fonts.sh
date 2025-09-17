#!/bin/bash

# LVGL Font Conversion Script for HealthyPi Move
# Based on recommendations from google-fonts-recommendations.md

set -e

# Paths
FONT_DIR="/Users/akw/Documents/GitHub/healthypi-move-workspace/healthypi-move-fw/app/src/ui/fonts"
TTF_DIR="$FONT_DIR/ttf"
LVGL_DIR="$FONT_DIR/lvgl"
CONVERTER="$FONT_DIR/lv_font_conv/lv_font_conv.js"

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
node "$CONVERTER" \
    --font "$TTF_DIR/Inter-Regular.ttf" \
    --size 16 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --output "$LVGL_DIR/inter_regular_16.c"

# Inter SemiBold 18px - Metric values (steps, HR)  
echo "Converting Inter SemiBold 18px..."
node "$CONVERTER" \
    --font "$TTF_DIR/Inter-SemiBold.ttf" \
    --size 18 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --output "$LVGL_DIR/inter_semibold_18.c"

# JetBrains Mono Regular 16px - Time display
echo "Converting JetBrains Mono Regular 16px..."
node "$CONVERTER" \
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
node "$CONVERTER" \
    --font "$TTF_DIR/Inter-Regular.ttf" \
    --size 14 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --output "$LVGL_DIR/inter_regular_14.c"

# Inter Regular 12px - Small text, captions
echo "Converting Inter Regular 12px..."
node "$CONVERTER" \
    --font "$TTF_DIR/Inter-Regular.ttf" \
    --size 12 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --output "$LVGL_DIR/inter_regular_12.c"

# JetBrains Mono Regular 24px - Large time display
echo "Converting JetBrains Mono Regular 24px..."
node "$CONVERTER" \
    --font "$TTF_DIR/JetBrainsMono-Regular.ttf" \
    --size 24 \
    --bpp 4 \
    --range 0x20-0x7F \
    --format lvgl \
    --output "$LVGL_DIR/jetbrains_mono_regular_24.c"

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
echo "  LV_FONT_DECLARE(jetbrains_mono_regular_16);"
echo ""
echo "Use in styles:"
echo "  lv_obj_set_style_text_font(obj, &inter_regular_16, LV_PART_MAIN);"