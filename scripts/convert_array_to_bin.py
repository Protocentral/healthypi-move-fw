#!/usr/bin/env python3
"""
Script to convert C array files to binary files for littlefs storage.
This reduces flash usage by storing firmware data in the filesystem instead of flash.
"""

import re
import sys
import os

def extract_array_from_c_file(filename):
    """Extract array data from C file and return as bytes."""
    with open(filename, 'r') as f:
        content = f.read()
    
    # Find the array definition
    array_match = re.search(r'const\s+uint8_t\s+\w+\[\d+\]\s*=\s*\{([^}]+)\}', content, re.DOTALL)
    if not array_match:
        raise ValueError(f"Could not find array definition in {filename}")
    
    array_content = array_match.group(1)
    
    # Extract hex values
    hex_values = re.findall(r'0x([0-9a-fA-F]{1,2})', array_content)
    
    # Convert to bytes
    byte_data = bytes([int(val, 16) for val in hex_values])
    
    return byte_data

def main():
    if len(sys.argv) != 3:
        print("Usage: python3 convert_array_to_bin.py <input_c_file> <output_bin_file>")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    if not os.path.exists(input_file):
        print(f"Error: Input file {input_file} does not exist")
        sys.exit(1)
    
    try:
        # Extract array data
        print(f"Extracting array data from {input_file}...")
        byte_data = extract_array_from_c_file(input_file)
        
        # Write binary file
        print(f"Writing {len(byte_data)} bytes to {output_file}...")
        with open(output_file, 'wb') as f:
            f.write(byte_data)
        
        print(f"Successfully converted array to binary file: {output_file}")
        print(f"Size: {len(byte_data)} bytes")
        
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
