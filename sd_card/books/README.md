# Books

![Content](https://img.shields.io/badge/Type-Content-purple)
![Formats](https://img.shields.io/badge/Format-TXT_/_EPUB-blue)

E-book files for the Chinese book reader. Place UTF-8 encoded text files (`.txt`) and EPUB files (`.epub`) here.

## Supported Formats

- **TXT** — Plain text, UTF-8 encoded. Rendered in vertical CJK layout (right-to-left columns).
- **EPUB** — Standard EPUB format. HTML content is extracted and rendered as plain text.

## Auto-Generated Sidecar Files

The reader automatically creates sidecar files alongside each book:

| Extension | Purpose |
|-----------|---------|
| `.pos` | Saved reading position (byte offset). Auto-created when you read a book. |
| `.bm` | Bookmarks file (up to 5 per book). Created when you add bookmarks. |

These files are gitignored since they are user-specific.

## Notes

- Maximum 20 books are scanned from this directory
- Book files are gitignored due to copyright — add your own books to the SD card
- File names are displayed in the book list UI
