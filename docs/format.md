# Strata file format

This is the on-disk format of a Strata database. Code reads and writes exactly what this document says, and changes here come before changes in code. All integers are little-endian unless a section says otherwise.

## Pages

A database file is a sequence of pages of 4096 bytes. Pages are numbered from 1, and page *n* starts at byte `(n - 1) * 4096`. Number 0 means "no page".

The last 4 bytes of every page hold a checksum:

```
checksum = crc32c(page[0 .. 4092]) XOR (page_number * 0x9E3779B1)
```

Mixing in the page number means a page written to the wrong place fails its check, not just a page with flipped bits. A reader that finds a bad checksum reports the page as corrupt and never interprets its contents.

The first byte of every page says what kind it is:

| Kind | Value | Meaning |
| --- | --- | --- |
| header | 1 | page 1 only |
| free | 2 | on the free list |
| table leaf | 10 | B+tree leaf keyed by row id |
| table interior | 11 | B+tree interior keyed by row id |
| index leaf | 12 | B-tree leaf keyed by an encoded record |
| index interior | 13 | B-tree interior keyed by an encoded record |
| overflow | 20 | continuation of a value too long for its cell |

## Page 1: the header

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | kind, 1 |
| 1 | 15 | magic, `Strata format 1` |
| 16 | 4 | page size, 4096 |
| 20 | 4 | page count, the number of pages in the database |
| 24 | 4 | first free page, or 0 |
| 28 | 4 | number of free pages |
| 32 | 4 | root page of the schema table, or 0 before the first table |
| 36 | 4 | schema version, raised by every schema change |
| 40 | 8 | change counter, raised by every commit |
| 48 | 4044 | reserved, zero |
| 4092 | 4 | checksum |

## Free pages

A free page has kind 2. The next free page number is at offset 4 (0 ends the list). Freed pages go to the front of the list, and new pages come from the front before the file grows.
