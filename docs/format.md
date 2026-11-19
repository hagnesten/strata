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

## Records

A record is a row or an index key. It starts with a varint column count, then each value as a one-byte type and its data:

| Type | Value | Data |
| --- | --- | --- |
| null | 0 | nothing |
| integer | 1 | zigzag varint |
| real | 2 | 8-byte IEEE double, little-endian bits |
| text | 3 | varint byte length, then UTF-8 bytes |
| blob | 4 | varint byte length, then bytes |

Varints are little-endian base-128: 7 bits per byte, with the high bit set on every byte except the last. Records compare value by value: null is smallest, then numbers (integers and reals compare by value), then text bytewise, then blobs bytewise. When one record is a prefix of the other, the shorter one is smaller.

## B-tree pages

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 1 | kind, 10 to 13 |
| 1 | 1 | zero |
| 2 | 2 | cell count |
| 4 | 2 | start of cell content, the lowest byte used by cells |
| 6 | 2 | bytes lost to gaps between cells |
| 8 | 4 | interior pages: right-most child. Leaves: next leaf, or 0 |
| 12 | 4 | leaves: previous leaf, or 0 |
| 16 | 2 per cell | cell offsets, in key order |

Cells are packed from the end of the usable area (byte 4092) downward. A new cell goes into the gap between the offsets and the content when it fits, and a deleted or shrunk cell leaves a gap that is counted at offset 6. The page is packed again when a cell no longer fits in the gap. Every key in a child is less than or equal to the key of the cell that points at it, and the right-most child holds keys greater than the last cell.

The four cell kinds:

- **Table leaf:** varint row id, varint payload size, local payload, then a 4-byte first overflow page if the payload spilled.
- **Table interior:** 4-byte child page, varint row id.
- **Index leaf:** varint payload size, local payload, then a 4-byte first overflow page if it spilled.
- **Index interior:** 4-byte child page, then the same as an index leaf.

A payload of up to 995 bytes stays local. Anything larger keeps its first 240 bytes local and the rest goes to overflow pages. Each overflow page holds kind 20, the next page at offset 4, a 2-byte byte count at offset 8, and up to 4080 bytes of data from offset 12.

An index key is the record of the indexed columns followed by the row id, so every index entry is unique.

## The write-ahead log

Commits go to a second file next to the database, named after it with `-wal` on the end. The main file only changes during a checkpoint.

The log starts with a 32-byte header:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 8 | magic, `StrataWL` |
| 8 | 4 | version, 1 |
| 12 | 4 | page size |
| 16 | 4 | first salt |
| 20 | 4 | second salt |
| 24 | 4 | checkpoint sequence |
| 28 | 4 | CRC-32C of bytes 0 to 27 |

After the header come frames. Each is a 24-byte frame header followed by a full page:

| Offset | Size | Field |
| --- | --- | --- |
| 0 | 4 | page number |
| 4 | 4 | page count after this commit, or 0 if this frame is not the last of a commit |
| 8 | 4 | first salt, copied from the log header |
| 12 | 4 | second salt |
| 16 | 4 | chained CRC-32C |
| 20 | 4 | zero |

The chained CRC covers frame header bytes 0 to 15 and the page, and starts from the previous frame's CRC (the header's CRC for the first frame). A commit writes its changed pages and then page 1 as the commit frame, and is durable once the log is flushed.

**Recovery** reads frames in order. It stops at the first frame whose salts or chained CRC don't match, and keeps everything up to the last commit frame before that point. A torn or half-written commit is therefore never seen.

**A checkpoint** copies the newest version of every logged page into the main file, flushes it, and only then starts a fresh log with new salts. A crash in the middle simply replays the same log again on the next open.

## Free pages

A free page has kind 2. The next free page number is at offset 4 (0 ends the list). Freed pages go to the front of the list, and new pages come from the front before the file grows.
