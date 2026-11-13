# LFNTSR – long file names for plain DOS

LFNTSR is a small TSR that adds the Windows 95 long file name API
(INT 21h, AX=71xxh) to DOS, in the spirit of DOSLFN and LFNDOS. Programs
that know this API (DJGPP and Open Watcom programs, many FreeDOS tools,
...) can then create, find, rename and
delete files with long names on FAT12/FAT16 disks. The names are
stored in standard VFAT format, so Windows, Linux and mtools read
them as usual, and the reverse also works.

* **8086/8088 compatible.** The source is assembled with `cpu 8086`, so
  it uses no 186/286/386 instructions. 32-bit sector numbers and the
  64-bit FILETIME arithmetic are built from 16-bit operations.
* **DOS 3.10 or newer** (MS-DOS, PC DOS, DR-DOS, FreeDOS, ...).
* Written for **NASM** (free, open source). There are no other build
  dependencies.
* About 23 KB resident with the default settings (19 KB with `/T:0`).
  It can be loaded high with `LH`/`LOADHIGH`.
* Programs that only know the plain DOS functions keep long names intact
  ("tunnelling", see below).

## Building

```bash
nasm -f bin -I src/ -o lfntsr.com src/lfntsr.asm
```

or simply `make`. `make check` runs the test suite (see below).

Ready-built packages are on the GitHub releases page: tagged releases,
and a `snapshot` pre-release that is rebuilt from every push to `main`
(`lfntsr-snapshot.zip`). To make a release, set `VERSION` in
`src/lfntsr.asm`, then push a matching tag (`v1.10` for `0110h`).
`tools/mkdist.sh VERSION` builds the same zip locally.

## Usage

```
LFNTSR [/U] [/F] [/Q] [/H:n] [/T:n]
  /U    uninstall
  /F    install even if a long file name API is already present
  /Q    quiet
  /H:n  number of find handles, 1-64 (default 8, 274 bytes each)
  /T:n  long names remembered for tunnelling, 0-32 (default 8,
        278 bytes each; 0 turns tunnelling off)
```

Put `LFNTSR` (or `LH LFNTSR /Q`) into `AUTOEXEC.BAT`. On startup it
reads the active code page (INT 21h/6601h) and the country upper-case
table (6502h). It refuses to load when Windows or another LFN driver
already provides the API, unless you give `/F`.

## Supported functions

| AX     | Function                                    | Notes |
|--------|---------------------------------------------|-------|
| 710Dh  | reset drive                                 | |
| 7139h  | create directory                            | |
| 713Ah  | remove directory                            | |
| 713Bh  | change directory                            | |
| 7141h  | delete file                                 | SI=1: wildcards + attribute masks |
| 7143h  | get/set attributes, times, size             | BL=0..8 |
| 7147h  | get current directory (long form)           | |
| 714Eh  | find first                                  | DOS or FILETIME time format |
| 714Fh  | find next                                   | |
| 71A1h  | find close                                  | 8 handles (`/H:n`), LRU reuse |
| 7156h  | rename / move file                          | also changing only the letter case |
| 7160h  | truename (CL=0 full, 1 short, 2 long path)  | CH bit 7 = don't expand SUBST |
| 716Ch  | extended open/create                        | |
| 71A0h  | get volume information                      | BX=4006h on FAT12/16 |
| 71A6h  | get file info by handle                     | |
| 71A7h  | FILETIME <-> DOS time conversion            | |
| 71A8h  | generate short name                         | |
| 71A9h  | server open/create                          | same as 716Ch |

Any other 71xxh call returns AX=7100h with carry set, as Windows does
for functions it doesn't support.

## How it works

LFNTSR hooks INT 21h. It handles AH=71h, and it also watches the plain DOS
create, delete and rename functions (see *Tunnelling* below). Each call runs on a private
stack. While a call is in progress it installs its own INT 24h handler
(critical errors return "fail") and an INT 23h handler that ignores
Ctrl-C, so DOS can't abort a program halfway through an update.

* **Path resolution.** A long path is converted into a short one by
  reading directories directly with INT 25h (both the classic and the
  >32 MB packet form). Long names are assembled from the VFAT entries
  and checked against the SFN checksum. The starting point comes from
  DOS itself (TRUENAME of `X:\` and the current directory), so SUBST'd
  drives work.
* **Doing the work.** Wherever possible the real operation is done by
  DOS on the short name (mkdir, rmdir, delete, rename, open, set
  attributes). LFNTSR then adds or removes the long name entries in
  front of the short entry with INT 26h, moving the short entry when
  there is no room. A new file with a long name is created
  differently: LFNTSR writes the long name entries and an empty short
  entry, then lets DOS open it.
* **Growing directories.** When a subdirectory needs more room, LFNTSR
  appends a cluster itself (FAT12 and FAT16, all FAT copies). It then
  marks the free-cluster count in DOS's drive parameter block as
  unknown, so DOS recounts.
* **Keeping DOS consistent.** Around every DOS call and direct disk
  access, LFNTSR calls INT 21h/0Dh. Both MS-DOS (4.0 source) and
  FreeDOS write dirty buffers *and invalidate all buffers* there, so
  DOS never works with stale directory or FAT sectors.
* **Short names** follow the Windows 95 rules: `Long File Name.txt` →
  `LONGFI~1.TXT`, then `~2` ..., then `LONGF~10.TXT`. Characters that
  are illegal in short names become `_`. A valid 8.3 name that contains
  lower case letters gets a long name entry to keep its case, as on
  Windows 95.
* **Unicode.** OEM ↔ Unicode tables for code pages 437, 737, 775, 850,
  852, 855, 857, 860, 861, 862, 863, 865, 866 and 869 are built in.
  Only the table for the active code page stays resident. Characters
  the code page can't show appear as `?`, and find-first/next sets the
  corresponding flag in CX.
* **Devices.** Names like `CON`, `NUL` or `LPT1.TXT` are recognized by
  walking the DOS device driver chain and are passed to DOS.

* **Find handles.** When all handles are in use, find first takes over
  the least recently used one. The handle number contains a generation
  count, so the program that still holds the old handle gets error 6
  (invalid handle) instead of the entries of another search. Programs
  that keep many searches open (a recursive find that keeps one search per
  directory level) need `/H:n`.
* **Windows 3.x (386 enhanced mode).** All DOS boxes share the one
  resident LFNTSR. While it works on a call it holds the Windows critical
  section (INT 2Fh, AX=1681h/1682h), so no other virtual machine can run
  LFNTSR code in between. A 71xxh call that arrives while another one is
  still in progress (from an interrupt handler) fails with error 5
  (access denied), not 7100h, so that the caller does not fall back to the
  short name function and lose the long name.

### Tunnelling and plain DOS calls

Many DOS and Windows 3.x programs save a file by writing a temporary file
and then deleting or renaming the old one, all with the plain DOS
functions. Without help the long name is lost, and on MS-DOS the old long
name entries stay behind as orphans (ScanDisk and `fsck` complain, and
they use up directory slots). LFNTSR therefore also watches these DOS
calls: create (3Ch, 5Bh, 6C00h), delete (41h, FCB 13h) and rename
(56h, FCB 17h). Like Windows 95 it then

* removes the long name entries of a file that DOS deleted or renamed.
  After an FCB call with wildcards it removes all orphaned long name
  entries of that directory;
* remembers the long name and creation time of such a file for 15
  seconds, keyed by drive, directory and short name;
* gives that long name and creation time back to a file that is created
  with the same short name in the same directory within these 15 seconds
  (3Ch, 5Bh, 6C00h), or that a plain rename (56h) gives that short name.
  This does not happen if the long name is in use again by then.

Example: Write's "Save As" over `Save Demo Document.wri` deletes
`SAVEDE~1.WRI` and creates it again. The new file is called
`Save Demo Document.wri` again.

For a create, LFNTSR writes the long name entries and an empty short entry
before DOS runs, and DOS then opens the existing empty file (5Bh is sent
to DOS as 3Ch, and a 6Ch "fail if it exists" as "open"). The caller gets
the same results as for a new file. DOS itself runs on the caller's stack
with the caller's INT 23h/24h handlers, as without LFNTSR. The 71xxh calls
do not use the tunnel. Remembered names live only in memory.

### Drives that are not FAT12/FAT16

Network drives, CD-ROMs (MSCDEX), FAT32 volumes and disks with sector
sizes other than 512 bytes are not modified directly. On these drives
the 71xxh calls are mapped to the normal DOS functions (short names
only). Programs keep working, and 71A0h reports that long names are not
supported (BX=0).

## Limitations

* No FAT32 long name support (FAT32 only exists in DOS 7.1 and FreeDOS;
  those drives are handled as described above).
* Single-byte code pages only (no DBCS).
* 71A6h returns attributes 20h and file index 0. FILETIME values are
  local time (there is no time zone information in DOS).
* 7142h (64-bit seek) and 71AAh (SUBST) are not implemented.
* Moving a directory to another directory fails, because DOS can't do it.
* LFNTSR is not reentrant: a 71xxh call made while another one is in
  progress (from an interrupt handler) fails with error 5. Plain DOS calls
  made at such a time go straight to DOS (no tunnelling).
* Windows for Workgroups 3.11 with 32-bit file access is not supported.
  With it, Windows' own file system handles the plain DOS calls of DOS
  boxes and Windows programs and caches the disk, while LFNTSR reads and
  writes directories directly with INT 25h/26h. Long names created this
  way may be lost, and directories may be damaged. Turn 32-bit file
  access off for drives you use with LFNTSR (32-bit *disk* access is
  fine). LFNTSR does not yet detect this case (DOSLFN switches to short
  names only there); this needs testing on WfW first.
* Windows programs that call INT 21h/71xxh from protected mode do not get
  long names: Windows 3.x does not translate their pointers for real mode
  DOS. DOSLFN includes a Windows driver (`LFNXLAT.386`) that does; LFNTSR
  does not.
  Programs that call through DPMI in real mode (like NewShell16) work.
* Tested under QEMU with FreeDOS (kernel 2040) and MS-DOS 6.20 (German),
  including SUBST drives, SHARE and RAMDRIVE. Not yet tested on real
  hardware, on an actual 8088, or on DR-DOS / PC DOS. Tunnelling and the
  plain DOS call handling (1.10) are so far tested on FreeDOS only, and
  the Windows critical section has not been tested under Windows yet.

## Tests

`tests/` contains an automated test suite. It boots FreeDOS in QEMU with
a FAT12 floppy, a FAT16 hard disk and a FAT32 hard disk, and runs a few
hundred API calls through the `LT.COM` test tool. If you provide an
MS-DOS 6.x boot floppy (built from your own setup disk 1 with
`tests/mkmsdos.sh`), the same tests also run on MS-DOS, plus tests
for SUBST, SHARE, RAMDRIVE and CHKDSK. The output is compared
with the expected output, and the disk images are checked with
`fsck.fat` and mtools from Linux. See [tests/README.md](tests/README.md).

## License

GNU General Public License, version 3 or later, see [LICENSE](LICENSE).
The FreeDOS boot floppy used by the tests
(`tests/fdboot.img`) is GPL software; see `tests/README.md`.
