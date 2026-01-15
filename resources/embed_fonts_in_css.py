#!/usr/bin/env python3
"""
Script to embed Font Awesome font files as base64 data URIs in the CSS file.
This creates a fully offline-capable Font Awesome CSS.
"""

import base64
import re
import sys
from pathlib import Path


def font_to_base64(font_path):
    """Convert a font file to base64 encoded string."""
    with open(font_path, 'rb') as f:
        font_data = f.read()
    return base64.b64encode(font_data).decode('utf-8')


def embed_fonts_in_css(css_path, vendor_dir, output_path):
    """Replace font file URLs in CSS with base64 data URIs."""
    with open(css_path, 'r') as f:
        css_content = f.read()

    fonts = {
        'fa-brands-400.woff2': 'font/woff2',
        'fa-regular-400.woff2': 'font/woff2',
        'fa-solid-900.woff2': 'font/woff2',
    }

    # Convert each font to base64
    for font_file, mime_type in fonts.items():
        font_path = vendor_dir / font_file
        if not font_path.exists():
            print(f"Warning: Font file not found: {font_path}", file=sys.stderr)
            continue

        print(f"Embedding {font_file}...", file=sys.stderr)
        b64_data = font_to_base64(font_path)
        data_uri = f"data:{mime_type};base64,{b64_data}"

        # Replace URL references to this font file
        # Pattern: url(../webfonts/fa-brands-400.woff2)
        pattern = rf'url\(["\']?\.\.\/webfonts\/{re.escape(font_file)}["\']?\)'
        css_content = re.sub(pattern, f'url("{data_uri}")', css_content)

    # Also remove TTF fallbacks since woff2 is used only
    css_content = re.sub(r',\s*url\([^)]*\.ttf[^)]*\)\s*format\(["\']truetype["\']\)', '', css_content)

    with open(output_path, 'w') as f:
        f.write(css_content)

    print(f"Created offline Font Awesome CSS: {output_path}", file=sys.stderr)
    print(f"Size: {len(css_content):,} bytes", file=sys.stderr)


def main():
    if len(sys.argv) != 4:
        print("Usage: embed_fonts_in_css.py <input_css> <vendor_dir> <output_css>", file=sys.stderr)
        sys.exit(1)

    css_path = Path(sys.argv[1])
    vendor_dir = Path(sys.argv[2])
    output_path = Path(sys.argv[3])

    if not css_path.exists():
        print(f"Error: CSS file not found: {css_path}", file=sys.stderr)
        sys.exit(1)

    if not vendor_dir.is_dir():
        print(f"Error: Vendor directory not found: {vendor_dir}", file=sys.stderr)
        sys.exit(1)

    embed_fonts_in_css(css_path, vendor_dir, output_path)


if __name__ == '__main__':
    main()
