#pragma once
#include <Arduino.h>

// ==================== UTF-8 Utility Functions ====================

// Get the byte length of a UTF-8 character from its leading byte
inline int utf8CharLen(unsigned char leadByte) {
  if (leadByte < 0x80) return 1;
  if (leadByte < 0xE0) return 2;
  if (leadByte < 0xF0) return 3;
  return 4;
}

// Decode a UTF-8 character from a String at position `pos`.
// Returns the Unicode codepoint and advances `pos` past the character.
inline uint32_t utf8Decode(const String &text, int &pos) {
  unsigned char c = text.charAt(pos);
  uint32_t cp;
  if (c < 0x80) {
    cp = c;
    pos += 1;
  } else if (c < 0xE0) {
    cp = ((c & 0x1F) << 6) | (text.charAt(pos + 1) & 0x3F);
    pos += 2;
  } else if (c < 0xF0) {
    cp = ((c & 0x0F) << 12) |
         ((text.charAt(pos + 1) & 0x3F) << 6) |
         (text.charAt(pos + 2) & 0x3F);
    pos += 3;
  } else {
    cp = ((c & 0x07) << 18) |
         ((text.charAt(pos + 1) & 0x3F) << 12) |
         ((text.charAt(pos + 2) & 0x3F) << 6) |
         (text.charAt(pos + 3) & 0x3F);
    pos += 4;
  }
  return cp;
}

// Decode a UTF-8 character from a raw char buffer at position `pos`.
// Returns the Unicode codepoint and advances `pos` past the character.
inline uint32_t utf8Decode(const char *buf, int &pos) {
  unsigned char c = (unsigned char)buf[pos];
  uint32_t cp;
  if (c < 0x80) {
    cp = c;
    pos += 1;
  } else if (c < 0xE0) {
    cp = ((c & 0x1F) << 6) | ((unsigned char)buf[pos + 1] & 0x3F);
    pos += 2;
  } else if (c < 0xF0) {
    cp = ((c & 0x0F) << 12) |
         (((unsigned char)buf[pos + 1] & 0x3F) << 6) |
         ((unsigned char)buf[pos + 2] & 0x3F);
    pos += 3;
  } else {
    cp = ((c & 0x07) << 18) |
         (((unsigned char)buf[pos + 1] & 0x3F) << 12) |
         (((unsigned char)buf[pos + 2] & 0x3F) << 6) |
         ((unsigned char)buf[pos + 3] & 0x3F);
    pos += 4;
  }
  return cp;
}

// Encode a Unicode codepoint to UTF-8, appending to a String.
// Returns the number of bytes written.
inline int utf8Encode(uint32_t cp, String &out) {
  if (cp < 0x80) {
    out += (char)cp;
    return 1;
  } else if (cp < 0x800) {
    out += (char)(0xC0 | (cp >> 6));
    out += (char)(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp < 0x10000) {
    out += (char)(0xE0 | (cp >> 12));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
    return 3;
  } else {
    out += (char)(0xF0 | (cp >> 18));
    out += (char)(0x80 | ((cp >> 12) & 0x3F));
    out += (char)(0x80 | ((cp >> 6) & 0x3F));
    out += (char)(0x80 | (cp & 0x3F));
    return 4;
  }
}

// Encode a Unicode codepoint to a char buffer (must have room for 4 bytes + null).
// Returns the number of bytes written. Buffer is null-terminated.
inline int utf8Encode(uint32_t cp, char *buf) {
  if (cp < 0x80) {
    buf[0] = (char)cp; buf[1] = 0;
    return 1;
  } else if (cp < 0x800) {
    buf[0] = 0xC0 | (cp >> 6);
    buf[1] = 0x80 | (cp & 0x3F);
    buf[2] = 0;
    return 2;
  } else if (cp < 0x10000) {
    buf[0] = 0xE0 | (cp >> 12);
    buf[1] = 0x80 | ((cp >> 6) & 0x3F);
    buf[2] = 0x80 | (cp & 0x3F);
    buf[3] = 0;
    return 3;
  } else {
    buf[0] = 0xF0 | (cp >> 18);
    buf[1] = 0x80 | ((cp >> 12) & 0x3F);
    buf[2] = 0x80 | ((cp >> 6) & 0x3F);
    buf[3] = 0x80 | (cp & 0x3F);
    buf[4] = 0;
    return 4;
  }
}

// ==================== Vertical Punctuation Mapping ====================
// Convert horizontal CJK punctuation to their vertical presentation forms.
// Used when rendering text in vertical (top-to-bottom) layout.
// Returns the vertical form codepoint, or the original if no mapping exists.
inline uint32_t toVerticalPunct(uint32_t cp) {
  switch (cp) {
    case 0x300C: return 0xFE41; // 「 → ﹁
    case 0x300D: return 0xFE42; // 」 → ﹂
    case 0x201C: return 0xFE41; // " → ﹁
    case 0x201D: return 0xFE42; // " → ﹂
    case 0x3008: return 0xFE3F; // 〈 → ︿
    case 0x3009: return 0xFE40; // 〉 → ﹀
    case 0x300E: return 0xFE43; // 『 → ﹃
    case 0x300F: return 0xFE44; // 』 → ﹄
    case 0x300A: return 0xFE3D; // 《 → ︽
    case 0x300B: return 0xFE3E; // 》 → ︾
    case 0x3010: return 0xFE3B; // 【 → ︻
    case 0x3011: return 0xFE3C; // 】 → ︼
    case 0xFF08: return 0xFE35; // （ → ︵
    case 0xFF09: return 0xFE36; // ） → ︶
    case 0x3016: return 0xFE17; // 〖 → ︗
    case 0x3017: return 0xFE18; // 〗 → ︘
    case 0x3014: return 0xFE39; // 〔 → ︹
    case 0x3015: return 0xFE3A; // 〕 → ︺
    case 0xFF5B: return 0xFE37; // ｛ → ︷
    case 0xFF5D: return 0xFE38; // ｝ → ︸
    case 0xFF3B: return 0xFE47; // ［ → ﹇
    case 0xFF3D: return 0xFE48; // ］ → ﹈
    case 0x2026: return 0xFE19; // … → ︙
    case 0x2025: return 0xFE30; // ‥ → ︰
    case 0x2014: return 0xFE31; // — → ︱
    case 0xFE4F: return 0xFE34; // ﹏ → ︴
    default: return cp;
  }
}

// Convert half-width ASCII punctuation to full-width CJK equivalents.
// Used in Chinese reading to ensure uniform character width.
inline uint32_t halfToFullWidth(uint32_t cp) {
  switch (cp) {
    case ',':  return 0xFF0C; // ，
    case '.':  return 0x3002; // 。
    case '!':  return 0xFF01; // ！
    case '?':  return 0xFF1F; // ？
    case ':':  return 0xFF1A; // ：
    case ';':  return 0xFF1B; // ；
    case '(':  return 0xFF08; // （
    case ')':  return 0xFF09; // ）
    case '[':  return 0xFF3B; // ［
    case ']':  return 0xFF3D; // ］
    case '{':  return 0xFF5B; // ｛
    case '}':  return 0xFF5D; // ｝
    case '"':  return 0x300C; // 「
    case '\'': return 0x300E; // 『
    case '~':  return 0xFF5E; // ～
    case ' ':  return 0x3000; // 　(ideographic space)
    case 0x00A0: return 0x3000; // non-breaking space → ideographic space
    default:   return cp;
  }
}

// Apply vertical punctuation substitution to a UTF-8 String.
// Decodes the first codepoint, maps it, and re-encodes if changed.
// Also updates `unicode` in-place if provided.
inline void applyVerticalPunct(String &ch, uint32_t &unicode) {
  uint32_t mapped = toVerticalPunct(unicode);
  if (mapped != unicode) {
    unicode = mapped;
    ch = "";
    utf8Encode(mapped, ch);
  }
}

// Check if a codepoint is a punctuation mark that should NOT start a new column
// (禁則處理 kinsoku: line-start / column-start prohibited characters)
inline bool isColumnStartProhibited(uint32_t cp) {
  switch (cp) {
    // Closing brackets / quotes
    case 0x300D: // 」
    case 0x300F: // 』
    case 0x300B: // 》
    case 0x3009: // 〉
    case 0x3011: // 】
    case 0x3015: // 〕
    case 0x3017: // 〗
    case 0xFF09: // ）
    case 0xFF5D: // ｝
    case 0xFF3D: // ］
    case 0x201D: // \xe2\x80\x9d
    case 0x2019: // \xe2\x80\x99
    // Periods / commas / stops
    case 0x3002: // 。
    case 0xFF0C: // ，
    case 0x3001: // 、
    case 0xFF1B: // ；
    case 0xFF1A: // ：
    case 0xFF01: // ！
    case 0xFF1F: // ？
    // Small punctuation
    case 0x30FB: // ・
    case 0x2026: // …
    case 0x2025: // ‥
    case 0x2014: // —
    // Vertical forms (after applyVerticalPunct mapping)
    case 0xFE42: // ﹂
    case 0xFE44: // ﹄
    case 0xFE3E: // ︾
    case 0xFE40: // ﹀
    case 0xFE3C: // ︼
    case 0xFE3A: // ︺
    case 0xFE18: // ︘
    case 0xFE36: // ︶
    case 0xFE38: // ︸
    case 0xFE48: // ﹈
    case 0xFE19: // ︙
    case 0xFE30: // ︰
    case 0xFE31: // ︱
    // ASCII equivalents
    case 0x002C: // ,
    case 0x002E: // .
    case 0x003F: // ?
    case 0x0021: // !
    case 0x003B: // ;
    case 0x003A: // :
    case 0x0029: // )
    case 0x005D: // ]
      return true;
    default:
      return false;
  }
}

// Extract one UTF-8 character as a substring from a String at position `pos`.
// Advances `pos` past the character. Returns the substring.
inline String utf8ExtractChar(const String &text, int &pos) {
  int start = pos;
  int len = utf8CharLen((unsigned char)text.charAt(pos));
  pos += len;
  return text.substring(start, start + len);
}
