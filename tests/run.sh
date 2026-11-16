#!/bin/sh
# Run a DOS batch file under FreeDOS in QEMU with LFNTSR loaded.
#   tests/run.sh BATCHFILE WORKDIR
# Needs: nasm, qemu-system-i386, mtools, mkfs.fat, sfdisk and a FreeDOS boot
# floppy image in $FDBOOT (kernel + COMMAND.COM; default: tests/fdboot.img).
# Result: WORKDIR/out.txt (A:\OUT.TXT), WORKDIR/fd.img, WORKDIR/hd.img
set -e
here=$(cd "$(dirname "$0")" && pwd)
top=$(dirname "$here")
bat=$1
work=${2:-/tmp/lfntest}
shift; shift || true
FDBOOT=${FDBOOT:-$here/fdboot.img}
mkdir -p "$work"
nasm -f bin ${NASMDEF} -I "$top/src/" -o "$work/LFNTSR.COM" "$top/src/lfntsr.asm"
nasm -f bin -o "$work/LT.COM" "$here/lt.asm"
printf 'mov al,0\nout 0xf4,al\nhlt\n' > "$work/exit.asm"
nasm -f bin -o "$work/EXIT.COM" "$work/exit.asm"
cp "$FDBOOT" "$work/fd.img"
# hard disk C:, one primary partition (16 heads, 63 sectors): FAT16 with
# ~40 MB, or with HDFAT=32 FAT32 with ~60 MB (1 sector per cluster)
rm -f "$work/hd.img"
if [ "$HDFAT" = 32 ]; then
    HDCYLS=${HDCYLS:-120}
    set -- $(python3 "$here/mkhd.py" "$work/hd.img" $HDCYLS 0x0B)
    SOURCE_DATE_EPOCH=946684800 mkfs.fat -F 32 -s 1 -h $1 -g 16/63 -S 512 --offset $1 -i 1234ABCD -n TESTDISK "$work/hd.img" $(($2 / 2)) >/dev/null
else
    set -- $(python3 "$here/mkhd.py" "$work/hd.img" ${HDCYLS:-81} 6)
    SOURCE_DATE_EPOCH=946684800 mkfs.fat -F 16 -h $1 -g 16/63 -S 512 --offset $1 -i 1234ABCD -n TESTDISK "$work/hd.img" $(($2 / 2)) >/dev/null
fi
echo "@@$(($1 * 512))" > "$work/hdoff"
# optional: HDFILL=MB puts a file of that size on C: first, so that what the
# tests create lies in clusters above 65535 (FAT32)
if [ -n "$HDFILL" ]; then
    dd if=/dev/zero of="$work/FILL.BIN" bs=1M count=$HDFILL 2>/dev/null
    mcopy -i "$work/hd.img@@$(($1 * 512))" "$work/FILL.BIN" ::FILL.BIN
    rm -f "$work/FILL.BIN"
fi
# optional preparation of the hard disk: $HDSETUP IMAGE@@OFFSET
if [ -n "$HDSETUP" ]; then $HDSETUP "$work/hd.img@@$(($1 * 512))"; fi
# optional second disk D: with FAT32
HD2=
if [ -n "$FAT32" ]; then
    rm -f "$work/hd2.img"
    set -- $(python3 "$here/mkhd.py" "$work/hd2.img" 120 0x0B)
    SOURCE_DATE_EPOCH=946684800 mkfs.fat -F 32 -s 1 -h $1 -g 16/63 -S 512 --offset $1 -i 5678ABCD -n FAT32DISK "$work/hd2.img" $(($2 / 2)) >/dev/null
    HD2="-drive file=$work/hd2.img,format=raw,if=none,id=hd1 -device ide-hd,drive=hd1,cyls=120,heads=16,secs=63"
fi
python3 -c 'import sys; d=open(sys.argv[1],encoding="utf-8").read(); open(sys.argv[2],"wb").write(d.replace("\n","\r\n").encode("cp437"))' "$bat" "$work/AUTOEXEC.BAT"
printf 'FILES=20\r\nBUFFERS=20\r\nLASTDRIVE=Z\r\nSHELL=A:\\COMMAND.COM /E:512 /P\r\n%b' "$CONFIGEXTRA" > "$work/CONFIG.SYS"
mdel -i "$work/fd.img" ::README.TXT 2>/dev/null || true
mcopy -o -i "$work/fd.img" "$work/AUTOEXEC.BAT" "$work/CONFIG.SYS" \
    "$work/LFNTSR.COM" "$work/LT.COM" "$work/EXIT.COM" ::
set +e
timeout ${QTIMEOUT:-120} qemu-system-i386 -display none -m 4 \
    -drive file="$work/fd.img",format=raw,if=floppy \
    -drive file="$work/hd.img",format=raw,if=none,id=hd0 -device ide-hd,drive=hd0,cyls=${HDCYLS:-81},heads=16,secs=63 \
    $HD2 -boot a -debugcon file:"$work/debug.txt" -device isa-debug-exit,iobase=0xf4 ${QEMUEXTRA}
rc=$?
set -e
if [ $rc -ne 1 ]; then echo "QEMU did not exit cleanly (rc=$rc)" >&2; fi
mcopy -n -o -i "$work/fd.img" ::OUT.TXT "$work/out.raw" || true
python3 -c 'import sys; open(sys.argv[2],"w",encoding="utf-8").write(open(sys.argv[1],"rb").read().decode("cp437").replace("\r",""))' "$work/out.raw" "$work/out.txt"
