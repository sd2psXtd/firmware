# MCFAT / PS2 Memory Card FAT Filesystem Specification

This document is an implementation-derived specification of the PlayStation 2
memory card filesystem as handled by this repository. It intentionally favors
behavior observed in `lib/src/*.c` over secondary public descriptions.

The format is not PC FAT. It is a Sony PS2 memory card filesystem with a
FAT-like cluster chain table, NAND-style page and erase-block handling, spare
data/ECC support, bad-block tracking, and a recovery protocol based on two
reserved backup blocks.

## Scope And Terminology

All multi-byte integer fields used by this implementation are little-endian C
integer fields.

Terminology used below:

- Page: the physical unit read and written through `McReadPage` and
  `McWritePage`. This implementation hard-codes 512-byte data pages.
- Spare data: per-page metadata bytes stored after the page data. With 512-byte
  pages this implementation uses 16 spare bytes, making a raw page image 528
  bytes when spare data is present.
- Erase block: a physical erase unit containing `blocksize` pages.
- Cluster: the filesystem allocation unit. This implementation fixes the
  cluster data size at 1024 bytes.
- Physical cluster: a cluster index from the beginning of the card image.
- FAT index: an allocatable data cluster index relative to `alloc_offset`.
- Directory entry index: a zero-based index of a 512-byte entry within a
  directory.

## Physical Geometry

The host supplies raw media operations through `mcfat_mcops_t`:

| Operation | Meaning |
| --- | --- |
| `page_read(info, page, count, buffer)` | Read `count` pages starting at page. |
| `page_write(info, page, buffer)` | Write one page. The buffer may contain page data plus spare data. |
| `page_erase(info, page)` | Erase the erase block containing or starting at page, depending on host adapter. |
| `info(info)` | Return card/page/block geometry. |

In this implementation:

- `pagesize` is always treated as 512 bytes.
- `cluster_size` is always 1024 bytes.
- `pages_per_cluster = 1024 / pagesize`, normally 2.
- `blocksize` is stored as pages per erase block.
- `clusters_per_block = blocksize / pages_per_cluster`.
- `cardsize` is expressed in pages.
- `clusters_per_card = (cardsize / blocksize) * clusters_per_block`.
- Spare byte count is `(pagesize + 31) >> 5`, normally 16.

When spare/ECC is active, each 512-byte page has 16 spare bytes. The first
12 spare bytes hold four 3-byte ECC records, one for each 128-byte slice of the
page. The implementation writes page data followed by 16 spare bytes.

## Card Flags

The implementation uses these physical flags internally:

| Bit | Name | Meaning |
| --- | --- | --- |
| `0x01` | `CF_USE_ECC` | Spare ECC should be checked/corrected. |
| `0x08` | `CF_BAD_BLOCK` | Bad block handling is available/expected. |
| `0x10` | `CF_ERASE_ZEROES` | Erased data polarity is inverted from the default assumption. |

Important implementation note: `McGetCardSpec` obtains card geometry from the
host adapter but then forces `pagesize = 512` and `flags = 0x22 | CF_BAD_BLOCK`.
It only adds `CF_USE_ECC` when the host reports `page_bytes != 512`.

## Overall Layout

The filesystem is cluster-addressed after geometry is known.

The formatted card has these regions:

1. Reserved low physical clusters, including the superblock at page 0.
2. One or more IFC clusters, whose absolute physical cluster numbers are stored
   in the superblock `ifc_list`.
3. FAT clusters, whose absolute physical cluster numbers are stored in the IFC
   clusters.
4. Allocatable data clusters beginning at `alloc_offset`.
5. Reserved backup blocks near the end of the card.

Directory entries and file FAT chains store cluster numbers relative to
`alloc_offset`. IFC and FAT cluster references are absolute physical cluster
numbers.

To read a file or directory data cluster:

```text
physical_cluster = alloc_offset + relative_cluster
page = physical_cluster * pages_per_cluster
```

To access a FAT entry for `relative_cluster`, use the two-level FAT described
below.

## Superblock

The superblock begins at page 0 / physical cluster 0. The implementation treats
the first 0x150 bytes as the authoritative on-media superblock. It may write the
larger in-memory `MCDevInfo` structure, but mount-time parsing copies only bytes
0x000..0x14f and recomputes or overrides the remaining runtime fields.

### Superblock Field Layout

| Offset | Size | Type | Field | Meaning |
| ---: | ---: | --- | --- | --- |
| `0x000` | 28 | char[28] | `magic` | Must be `Sony PS2 Memory Card Format ` exactly. |
| `0x01c` | 12 | char[12] | `version` | Format version string. This implementation formats as `1.2.0.0`. |
| `0x028` | 2 | int16 | `pagesize` | Bytes per page. Expected 512 here. |
| `0x02a` | 2 | uint16 | `pages_per_cluster` | Normally 2. |
| `0x02c` | 2 | uint16 | `blocksize` | Pages per erase block. Normally 16 for an 8 MB card. |
| `0x02e` | 2 | uint16 | `unused` | Written as `0xff00` by this implementation. |
| `0x030` | 4 | uint32 | `clusters_per_card` | Total physical clusters on card. |
| `0x034` | 4 | uint32 | `alloc_offset` | First allocatable physical cluster. |
| `0x038` | 4 | uint32 | `alloc_end` | Count/end of allocatable area relative to `alloc_offset`. |
| `0x03c` | 4 | uint32 | `rootdir_cluster` | Root directory relative cluster. Must be 0 for this implementation. |
| `0x040` | 4 | uint32 | `backup_block1` | Erase block used to store backup page contents during writes. |
| `0x044` | 4 | uint32 | `backup_block2` | Erase block used as write-progress marker. Normally erased when clean. |
| `0x048` | 8 | bytes | `unused2` | Reserved/unused. |
| `0x050` | 128 | uint32[32] | `ifc_list` | Absolute physical clusters of indirect FAT clusters. |
| `0x0d0` | 128 | int32[32] | `bad_block_list` | Bad erase block numbers. Unused entries are `-1`. |

The in-memory structure has these additional fields at 0x150 and later:
`cardtype`, `cardflags`, `cluster_size`, `FATentries_per_cluster`,
`clusters_per_block`, `cardform`, current directory state, and allocation cache
state. Treat these as runtime-derived for interoperability, even if some images
created by this implementation contain them on disk.

### Superblock Validation

Mounting performs these checks:

1. Read page 0.
2. Compare the first 28 bytes with `Sony PS2 Memory Card Format `.
3. Reject version 1.0 by checking major byte at offset 28 and minor byte at
   offset 30.
4. Copy bytes 0x000..0x14f into the device info structure.
5. Set card type to PS2.
6. Check backup blocks and recover if required.
7. Read root directory entry 0 and require its name to be `.`.
8. Read root directory entry 1 and require its name to be `..`.

For version 1.1, if `clusters_per_block * backup_block2 == alloc_end`, mount
adjusts `alloc_end` to `(clusters_per_block * backup_block2) - alloc_offset`.

## IFC And FAT

The filesystem uses a two-level table:

```text
relative_cluster -> indirect_index, fat_offset
indirect_index -> ifc_index, indirect_offset
superblock.ifc_list[ifc_index] -> absolute IFC cluster
IFC cluster entry[indirect_offset] -> absolute FAT cluster
FAT cluster entry[fat_offset] -> FAT value
```

All IFC and FAT clusters are 1024-byte clusters interpreted as 256 signed
32-bit little-endian entries.

Index calculations:

```text
FATentries_per_cluster = 256

indirect_index  = relative_cluster / 256
fat_offset      = relative_cluster % 256

ifc_index       = indirect_index / 256
indirect_offset = indirect_index % 256

ifc_cluster     = ifc_list[ifc_index]              // absolute physical cluster
fat_cluster     = ifc[ifc_cluster][indirect_offset] // absolute physical cluster
fat_value       = fat[fat_cluster][fat_offset]
```

The formatter computes:

```text
fat_length = ceil((clusters_per_card * 4) / cluster_size)
ifc_length = ceil((fat_length * 4) / cluster_size)
```

If more than 32 IFC clusters would be needed, it clamps to:

```text
ifc_length = 32
fat_length = 256 * 32
```

## FAT Values

FAT entries are signed 32-bit values, but their bit patterns are what matter.

| Value / pattern | Meaning |
| --- | --- |
| `0x7fffffff` | Free cluster. |
| Any non-negative value | Free cluster for allocation purposes. Deleted chains can leave stale positive next-cluster values. |
| `0xffffffff` | Reserved or allocated end-of-chain. |
| `0x80000000 | n` | Link to relative cluster `n`. The high bit marks the cluster as allocated. |
| `0xfffffffd` | Bad cluster. |

Allocation scans from an internal cursor and accepts any FAT entry `>= 0` as
free. Reserving a free cluster first writes `0xffffffff`. Linking an existing
cluster to a new cluster writes `0x80000000 | new_relative_cluster`.

Deleting an entry clears the directory `exists` bit and clears the high bit on
each FAT entry in the chain. This turns `0xffffffff` into `0x7fffffff`, and
linked entries into non-negative stale next indices. Because allocation treats
all non-negative values as free, both forms are free.

Re-enabling an entry with `McSetDirEntryState(..., flags != 0)` sets the high
bit on each FAT entry in its chain until the end marker is reached.

## Directory Entries

Every directory entry is exactly 512 bytes. With the normal 1024-byte cluster
size, each directory cluster contains two entries.

### Directory Entry Layout

| Offset | Size | Type | Field | Meaning |
| ---: | ---: | --- | --- | --- |
| `0x000` | 2 | uint16 | `mode` | Permission/type/status flags. |
| `0x002` | 2 | uint16 | `unused` | Returned as `Reserve1` by directory listing. |
| `0x004` | 4 | uint32 | `length` | File byte length, or directory entry count. |
| `0x008` | 8 | `sceMcStDateTime` | `created` | Creation timestamp. |
| `0x010` | 4 | uint32/int32 | `cluster` | Starting relative cluster, or parent-related cluster for dot entries. |
| `0x014` | 4 | uint32 | `dir_entry` | Directory-entry back-reference, mainly used by dot entries. |
| `0x018` | 8 | `sceMcStDateTime` | `modified` | Modification timestamp. |
| `0x020` | 4 | uint32 | `attr` | Application/private attribute field. |
| `0x024` | 28 | uint32[7] | `unused2` | Reserved/unused. |
| `0x040` | 32 | char[32] | `name` | Entry name. |
| `0x060` | 416 | bytes | `unused3` | Reserved/unused. |

### Timestamp Layout

`sceMcStDateTime` is 8 bytes:

| Offset | Size | Field |
| ---: | ---: | --- |
| `0x00` | 1 | reserved |
| `0x01` | 1 | seconds |
| `0x02` | 1 | minutes |
| `0x03` | 1 | hours |
| `0x04` | 1 | day |
| `0x05` | 1 | month |
| `0x06` | 2 | year |

The implementation creates timestamps from host time adjusted by subtracting
nine hours before converting with `gmtime`.

### Mode Flags

| Bit | Name | Meaning |
| --- | --- | --- |
| `0x0001` | readable | Entry may be read. |
| `0x0002` | writable | Entry may be written/deleted. |
| `0x0004` | executable | Executable/search permission. |
| `0x0008` | copy-prohibit | Copy protected. |
| `0x0010` | file | Entry is a file. |
| `0x0020` | subdir | Entry is a directory. |
| `0x0040` | create-dir | Open flag used internally to create directories. |
| `0x0080` | closed | Set when cleanly closed; cleared while a write is in progress. |
| `0x0200` | create-file | Open flag equivalent to create. |
| `0x0400` | creation marker | Set on created file and directory entries. |
| `0x0800` | PDA exec | PDA application marker. |
| `0x1000` | PS1 | PlayStation-format data marker. |
| `0x2000` | hidden | Hidden from normal directory listing. |
| `0x8000` | exists | Entry exists. |

New normal directories are created with mode `0x8427` plus selected hidden,
PS1, and PDA bits. Root `..` is created hidden as `0xa426`.

New normal files are created with mode `0x8417` plus selected hidden, PS1, and
PDA bits. Newly created empty files have `cluster = -1` until the first data
cluster is allocated.

## Directory Structure

Directories are ordinary FAT chains whose contents are directory entries.
Directory `length` is an entry count, not a byte count.

Entry 0 of every directory is `.`. Entry 1 is `..`.

For root:

- `.` has name `.`, cluster 0, dir_entry 0, length 2, mode `0x8427`.
- `..` has name `..`, cluster 0, dir_entry 0, hidden mode `0xa426`.

For a non-root directory:

- The directory's parent entry points to the directory's relative cluster and
  has length 2 at creation.
- The directory's `.` entry stores the parent directory cluster in `cluster`
  and the directory's entry index in the parent in `dir_entry`.
- The directory's `..` entry stores the grandparent/back-reference information
  copied from the parent's `.` entry.

This back-reference design is why the `cluster` field in dot entries does not
mean "this directory's own cluster". It is used for walking back up the tree and
for reconstructing current paths.

When a new entry is created:

1. The parent directory is searched for a free slot.
2. If no slot exists at the current `length`, the directory FAT chain may be
   extended.
3. The parent directory `length` is incremented when allocation extends its
   logical entry count.
4. The new entry is written at the chosen entry index.

Directory free-space reporting counts missing/deleted entries plus one extra
slot when `length` is odd, because 1024-byte clusters hold two 512-byte entries.

## Paths And Names

Path separator is `/`.

- Absolute paths begin with `/` and start at root directory entry `(cluster 0,
  entry 0)`.
- Relative paths begin at the current directory state stored in memory.
- `.` and `..` are resolved through directory entries, not by purely textual
  path normalization.

Names are stored in a 32-byte field. The implementation commonly copies up to
32 bytes. Some paths ensure byte 31 is NUL during renaming/status changes, but
creation uses `strncpy(name, p, 32)` without forcing a terminator. Consumers
should treat the name field as a fixed 32-byte field and stop at the first NUL
if present.

Normal `McGetDir` listing skips entries without `exists`, skips hidden entries,
and supports `?` and `*` wildcard matching in the requested final path
component.

The lookup code contains a special comparison path for names of length at least
11, first comparing bytes from offset 10 onward. This behavior should be
preserved by compatibility-focused tools, but it should not be mistaken for a
different on-disk name field.

## File I/O

For regular files:

- Directory `length` is the file byte size.
- Directory `cluster` is the first relative cluster in the FAT chain, or `-1`
  for a new empty file.
- Reads clamp to `length`.
- Seeking can move beyond EOF; writing past current allocated clusters extends
  the FAT chain.
- The current file position maps to a chain index with
  `position / cluster_size`.

Read seek behavior:

1. Start from the file's first relative cluster.
2. Follow FAT entries until the cluster containing `position` is reached.
3. A FAT value `>= -1` while more clusters are needed is treated as no further
   allocated cluster.
4. The physical cluster read is `alloc_offset + relative_cluster`.

Write behavior:

1. On first write, the implementation updates the directory entry and flushes.
2. If the current seek target has no allocated cluster, `mcfat_fatWseek`
   allocates one and links it.
3. Data is copied into the cached cluster and the cache entry is marked dirty.
4. File size grows to the maximum written position.
5. Close/update writes the file's starting cluster and final size back into the
   directory entry, updates modified time, and toggles the closed bit.

The `closed` flag is cleared while a write is active and set when a file is
cleanly closed without a pending write state.

## Formatting Algorithm

The formatter performs this sequence:

1. Initialize or preserve bad block information depending on existing format
   state and version.
2. Write magic and version `1.2.0.0` into the superblock fields.
3. Clear/probe the low reserved clusters.
4. Compute `fat_length` and `ifc_length`.
5. Allocate IFC clusters, skipping bad/unusable physical clusters.
6. Allocate FAT clusters, writing their absolute cluster numbers into the IFC
   clusters.
7. Set `alloc_offset` to the physical cluster after the last FAT cluster.
8. Choose two usable backup erase blocks by scanning downward from the end of
   the card. The first usable block found becomes `backup_block1`; the second
   becomes `backup_block2`.
9. Erase `backup_block2`.
10. Initialize FAT entries for allocatable clusters up to the block before
    `backup_block2`.
11. Mark the first usable allocatable cluster as root directory cluster 0 and
    end-of-chain.
12. Mark other usable allocatable clusters as free.
13. Mark bad clusters as `0xfffffffd`.
14. Write the superblock.
15. Create the root directory entries.
16. Flush all dirty cache entries.

The implementation computes an allocation cap approximately as:

```text
allocatable_clusters_per_card = floor(clusters_per_card / 1000) * 1000 + 1
```

It uses this during formatting and mount-time recomputation of
`max_allocatable_clusters`.

## Cache Flush, Atomicity, And Backup Blocks

Writes are erase-block oriented. A dirty 1024-byte cluster cannot be safely
written alone without considering the rest of its erase block.

When flushing a dirty cluster:

1. The implementation builds a complete page list for the target erase block.
   Cached clusters contribute their current cache data. Missing clusters are
   read from media into a backup buffer.
2. If needed, it erases `backup_block1` and computes ECC over the complete page
   list.
3. It writes a marker to page 0 of `backup_block2`. Word 0 contains
   `target_block | 0x80000000`.
4. It writes the complete target-block page list into `backup_block1`.
5. It writes a second marker to page 1 of `backup_block2`.
6. It erases the actual target block.
7. It writes pages back to the target block.
8. It clears dirty flags.
9. It erases `backup_block2` to mark the operation clean.

Mount-time recovery checks `backup_block2`:

- If both first marker pages are erased, no recovery is needed.
- If only the first marker exists and the second is erased, the implementation
  treats the backup as incomplete and erases `backup_block2`.
- Otherwise, it treats the previous write as interrupted, restores the target
  block named by marker page 0 from `backup_block1`, and then erases
  `backup_block2`.

The target block number is stored with the high bit set in marker page 0, but
mount-time recovery masks the high bit before using it.

## ECC

ECC is calculated for each 128-byte slice of a page. Each slice gets three ECC
bytes. For a 512-byte page, four slices require 12 bytes. The spare area is
16 bytes, leaving implementation-specific unused spare bytes.

On reads with `CF_USE_ECC`, if the last spare byte indicates an erased page, ECC
correction is skipped. Otherwise, each 128-byte slice is checked. Single-bit
data errors can be corrected in place. Uncorrectable results cause page read
failure after retries.

## Bad Blocks

Bad blocks are tracked by erase block number in `bad_block_list`; unused slots
are `-1`. The implementation uses only the first 16 entries for several active
checks and replacement paths, even though the on-disk field reserves 32 entries.

Bad clusters in the FAT are marked `0xfffffffd`.

When a write discovers a failing block:

1. The complete intended block contents can be staged in `backup_block1`.
2. `mcfat_badblock` records the failing erase block.
3. Replacement clusters are allocated from the remaining allocatable area.
4. FAT entries, IFC references, and directory entry starting clusters are
   rewritten from the bad block's clusters to replacement clusters.
5. Clusters in the bad block are marked bad in the FAT.
6. The superblock is rewritten with updated bad block metadata.

The implementation refuses to replace the block containing the first allocatable
cluster/root area.

## Known Implementation-Derived Cautions

These are important for writing compatible tools:

- Treat only superblock bytes 0x000..0x14f as required on disk. The current
  implementation may write more, but it does not trust the tail on mount.
- `bad_block_list` reserves 32 entries, but much active logic only scans 16.
- Free FAT entries are not only `0x7fffffff`; any non-negative value is treated
  as free.
- Directory entry `length` changes meaning by entry type: bytes for files,
  entry count for directories.
- Dot entries use `cluster` and `dir_entry` as back-references. Do not interpret
  every directory entry's `cluster` field the same way.
- Names are fixed 32-byte fields and are not always guaranteed to be NUL
  terminated by creation code.
- The current code contains pointer-arithmetic and copy quirks around writing
  `MCDevInfo`. The interoperable interpretation is the observed mount behavior,
  not every byte the implementation may currently dump from memory.

## Minimal Reader Algorithm

To implement a read-only parser:

1. Read page 0.
2. Validate magic and supported version.
3. Parse fields through `bad_block_list`.
4. Derive `pages_per_cluster`, `clusters_per_block`, and `cluster_size` from
   geometry if needed.
5. Recover or reject unclean backup state if write recovery is in scope.
6. Read root entry 0 from relative cluster 0 and require name `.`.
7. For directory traversal, read directory entry index `i` by following the
   directory FAT chain from its starting relative cluster. Each cluster contains
   `cluster_size / 512` entries.
8. For files, read the chain starting at the file entry's relative cluster until
   `length` bytes have been produced or `0xffffffff` is reached.

## Minimal Writer Requirements

A writer must additionally:

1. Preserve block-level atomic update semantics or provide an equivalent safe
   strategy.
2. Maintain the two-level IFC/FAT structure.
3. Use high-bit FAT links for allocated non-terminal chain entries.
4. Reserve newly allocated terminal clusters as `0xffffffff`.
5. Update directory entry length, start cluster, modified time, and closed bit.
6. Extend parent directory length when adding entries beyond current logical
   length.
7. Maintain root `.` and `..` invariants.
8. Handle bad clusters and backup blocks, or explicitly operate only on images
   where bad block handling is disabled/out of scope.

## Implementation Source Map

The main implementation evidence for this document is:

| Subject | Source |
| --- | --- |
| Public API, mode flags, directory entry structs | `lib/include/mcfat.h` |
| Runtime device info, superblock structure, cache structs | `lib/src/mcfat-internal.h` |
| Mount validation and superblock parsing | `mcfat_setdevinfos` in `lib/src/main.c` |
| Geometry derivation | `McGetCardSpec` in `lib/src/mcsio2.c` and `mcfat_setdevspec` in `lib/src/main.c` |
| Formatting and initial layout | `mcfat_format2` in `lib/src/ps2mc_fio.c` |
| Directory entry creation | `McCreateDirentry` in `lib/src/main.c` |
| FAT indexing and values | `McGetFATentry`, `McSetFATentry`, `mcfat_findfree2`, `mcfat_fatRseek`, and `mcfat_fatWseek` |
| Directory lookup and listing | `mcfat_cachedirentry`, `mcfat_getdirinfo`, and `mcfat_getdir2` |
| File read/write behavior | `mcfat_read2`, `mcfat_write2`, and `mcfat_close2` |
| Cache flush and backup-block recovery | `mcfat_flushcacheentry` and `mcfat_checkBackupBlocks` |
| ECC | `McDataChecksum`, `mcfat_correctdata`, `McReadPage`, and `McWritePage` |
| Bad block replacement | `mcfat_replaceBackupBlock`, `mcfat_fillbackupblock1`, and `McReplaceBadBlock` |
