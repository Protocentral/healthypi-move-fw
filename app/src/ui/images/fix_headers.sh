#!/bin/bash

# Script to replace complex LVGL header inclusion with simple #include <lvgl.h>
# in all C files in the images directory

echo "Fixing LVGL headers in C files..."

# Counter for processed files
count=0

# Process each C file in the current directory
for c_file in *.c; do
    if [ -f "$c_file" ]; then
        echo "Processing $c_file..."
        
        # Create a temporary file
        temp_file=$(mktemp)
        
        # Use sed to replace the header block
        # This will match from the #if defined(LV_LVGL_H_INCLUDE_SIMPLE) line
        # to the #endif line and replace it with #include <lvgl.h>
        sed '/#if defined(LV_LVGL_H_INCLUDE_SIMPLE)/,/#endif/{
            /#if defined(LV_LVGL_H_INCLUDE_SIMPLE)/c\
#include <lvgl.h>
            /#endif/d
            /^#include "lvgl.h"$/d
            /^#elif defined(LV_LVGL_H_INCLUDE_SYSTEM)$/d
            /^#include <lvgl.h>$/d
            /^#elif defined(LV_BUILD_TEST)$/d
            /^#include "..\/lvgl.h"$/d
            /^#else$/d
            /^#include "lvgl\/lvgl.h"$/d
        }' "$c_file" > "$temp_file"
        
        # Replace the original file with the modified version
        mv "$temp_file" "$c_file"
        
        ((count++))
        echo "Fixed $c_file"
    fi
done

echo "Completed! Fixed headers in $count C files."
