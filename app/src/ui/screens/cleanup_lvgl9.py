#!/usr/bin/env python3

import os
import re
import glob

def cleanup_lvgl9_issues():
    """Clean up remaining LVGL 9 compatibility issues"""
    
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
            
            # Comment out problematic style_width/height calls with too many arguments
            content = re.sub(
                r'(\s*)lv_obj_set_style_(width|height)\([^;]+LV_PART_INDICATOR[^;]+LV_STATE_DEFAULT[^;]*\);',
                r'\1// LVGL 9: Chart point styling changed - commented out\n\1// lv_obj_set_style_\2(...);',
                content
            )
            
            # Remove unused draw event callback functions
            content = re.sub(
                r'static void draw_event_cb[^{]*\{\s*// Simplified for LVGL 9[^}]*\}\s*\n',
                '',
                content,
                flags=re.DOTALL
            )
            
            # Only write if changes were made
            if content != original_content:
                with open(c_file, 'w', encoding='utf-8') as f:
                    f.write(content)
                print(f"✓ Cleaned up {os.path.basename(c_file)}")
                processed_count += 1
                
        except Exception as e:
            print(f"✗ Error processing {os.path.basename(c_file)}: {e}")
    
    print(f"\nCleaned up {processed_count} files.")

if __name__ == "__main__":
    print("Cleaning up remaining LVGL 9 issues...")
    cleanup_lvgl9_issues()
