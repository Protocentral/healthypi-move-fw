 #!/bin/bash

# Script to convert each PNG file individually using LVGLImage.py
# with options --ofmt C --cf I8

LVGL_SCRIPT="~/zephyrproject/modules/lib/gui/lvgl/scripts/LVGLImage.py"

# Check if the LVGLImage.py script exists
if [ ! -f "$(eval echo $LVGL_SCRIPT)" ]; then
    echo "Error: LVGLImage.py script not found at $LVGL_SCRIPT"
    exit 1
fi

# Process each PNG file individually
for png_file in *.png; do
    if [ -f "$png_file" ]; then
        echo "Converting $png_file..."
        python3 $(eval echo $LVGL_SCRIPT) --ofmt C --cf I4 -o . "$png_file"
        
        if [ $? -eq 0 ]; then
            echo "Successfully converted $png_file"
        else
            echo "Error converting $png_file"
        fi
    fi
done

echo "Conversion complete!"