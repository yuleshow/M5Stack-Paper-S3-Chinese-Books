#!/usr/bin/env python3
"""Export parsed kuanyin fortune slip JSON to CSV for easy review."""
import json, csv, os

base = os.path.join(os.path.dirname(__file__), '..', 'assets', 'Fortune_Slips', 'kuanyin', 'wording')
with open(os.path.join(base, 'parsed.json'), 'r') as f:
    data = json.load(f)

sacred_keys = ['家宅','自身','求財','交易','婚姻','六甲','行人','田蠶','六畜','尋人','公訟','訟詞','移徙','失物','疾病','山墳']

out = os.path.join(base, 'kuanyin.csv')
with open(out, 'w', newline='', encoding='utf-8-sig') as f:
    w = csv.writer(f)
    w.writerow(['#','籤號','等級','宮位',
                '詩曰一','詩曰二',
                '詩意','解曰','故事','故事內容'] + sacred_keys)
    for i, s in enumerate(data, 1):
        poem1 = '\n'.join(s.get('poem1', []))
        poem2 = '\n'.join(s.get('poem2', []))
        sacred = s.get('sacred', {})
        row = [i, s.get('number',''), s.get('rank',''), s.get('palace',''),
               poem1, poem2,
               s.get('meaning',''), s.get('interpretation',''),
               s.get('story_title',''), s.get('story_text','')]
        row += [sacred.get(k, '') for k in sacred_keys]
        w.writerow(row)

print(f"Wrote {len(data)} slips to {out}")
