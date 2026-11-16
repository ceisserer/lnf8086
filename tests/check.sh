#!/bin/sh
# Run all emulator tests and compare with the expected results.
#   tests/check.sh [-u]     (-u: update the expected files)
# FreeDOS tests always run (boot floppy tests/fdboot.img).  MS-DOS tests run
# when MSDOS_IMG names an MS-DOS boot floppy made with tests/mkmsdos.sh.
here=$(cd "$(dirname "$0")" && pwd)
work=${WORK:-/tmp/lfntsr-check}
update=0; [ "$1" = "-u" ] && update=1
fail=0

compare() {     # compare FILE EXPECTED
    if [ $update = 1 ]; then cp "$1" "$2"; return; fi
    if ! diff -u "$2" "$1"; then echo "FAIL: $1 differs from $2"; fail=1; fi
}

fsck_img() {    # fsck_img IMAGE OFFSET_SECTORS [nolfn]
    [ -f "$1" ] || return 0
    dd if="$1" of="$1.part" bs=512 skip=$2 2>/dev/null
    if ! fsck.fat -n "$1.part" >"$1.fsck" 2>&1; then
        echo "FAIL: fsck.fat on $1"; cat "$1.fsck"; fail=1
    fi
    # fsck.fat -n does not report orphaned long name entries
    if [ "$3" != nolfn ] && ! python3 "$here/lfncheck.py" "$1.part"; then
        echo "FAIL: orphaned long name entries on $1"; fail=1
    fi
    rm -f "$1.part"
}

# run NAME BATCH BOOTIMAGE [VAR=VALUE ...]
run() {
    name=$1; bat=$2; boot=$3; shift 3
    echo "== $name"
    rm -rf "$work/$name"
    env FDBOOT="$boot" "$@" "$here/run.sh" "$here/$bat.bat" "$work/$name" || fail=1
    # volume label creation time (CHKDSK) depends on the day of the test
    sed -i '/erzeugt\|created/d' "$work/$name/out.txt"
    compare "$work/$name/out.txt" "$here/$name.exp"
    fsck_img "$work/$name/hd.img" 63
    fsck_img "$work/$name/fd.img" 0
    fsck_img "$work/$name/hd2.img" 63
    off=$(cat "$work/$name/hdoff")
    LC_ALL=C.UTF-8 mdir -/ -b -i "$work/$name/hd.img$off" :: | sort > "$work/$name/hd.files"
    compare "$work/$name/hd.files" "$here/$name.files"
}

run basic basic "$here/fdboot.img" FAT32=1
run pass pass "$here/fdboot.img" FAT32=1
run tunnel tunnel "$here/fdboot.img" HDSETUP="$here/mkorphan.sh"
run basic32 basic "$here/fdboot.img" HDFAT=32 HDFILL=40
run tunnel32 tunnel "$here/fdboot.img" HDFAT=32 HDFILL=40 HDSETUP="$here/mkorphan.sh"
if [ -n "$MSDOS_IMG" ]; then
    run msdos-basic basic "$MSDOS_IMG"
    run msdos msdos "$MSDOS_IMG" 'CONFIGEXTRA=DEVICE=A:\RAMDRIVE.SYS 256 512 64\r\n'
else
    echo "(MS-DOS tests skipped, set MSDOS_IMG)"
fi
[ $fail = 0 ] && echo "ALL TESTS PASSED"
exit $fail
