#!/usr/bin/env python3
import os
import io
import sys

def parse_c_array(h_file_path):
    if not os.path.exists(h_file_path):
        return None
    with open(h_file_path, 'r') as f:
        content = f.read()
    # Find the array contents between { and }
    start = content.find('{')
    end = content.find('}')
    if start == -1 or end == -1:
        return None
    elements = content[start+1:end].strip().split(',')
    bytes_list = []
    for e in elements:
        e = e.strip()
        if not e:
            continue
        try:
            bytes_list.append(int(e, 16))
        except ValueError:
            pass
    return bytes_list

def svg_to_png_bytes(svg_path, width, height):
    import cairosvg
    with open(svg_path, 'rb') as f:
        svg_data = f.read()
    png_bytes = cairosvg.svg2png(bytestring=svg_data, output_width=width, output_height=height)
    return png_bytes

def render_image(svg_path, width, height, rotate_angle):
    from PIL import Image
    png_bytes = svg_to_png_bytes(svg_path, width, height)
    img = Image.open(io.BytesIO(png_bytes))
    img = img.convert('RGBA')
    # Flatten alpha: paste on white background
    background = Image.new('RGBA', img.size, (255, 255, 255, 255))
    background.paste(img, mask=img.split()[3])
    img = background
    
    # Rotate
    if rotate_angle != 0:
        img = img.rotate(rotate_angle, expand=True)
    return img

def image_to_bytes(img, threshold):
    img = img.convert('L')
    width, height = img.size
    pixels = list(img.getdata())
    packed = []
    for y in range(height):
        for x in range(0, width, 8):
            byte = 0
            for b in range(8):
                if x + b < width:
                    v = pixels[y * width + x + b]
                    # 1 for white, 0 for black
                    bit = 1 if v >= threshold else 0
                    byte |= (bit << (7 - b))
            packed.append(byte)
    return packed

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    
    # Look for forkdrift.svg
    svg_path = os.path.join(project_root, 'src', 'images', 'forkdrift.svg')
    if not os.path.exists(svg_path):
        svg_path = os.path.join(project_root, '..', 'forkdrift.svg')
    
    if not os.path.exists(svg_path):
        print(f"Error: forkdrift.svg not found at {svg_path}")
        sys.exit(1)
        
    logo160_path = os.path.join(project_root, 'src', 'images', 'Logo160.h')
    existing_bytes = parse_c_array(logo160_path)
    
    if existing_bytes is None:
        print(f"Warning: Logo160.h not found or could not be parsed at {logo160_path}")
        # Default fallback parameters
        best_rotation = 90
        best_threshold = 128
    else:
        print(f"Successfully loaded and parsed existing Logo160.h ({len(existing_bytes)} bytes)")
        
        # Test rotations and thresholds
        best_rotation = None
        best_threshold = None
        min_diff = float('inf')
        
        # Let's search rotations [0, 90, 180, 270] and thresholds [100, 128, 150, 180, 200]
        # We will do a finer search if we find a close match.
        rotations = [0, 90, 180, 270]
        thresholds = list(range(50, 220, 10))
        
        print("Searching for matching rendering parameters...")
        for rotation in rotations:
            try:
                img = render_image(svg_path, 160, 160, rotation)
            except Exception as e:
                print(f"Error rendering at rotation {rotation}: {e}")
                continue
                
            for thresh in thresholds:
                packed = image_to_bytes(img, thresh)
                if len(packed) != len(existing_bytes):
                    continue
                # Calculate number of differing bytes
                diff = sum(1 for a, b in zip(packed, existing_bytes) if a != b)
                if diff < min_diff:
                    min_diff = diff
                    best_rotation = rotation
                    best_threshold = thresh
                    if diff == 0:
                        break
            if min_diff == 0:
                break
                
        print(f"Best match found: rotation={best_rotation}°, threshold={best_threshold} (differing bytes: {min_diff}/{len(existing_bytes)})")
        
        # Fine-tune threshold search if diff is close but not zero
        if min_diff > 0 and best_threshold is not None:
            print("Fine-tuning threshold search...")
            img = render_image(svg_path, 160, 160, best_rotation)
            for thresh in range(max(1, best_threshold - 10), min(255, best_threshold + 10)):
                packed = image_to_bytes(img, thresh)
                diff = sum(1 for a, b in zip(packed, existing_bytes) if a != b)
                if diff < min_diff:
                    min_diff = diff
                    best_threshold = thresh
                    if diff == 0:
                        break
            print(f"Fine-tuned: rotation={best_rotation}°, threshold={best_threshold} (differing bytes: {min_diff}/{len(existing_bytes)})")

        if min_diff > 0:
            print(f"Warning: Could not get a perfect match (minimum diff: {min_diff} bytes). Will proceed with rotation={best_rotation or 90}°, threshold={best_threshold or 128}.")
            if best_rotation is None:
                best_rotation = 90
            if best_threshold is None:
                best_threshold = 128

    print(f"Generating Logo240.h with rotation={best_rotation}°, threshold={best_threshold}...")
    
    # Generate 240x240
    img240 = render_image(svg_path, 240, 240, best_rotation)
    packed240 = image_to_bytes(img240, best_threshold)
    
    # Format as C array
    c = f'#pragma once\n#include <cstdint>\n\n'
    c += f'// Image dimensions: 240x240\n'
    c += f'static const uint8_t Logo240[] = {{\n    '
    for i, v in enumerate(packed240):
        c += f'0x{v:02X}, '
        if (i + 1) % 19 == 0:
            c += '\n    '
    c = c.rstrip(', \n') + '\n};\n'
    
    output_path = os.path.join(project_root, 'src', 'images', 'Logo240.h')
    with open(output_path, 'w') as f:
        f.write(c)
    print(f"Successfully generated {output_path} ({len(packed240)} bytes)!")

if __name__ == '__main__':
    main()
