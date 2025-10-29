#!/usr/bin/env python3

import os
import re
import glob

def fix_lvgl_headers():
    """Replace complex LVGL header inclusion with simple #include <lvgl.h>"""
    
    # Pattern to match the entire header block
    header_pattern = re.compile(
        r'#if defined\(LV_LVGL_H_INCLUDE_SIMPLE\).*?#endif',
        re.DOTALL | re.MULTILINE
    )
    
    # Get all C files in current directory
    c_files = glob.glob("*.c")
    
    if not c_files:
        print("No C files found in current directory")
        return
    
    processed_count = 0
    
    for c_file in c_files:
        print(f"Processing {c_file}...")
        
        try:
            # Read the file
            with open(c_file, 'r', encoding='utf-8') as f:
                content = f.read()
            
            # Check if the pattern exists
            if header_pattern.search(content):
                # Replace the header block with simple include
                new_content = header_pattern.sub('#include <lvgl.h>', content)
                
                # Write back to file
                with open(c_file, 'w', encoding='utf-8') as f:
                    f.write(new_content)
                
                print(f"✓ Fixed {c_file}")
                processed_count += 1
            else:
                print(f"- No LVGL header block found in {c_file}")
                
        except Exception as e:
            print(f"✗ Error processing {c_file}: {e}")
    
    print(f"\nCompleted! Fixed headers in {processed_count} C files.")

if __name__ == "__main__":
    print("Fixing LVGL headers in C files...")
    fix_lvgl_headers()
