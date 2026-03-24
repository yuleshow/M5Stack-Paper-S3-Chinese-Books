#!/usr/bin/env python3
"""Check for inline <style> tags and other HTML patterns that could break htmlStripDirect."""
import zipfile, re, os, sys

BASE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

for epub_name in sorted(os.listdir(os.path.join(BASE, 'sd_card', 'books'))):
    if 'economist' not in epub_name.lower():
        continue
    path = os.path.join(BASE, 'sd_card', 'books', epub_name)
    z = zipfile.ZipFile(path)
    
    print(f"\n{'='*60}")
    print(f"EPUB: {epub_name}")
    
    body_style_chapters = []
    svg_chapters = []
    self_closing_style = []
    unclosed_style = []
    
    for name in z.namelist():
        if not (name.endswith('.html') or name.endswith('.xhtml') or name.endswith('.htm')):
            continue
        raw = z.read(name).decode('utf-8', errors='replace')
        
        # Check for <style> inside <body>
        body_match = re.search(r'<body[^>]*>(.*)', raw, re.DOTALL | re.IGNORECASE)
        if body_match:
            body_content = body_match.group(1)
            style_in_body = re.findall(r'<style[^>]*>', body_content, re.IGNORECASE)
            style_close_in_body = re.findall(r'</style\s*>', body_content, re.IGNORECASE)
            if style_in_body:
                body_style_chapters.append((name, len(style_in_body), len(style_close_in_body)))
            
            # Check for self-closing <style ... />
            self_close = re.findall(r'<style[^>]*/\s*>', body_content, re.IGNORECASE)
            if self_close:
                self_closing_style.append((name, self_close))
            
            # Check if <style> opens but doesn't close in body
            if len(style_in_body) > len(style_close_in_body):
                unclosed_style.append((name, len(style_in_body), len(style_close_in_body)))
        
        # Check for SVG elements
        if '<svg' in raw.lower():
            svg_chapters.append(name)
    
    print(f"\nChapters with <style> in <body>: {len(body_style_chapters)}")
    for ch, opens, closes in body_style_chapters[:5]:
        print(f"  {ch}: {opens} opens, {closes} closes")
    
    if unclosed_style:
        print(f"\n*** UNCLOSED <style> in body: {len(unclosed_style)} ***")
        for ch, opens, closes in unclosed_style:
            print(f"  {ch}: {opens} opens, {closes} closes")
    
    if self_closing_style:
        print(f"\nSelf-closing <style />: {len(self_closing_style)}")
        for ch, patterns in self_closing_style:
            print(f"  {ch}: {patterns}")
    
    print(f"\nSVG chapters: {len(svg_chapters)}")
    for ch in svg_chapters[:3]:
        print(f"  {ch}")
    
    # Check for any unusual HTML patterns
    print(f"\nChecking for unusual patterns...")
    for name in z.namelist():
        if not (name.endswith('.html') or name.endswith('.xhtml')):
            continue
        raw = z.read(name).decode('utf-8', errors='replace')
        
        # Check for nested <style> tags
        if raw.count('<style') > 1:
            print(f"  Multiple <style> tags in: {name} (count={raw.count('<style')})")
        
        # Check for <link rel="stylesheet"> (inline CSS include)
        if '<link' in raw.lower() and 'stylesheet' in raw.lower():
            body_match = re.search(r'<body[^>]*>(.*)', raw, re.DOTALL | re.IGNORECASE)
            if body_match and '<link' in body_match.group(1).lower():
                print(f"  <link stylesheet> in body: {name}")
    
    z.close()
