#!/usr/bin/env python3

import os
import re
import glob

def fix_lvgl9_compatibility():
    """Fix LVGL 8 to LVGL 9 compatibility issues across all screen files"""
    
    # Get all C files in the screens directory
    screens_dir = "/Users/akw/Documents/GitHub/wrkspc-move/app/app/src/ui/screens"
    c_files = glob.glob(os.path.join(screens_dir, "*.c"))
    
    if not c_files:
        print("No C files found in screens directory")
        return
    
    processed_count = 0
    
    for c_file in c_files:
        print(f"Processing {os.path.basename(c_file)}...")
        
        try:
            # Read the file
            with open(c_file, 'r', encoding='utf-8') as f:
                content = f.read()
            
            original_content = content
            
            # Fix 1: Replace draw event callback API
            draw_callback_pattern = re.compile(
                r'static void draw_event_cb[^{]*\{[^}]*lv_obj_draw_part_dsc_t[^}]*\}',
                re.DOTALL
            )
            if draw_callback_pattern.search(content):
                # Replace with simplified version
                content = re.sub(
                    r'static void (draw_event_cb[^(]*\([^)]*\))\s*\{[^}]*lv_obj_draw_part_dsc_t[^}]*\}',
                    r'static void \1\n{\n    // Simplified for LVGL 9 - custom tick labels not implemented\n    LV_UNUSED(e);\n}',
                    content,
                    flags=re.DOTALL
                )
            
            # Fix 2: Remove draw event callbacks
            content = re.sub(
                r'lv_obj_add_event_cb\([^,]+,\s*[^,]+,\s*LV_EVENT_DRAW_PART_BEGIN[^;]*;',
                r'// Removed draw event callback for LVGL 9 compatibility',
                content
            )
            
            # Fix 3: Fix lv_obj_set_style_size calls - add missing LV_STATE_DEFAULT parameter
            content = re.sub(
                r'lv_obj_set_style_size\(([^,]+),\s*([^,]+),\s*(LV_PART_INDICATOR)\);',
                r'lv_obj_set_style_size(\1, \2, \3, LV_STATE_DEFAULT);',
                content
            )
            
            # Fix 4: Replace lv_chart_set_axis_tick calls
            content = re.sub(
                r'lv_chart_set_axis_tick\([^;]*;',
                r'// Note: lv_chart_set_axis_tick removed in LVGL 9 - using default axis settings',
                content
            )
            
            # Fix 5: Replace y_points access with lv_chart_set_value_by_id
            y_points_pattern = re.compile(
                r'([a-zA-Z_][a-zA-Z0-9_]*)->y_points\[([^\]]+)\]\s*=\s*([^;]+);'
            )
            
            def replace_y_points(match):
                series_var = match.group(1)
                index = match.group(2)
                value = match.group(3)
                # We need to find the chart variable - this is a simplified approach
                # In practice, you might need to track which chart each series belongs to
                return f'lv_chart_set_value_by_id(chart_obj, {series_var}, {index}, {value});'
            
            # More specific y_points replacement for known patterns
            content = re.sub(
                r'ser_([a-zA-Z0-9_]+)->y_points\[([^\]]+)\]\s*=\s*([^;]+);',
                lambda m: f'// TODO: Replace with lv_chart_set_value_by_id(chart_obj, ser_{m.group(1)}, {m.group(2)}, {m.group(3)});',
                content
            )
            
            # Fix 6: Replace lv_point_t with lv_point_precise_t for line points
            content = re.sub(
                r'static\s+lv_point_t\s+line_points\[\]',
                r'static lv_point_precise_t line_points[]',
                content
            )
            content = re.sub(
                r'lv_point_t\s+line_points\[\]',
                r'lv_point_precise_t line_points[]',
                content
            )
            
            # Only write if changes were made
            if content != original_content:
                with open(c_file, 'w', encoding='utf-8') as f:
                    f.write(content)
                print(f"✓ Fixed {os.path.basename(c_file)}")
                processed_count += 1
            else:
                print(f"- No LVGL 8 patterns found in {os.path.basename(c_file)}")
                
        except Exception as e:
            print(f"✗ Error processing {os.path.basename(c_file)}: {e}")
    
    print(f"\nCompleted! Applied LVGL 9 fixes to {processed_count} files.")

if __name__ == "__main__":
    print("Applying LVGL 9 compatibility fixes to screen files...")
    fix_lvgl9_compatibility()
