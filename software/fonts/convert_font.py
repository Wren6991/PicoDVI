#!/usr/bin/env python3
import os

# Configuration
INPUT_FILE = "/home/noneya/pico/PicoDVI/software/fonts/IBM_VGA_8x16.bin"
OUTPUT_FILE = "/home/noneya/pico/PicoDVI/software/include/font_8x16.h"
CHARS = 95  # ASCII 32 to 126
CHAR_HEIGHT = 16
CHAR_WIDTH = 8
FONT_NAME = "font_8x16"

def main():
    # Check if input file exists
    if not os.path.exists(INPUT_FILE):
        print(f"Error: Input file {INPUT_FILE} not found")
        return
    
    # Read binary font file
    with open(INPUT_FILE, "rb") as f:
        font_data = f.read()
    
    # Verify file size (256 chars * 16 bytes = 4096 bytes)
    expected_size = 256 * CHAR_HEIGHT
    if len(font_data) < expected_size:
        print(f"Error: Input file is too small ({len(font_data)} bytes, expected {expected_size})")
        return
    
    # Extract ASCII 32 to 126 (95 characters)
    start_char = 32  # ASCII space
    font_subset = []
    for i in range(start_char, start_char + CHARS):
        offset = i * CHAR_HEIGHT
        char_data = font_data[offset:offset + CHAR_HEIGHT]
        font_subset.extend(char_data)
    
    # Generate C header
    with open(OUTPUT_FILE, "w") as f:
        f.write("#ifndef FONT_8X16_H\n")
        f.write("#define FONT_8X16_H\n\n")
        f.write(f"static const uint8_t {FONT_NAME}[{CHARS * CHAR_HEIGHT}] = {{\n")
        
        # Write font data as hex
        for i, byte in enumerate(font_subset):
            if i % 16 == 0:
                f.write("    ")
            f.write(f"0x{byte:02x}, ")
            if i % 16 == 15:
                f.write("\n")
        
        # Ensure last line ends properly
        if len(font_subset) % 16 != 0:
            f.write("\n")
        
        f.write("};\n\n")
        f.write("#endif\n")
    
    print(f"Generated {OUTPUT_FILE} with {CHARS} characters ({len(font_subset)} bytes)")

if __name__ == "__main__":
    main()