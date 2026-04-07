import zipfile, re, sys

base = "/Users/yuleshow/yuleshow-github/M5Stack Paper S3 for Chinese Books/sd_card/books/en"
for book in ["Dragons of Winter Night_nodrm.epub", "Pok Pok_nodrm.epub"]:
    path = f"{base}/{book}"
    z = zipfile.ZipFile(path)
    html_files = [f for f in z.namelist() if f.endswith(('.html','.xhtml','.htm'))]
    print(f"\n=== {book} ({len(html_files)} chapters) ===")
    for f in html_files[:5]:
        content = z.read(f).decode('utf-8', errors='replace')
        a_opens = len(re.findall(r'<a[\s>]', content, re.I))
        a_closes = len(re.findall(r'</a>', content, re.I))
        text_len = len(re.sub(r'<[^>]+>', '', content).strip())
        print(f"  {f}: {text_len} chars, {a_opens} <a>, {a_closes} </a>")
        # Show first 3 <a> tag examples
        for m in list(re.finditer(r'<a[^>]*>.{0,80}?</a>', content, re.DOTALL))[:2]:
            print(f"    example: {m.group()[:120]}")
