#!/usr/bin/env python3

import os
import re
import glob

def fix_style_size_api():
    """Fix lv_obj_set_style_size API changes in LVGL 9"""
    
    # Get all C files in the screens directory
    screens_dir = "/Users/akw/Documents/GitHub/wrkspc-move/app/app/src/ui/screens"
    c_files = glob.glob(os.path.join(screens_dir, "*.c"))
    
    processed_count = 0
    
    for c_file in c_files:
        try:
            # Read the file
            with open(c_file, 'r', encoding='utf-8') as f:
                content = f.read()
            
            original_content = content
            
            # Fix lv_obj_set_style_size - replace with width/height for charts
            # This pattern matches lv_obj_set_style_size for chart indicators
            content = re.sub(
                r'lv_obj_set_style_size\(([^,]+),\s*([^,]+),\s*LV_PART_INDICATOR,\s*LV_STATE_DEFAULT\);',
                lambda m: f'lv_obj_set_style_width({m.group(1)}, {m.group(2)}, LV_PART_INDICATOR, LV_STATE_DEFAULT);\n    lv_obj_set_style_height({m.group(1)}, {m.group(2)}, LV_PART_INDICATOR, LV_STATE_DEFAULT);',
                content
            )
            
            # Only write if changes were made
            if content != original_content:
                with open(c_file, 'w', encoding='utf-8') as f:
                    f.write(content)
                print(f"✓ Fixed style_size API in {os.path.basename(c_file)}")
                processed_count += 1
                
        except Exception as e:
            print(f"✗ Error processing {os.path.basename(c_file)}: {e}")
    
    print(f"\nFixed style_size API in {processed_count} files.")

if __name__ == "__main__":
    print("Fixing lv_obj_set_style_size API calls...")
    fix_style_size_api()
