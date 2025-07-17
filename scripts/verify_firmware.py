#!/usr/bin/env python3
"""
Script to verify that the binary files match the original C arrays.
This ensures the conversion was successful.
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

def verify_files():
    """Verify that binary files match C arrays."""
    
    # File pairs to verify
    file_pairs = [
        ("lib/max32664_updater/msbl/max32664c_30_13_31.c", "app/firmware/max32664c_30_13_31.bin"),
        ("lib/max32664_updater/msbl/max32664d_40_6_0.c", "app/firmware/max32664d_40_6_0.bin")
    ]
    
    all_match = True
    
    for c_file, bin_file in file_pairs:
        print(f"Verifying {c_file} vs {bin_file}...")
        
        if not os.path.exists(c_file):
            print(f"  ERROR: C file not found: {c_file}")
            all_match = False
            continue
            
        if not os.path.exists(bin_file):
            print(f"  ERROR: Binary file not found: {bin_file}")
            all_match = False
            continue
        
        try:
            # Extract array data
            array_data = extract_array_from_c_file(c_file)
            
            # Read binary file
            with open(bin_file, 'rb') as f:
                bin_data = f.read()
            
            # Compare
            if array_data == bin_data:
                print(f"  ✓ MATCH: {len(array_data)} bytes")
            else:
                print(f"  ✗ MISMATCH: Array={len(array_data)} bytes, Binary={len(bin_data)} bytes")
                all_match = False
                
                # Show first difference
                min_len = min(len(array_data), len(bin_data))
                for i in range(min_len):
                    if array_data[i] != bin_data[i]:
                        print(f"    First difference at byte {i}: array=0x{array_data[i]:02x}, binary=0x{bin_data[i]:02x}")
                        break
                        
        except Exception as e:
            print(f"  ERROR: {e}")
            all_match = False
    
    return all_match

def main():
    print("MAX32664 Firmware Verification")
    print("==============================")
    
    if verify_files():
        print("\n✓ All files verified successfully!")
        print("The binary files match the original C arrays.")
    else:
        print("\n✗ Verification failed!")
        print("Some files do not match or are missing.")
        sys.exit(1)

if __name__ == "__main__":
    main()
