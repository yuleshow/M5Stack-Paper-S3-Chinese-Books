# Data

Data files used by build scripts and the firmware.

## Files

| File | Description |
|------|-------------|
| `cangjie5.dict.yaml` | Cangjie 5th generation input method dictionary in YAML format. Contains character-to-radical mappings for Chinese character input. Converted to binary format (`assets/cangjie5.bin`) by `scripts/convert_cangjie.py`. |

## Cangjie Dictionary Format

The YAML file maps Cangjie radical codes (lowercase letters a–z) to Chinese characters:

```yaml
a 日
aa 昌
aaa 晶
ab 旦
...
```

Each line contains a Cangjie code followed by a space and the corresponding character(s). The binary conversion compresses this into an efficient lookup format for the ESP32.
