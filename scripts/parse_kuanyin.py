#!/usr/bin/env python3
"""Parse Kuanyin fortune slip HTM files (Big5) and extract structured data."""

import os
import re
import html
import json
import sys

def parse_slip(htm_path):
    """Parse a single HTM file and return structured fortune slip data."""
    # Read as Big5
    try:
        with open(htm_path, 'rb') as f:
            raw = f.read()
        text = raw.decode('big5', errors='replace')
    except Exception as e:
        print(f"Error reading {htm_path}: {e}", file=sys.stderr)
        return None

    # Convert <br> to newlines, strip HTML tags
    text = re.sub(r'<br\s*/?>', '\n', text, flags=re.I)
    text = re.sub(r'&nbsp;', ' ', text, flags=re.I)
    text = re.sub(r'<[^>]+>', '', text)
    text = html.unescape(text)

    lines = [l.strip() for l in text.split('\n') if l.strip()]

    slip = {
        'number': '',
        'rank': '',
        'palace': '',
        'poem1': [],
        'poem2': [],
        'meaning': '',
        'interpretation': '',
        'sacred': {},
        'story_title': '',
        'story_text': '',
    }

    state = None
    sacred_keys = ['家宅', '自身', '求財', '交易', '婚姻', '六甲', '行人',
                   '田蠶', '六畜', '尋人', '公訟', '移徙', '失物', '疾病',
                   '山墳', '出外', '營商']

    for l in lines:
        # Skip navigation/ads
        if '籤詩網' in l or '京極時計' in l or '電波時計' in l:
            continue

        # Slip number: 第一籤, 第二十五籤, etc.
        if re.match(r'^第.+籤$', l):
            slip['number'] = l
            state = 'after_number'
            continue

        # Rank and palace: 上籤。子宮 or 中籤。亥宮
        m = re.match(r'^([上中下])籤[。.](.+宮)', l)
        if m:
            slip['rank'] = m.group(1) + '籤'
            slip['palace'] = m.group(2)
            continue
        # Sometimes just rank without period
        m = re.match(r'^([上中下])籤', l)
        if m and state == 'after_number':
            slip['rank'] = m.group(1) + '籤'
            rest = l[len(m.group(0)):].strip(' 。.')
            if '宮' in rest:
                slip['palace'] = rest
            continue

        # Section headers
        if '詩曰一' in l:
            state = 'poem1'
            continue
        if '詩曰二' in l:
            state = 'poem2'
            continue
        if l == '詩意':
            state = 'meaning'
            continue
        if l == '解曰':
            state = 'interpret'
            continue
        if l == '聖意' or l.startswith('聖意'):
            state = 'sacred'
            continue
        if l == '故事':
            state = 'story'
            continue

        # Content collection
        if state == 'poem1':
            # Poem lines are short Chinese verse
            if len(l) <= 30 and not any(k in l for k in sacred_keys):
                slip['poem1'].append(l)
            else:
                state = None
        elif state == 'poem2':
            if len(l) <= 30 and not any(k in l for k in sacred_keys):
                slip['poem2'].append(l)
            else:
                state = None
        elif state == 'meaning':
            if slip['meaning'] == '':
                slip['meaning'] = l
            state = None
        elif state == 'interpret':
            if slip['interpretation'] == '':
                slip['interpretation'] = l
            state = None
        elif state == 'sacred':
            # Parse "家宅　祈福。" style lines
            for key in sacred_keys:
                if l.startswith(key):
                    val = l[len(key):].strip(' 　。.')
                    # Remove trailing period
                    val = val.rstrip('。.')
                    slip['sacred'][key] = val
                    break
        elif state == 'story':
            # Look for second story marker "2.xxx"
            m2 = re.match(r'^2[.．、](.+)', l)
            if m2:
                slip['story_title'] = m2.group(1).strip()
                state = 'story2_body'
                continue
        elif state == 'story2_body':
            # Stop at junk footer lines
            if any(x in l for x in ['京極時計', '電波時計', '資料來源', '標準時間',
                                     'clockfile', 'showClock', 'obj.', 'http']):
                state = None
                continue
            if slip['story_text']:
                slip['story_text'] += l
            else:
                slip['story_text'] = l

    return slip


def main():
    wording_dir = os.path.join(os.path.dirname(__file__), '..', 'assets', 'Fortune_Slips', 'kuanyin', 'wording')
    wording_dir = os.path.abspath(wording_dir)

    if not os.path.isdir(wording_dir):
        print(f"Directory not found: {wording_dir}", file=sys.stderr)
        sys.exit(1)

    slips = []
    for i in range(1, 101):
        fname = f"{i:03d}.htm"
        fpath = os.path.join(wording_dir, fname)
        if not os.path.exists(fpath):
            print(f"Missing: {fname}", file=sys.stderr)
            continue
        slip = parse_slip(fpath)
        if slip:
            slips.append(slip)
            # Print summary
            poem1_str = ' / '.join(slip['poem1'][:2]) if slip['poem1'] else '(none)'
            sacred_count = len(slip['sacred'])
            print(f"#{i:3d} {slip['number']:8s} {slip['rank']:3s} {slip['palace']:4s} | "
                  f"P1:{len(slip['poem1'])}L P2:{len(slip['poem2'])}L | "
                  f"聖意:{sacred_count} | {slip['story_title'][:20] if slip['story_title'] else '(none)'}")

    # Save as JSON
    out_path = os.path.join(os.path.dirname(__file__), '..', 'assets', 'Fortune_Slips', 'kuanyin', 'wording', 'parsed.json')
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(slips, f, ensure_ascii=False, indent=2)
    print(f"\nSaved {len(slips)} slips to {out_path}")


if __name__ == '__main__':
    main()
