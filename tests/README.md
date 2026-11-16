# Tests

`check.sh` boots FreeDOS in QEMU, loads LFNTSR and runs the batch files
`basic.bat` (FAT16 hard disk C: and FAT12 floppy A:), `pass.bat`
(FAT32 disk D:, including a growing FAT32 root directory, and a
rename across drives) and `tunnel.bat`
(plain DOS create/delete/rename: tunnelling, removal of long name
entries, find handles, `/H` and `/T`). `basic32` and `tunnel32` run
`basic.bat` and `tunnel.bat` again with a FAT32 disk C: whose first 40 MB
are taken by a file, so that the test objects lie in clusters above
65535. Each run is compared
with the expected output (`*.exp`). The test also checks the disk images
from Linux: `fsck.fat` must find no errors, `lfncheck.py` must find no
orphaned long name entries (`fsck.fat -n` does not report those), and
the long file names that mtools sees must match `*.files`.

Note that the FreeDOS kernel removes the long name entries of files it
deletes or renames itself; MS-DOS does not. `mkorphan.sh` therefore puts
orphaned long name entries on the disk before `tunnel.bat` runs, so that
LFNTSR's removal is tested on FreeDOS too.

    make check            # or: tests/check.sh
    tests/check.sh -u     # accept the current output as expected

MS-DOS: the tests can also run on MS-DOS 6.x. MS-DOS is not included;
build a boot floppy from your own copy of setup disk 1 and point
`MSDOS_IMG` at it:

    tests/mkmsdos.sh /path/to/144OEM1.IMG /tmp/msdos.img
    MSDOS_IMG=/tmp/msdos.img tests/check.sh

This adds `msdos-basic` (the basic tests on MS-DOS) and `msdos` (SUBST,
SHARE, RAMDRIVE, CHKDSK). `tunnel.bat` runs on FreeDOS only so far.

Requirements: `nasm`, `qemu-system-i386`, `mtools`, `dosfstools`
(`mkfs.fat`, `fsck.fat`), `python3`.

`lt.asm` builds `LT.COM`, a small command line front end for the
INT 21h/71xxh calls (`LT md "Long Name"`, `LT dir "*.txt"`, ...). It is
handy for experiments on a real machine too.

## fdboot.img

`fdboot.img` is an unmodified FreeDOS 1.44 MB boot floppy (FreeDOS kernel
build 2040 and FreeCOM 0.82pl3), taken from
<https://github.com/infernalheaven/freedosbootdisks>. FreeDOS is free
software under the GNU GPL; the sources are available from
<https://github.com/FDOS/kernel>, <https://github.com/FDOS/freecom> and
<https://www.freedos.org/>. Use `FDBOOT=/path/to/other.img` to test with
another boot disk.
