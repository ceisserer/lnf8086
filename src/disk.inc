; ===========================================================================
; disk.inc - drive parameters, sector cache, FAT access, directory access
; All routines expect DS = ES = CS.  8086 instruction set only.
; Cluster numbers are 32 bit (DX:AX) so that FAT32 works; on FAT12/FAT16
; the high word is always 0.
; ===========================================================================

; ---------------------------------------------------------------------------
; Sector cache descriptors
;   +0 valid(b) +1 dirty(b) +2 sector(dd) +6 buffer(dw) +8 isfat(b)
; For the FAT cache the sector is relative to the start of the first FAT;
; dirty FAT sectors are written to every FAT copy.
; ---------------------------------------------------------------------------
C_VALID equ 0
C_DIRTY equ 1
C_SEC   equ 2
C_BUF   equ 6
C_ISFAT equ 8

; [dv_ext]: how sectors are read and written
IO_INT25 equ 0                  ; INT 25h/26h, classic (< 32 MB)
IO_EXT   equ 1                  ; INT 25h/26h, packet (CX=-1)
IO_7305  equ 2                  ; INT 21h/7305h (FAT32: DOS 7.1, FreeDOS)

; ---------------------------------------------------------------------------
; disk_io - read/write one sector of drive [dv_drive]
;   in : [io_sec] dword absolute sector, [io_buf] buffer, [io_write] 0/1
;   out: CF set on error
; ---------------------------------------------------------------------------
disk_io:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        push ds
        push es
        cmp byte [dv_ext],IO_INT25
        jne .ext
        cmp word [io_sec+2],0           ; small partition: classic call
        jne .ioerr
        mov al,[dv_drive]
        mov cx,1
        mov dx,[io_sec]
        mov bx,[io_buf]
        jmp short .go
.ext:   mov ax,[io_sec]
        mov [pkt_sec],ax
        mov ax,[io_sec+2]
        mov [pkt_sec+2],ax
        mov word [pkt_cnt],1
        mov ax,[io_buf]
        mov [pkt_buf],ax
        mov [pkt_buf+2],cs
        mov cx,0FFFFh
        mov bx,pkt_sec
        cmp byte [dv_ext],IO_7305
        je .x21
        mov al,[dv_drive]
.go:    DBG 'd'
        cmp byte [io_write],0
        jne .wr
        int 25h
        jmp short .done
.wr:    int 26h
.done:  pop bx                          ; discard flags left by INT 25h/26h
        jmp short .out                  ; (pop/jmp keep CF)
.x21:   DBG 'x'
        mov dl,[dv_drive]
        inc dl
        mov si,0                        ; read
        cmp byte [io_write],0
        je .x21r
        inc si                          ; write, "other" data
.x21r:  mov ax,7305h
        int 21h
        jmp short .out
.ioerr: stc
.out:   DBG 'e'
        pop es
        pop ds
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; ---------------------------------------------------------------------------
; cache_write - write back cache SI (assumed valid & dirty); clears dirty
; ---------------------------------------------------------------------------
cache_write:
        push ax
        push cx
        push dx
        mov ax,[si+C_BUF]
        mov [io_buf],ax
        mov byte [io_write],1
        mov ax,[si+C_SEC]
        mov dx,[si+C_SEC+2]
        cmp byte [si+C_ISFAT],0
        je .single
        add ax,[dv_fatstart]
        adc dx,[dv_fatstart+2]
        xor ch,ch
        mov cl,[dv_nfats]
.fatlp: mov [io_sec],ax
        mov [io_sec+2],dx
        call disk_io
        jc .err
        add ax,[dv_fatsz]
        adc dx,[dv_fatsz+2]
        loop .fatlp
        jmp short .ok
.single:
        mov [io_sec],ax
        mov [io_sec+2],dx
        call disk_io
        jc .err
.ok:    mov byte [si+C_DIRTY],0
        clc
.err:   pop dx
        pop cx
        pop ax
        ret

; ---------------------------------------------------------------------------
; cache_get - make sector DX:AX available in cache SI
;   out: BX = buffer, CF on error
; ---------------------------------------------------------------------------
cache_get:
        cmp byte [si+C_VALID],0
        je .load
        cmp ax,[si+C_SEC]
        jne .load
        cmp dx,[si+C_SEC+2]
        jne .load
        mov bx,[si+C_BUF]
        clc
        ret
.load:  cmp byte [si+C_VALID],0
        je .rd
        cmp byte [si+C_DIRTY],0
        je .rd
        call cache_write
        jc .ret
.rd:    mov byte [si+C_VALID],0
        mov [si+C_SEC],ax
        mov [si+C_SEC+2],dx
        push ax
        push dx
        cmp byte [si+C_ISFAT],0
        je .abs
        add ax,[dv_fatstart]
        adc dx,[dv_fatstart+2]
.abs:   mov [io_sec],ax
        mov [io_sec+2],dx
        mov bx,[si+C_BUF]
        mov [io_buf],bx
        mov byte [io_write],0
        call disk_io
        pop dx
        pop ax
        jc .ret
        mov byte [si+C_VALID],1
        mov byte [si+C_DIRTY],0
.ret:   ret

; ---------------------------------------------------------------------------
; cache_flush_all - write back dirty caches.  CF on error.
; cache_drop_all  - flush and invalidate both caches
; ---------------------------------------------------------------------------
cache_flush_all:
        push si
        mov si,c_dir
        call .one
        jc .x
        mov si,c_fat
        call .one
.x:     pop si
        ret
.one:   cmp byte [si+C_VALID],0
        je .clean
        cmp byte [si+C_DIRTY],0
        je .clean
        jmp cache_write
.clean: clc
        ret

cache_drop_all:
        call cache_flush_all
        pushf
        mov byte [c_dir+C_VALID],0
        mov byte [c_fat+C_VALID],0
        popf
        ret

; ---------------------------------------------------------------------------
; dos_sync - write our dirty sectors, forget them, let DOS flush/discard its
; buffers (INT 21h/0Dh).  Used around every DOS call that touches a disk.
; ---------------------------------------------------------------------------
dos_sync:
        push ax
        call cache_drop_all
        pushf
        mov ah,0Dh
        int 21h
        popf
        pop ax
        ret

; ---------------------------------------------------------------------------
; drive_setup - load parameters of physical drive AL (0=A:)
;   out: CF set if the drive is not a FAT12/FAT16/FAT32 disk we can handle
; ---------------------------------------------------------------------------
drive_setup:
        push ax
        push bx
        push cx
        push dx
        push si
        cmp byte [dv_ok],0
        je .new
        cmp al,[dv_drive]
        jne .new
        cmp byte [dv_fixed],0
        je .new
        clc                             ; cached fixed disk
        jmp .ret
.new:   call cache_drop_all
        mov byte [dv_ok],0
        mov [dv_drive],al
        ; removable?  (IOCTL 4408h: AX=0 removable, 1 fixed)
        mov bl,al
        inc bl
        mov ax,4408h
        int 21h
        mov byte [dv_fixed],0
        jc .rdboot
        cmp ax,1
        jne .rdboot
        mov byte [dv_fixed],1
.rdboot:
        ; boot sector: the classic call, the extended one, then 7305h
        mov word [io_sec],0
        mov word [io_sec+2],0
        mov word [io_buf],dirbuf
        mov byte [io_write],0
        mov byte [dv_ext],IO_INT25
.rdtry: call disk_io
        jnc .gotbs
        inc byte [dv_ext]
        cmp byte [dv_ext],IO_7305
        jbe .rdtry
        jmp .bad
.gotbs: mov si,dirbuf
        cmp word [si+0Bh],512           ; bytes per sector
        jne .bad
        mov al,[si+0Dh]                 ; sectors per cluster (power of 2)
        or al,al
        jz .bad
        mov [dv_spc],al
        xor cl,cl
.shf:   shr al,1
        jc .shfd
        inc cl
        jmp short .shf
.shfd:  or al,al                        ; more bits left -> not a power of 2
        jnz .bad
        mov [dv_spcshift],cl
        mov ax,[si+0Eh]                 ; reserved sectors
        or ax,ax
        jz .bad
        mov [dv_fatstart],ax
        mov word [dv_fatstart+2],0
        mov al,[si+10h]                 ; number of FATs
        or al,al
        jz .bad
        cmp al,4
        ja .bad
        mov [dv_nfats],al
        xor ax,ax
        mov [dv_fatsz+2],ax
        mov [dv_rootclus],ax
        mov [dv_rootclus+2],ax
        mov [dv_datastart+2],ax
        mov [dv_maxclust+2],ax
        mov [dv_fat32],al
        mov ax,[si+16h]                 ; sectors per FAT
        or ax,ax
        jnz .f1216
        jmp .f32                        ; 0 -> FAT32 BPB
.f1216: mov [dv_fatsz],ax
        mov ax,[si+11h]                 ; root entries
        or ax,ax
        jz .bad
        mov [dv_rootents],ax
        ; root start = reserved + nfats*fatsz
        xor ah,ah
        mov al,[dv_nfats]
        mul word [dv_fatsz]
        or dx,dx
        jnz .bad
        add ax,[dv_fatstart]
        jc .bad
        mov [dv_rootstart],ax
        mov bx,[dv_rootents]
        add bx,15
        mov cl,4
        shr bx,cl
        add ax,bx
        jc .bad
        mov [dv_datastart],ax
        ; total sectors
        mov ax,[si+13h]
        xor dx,dx
        or ax,ax
        jnz .tot
        mov ax,[si+20h]
        mov dx,[si+22h]
.tot:   call .nclus
        jc .bad
        ; DX:AX = number of data clusters
        or dx,dx
        jnz .bad
        cmp ax,65525
        jae .bad                        ; too many for FAT16
        cmp ax,1
        jb .bad
        mov byte [dv_fat16],0
        cmp ax,4085
        jb .f12
        mov byte [dv_fat16],1
.f12:   inc ax
        mov [dv_maxclust],ax            ; highest valid cluster number
        mov bx,26h                      ; extended BPB
        jmp .serial

.bad:   mov byte [dv_ok],0
        stc
        jmp .ret

.f32:   ; ---- FAT32
        cmp word [si+11h],0             ; no fixed root directory
        jne .bad
        mov [dv_rootents],ax            ; AX = 0
        mov [dv_rootstart],ax
        mov ax,[si+24h]                 ; sectors per FAT (32 bit)
        mov dx,[si+26h]
        mov [dv_fatsz],ax
        mov [dv_fatsz+2],dx
        or ax,dx
        jz .bad
        ; data start = reserved + nfats*fatsz
        mov ax,[dv_fatstart]
        xor dx,dx
        xor ch,ch
        mov cl,[dv_nfats]
.fadd:  add ax,[dv_fatsz]
        adc dx,[dv_fatsz+2]
        jc .bad
        loop .fadd
        mov [dv_datastart],ax
        mov [dv_datastart+2],dx
        ; FAT mirroring off (flags bit 7): only the active FAT (bits 0-3)
        mov al,[si+28h]
        test al,80h
        jz .mirr
        and al,0Fh
        cmp al,[dv_nfats]
        jae .bad
        mov byte [dv_nfats],1
        xor ah,ah
        mov cx,ax
        jcxz .mirr
.act:   mov ax,[dv_fatsz]
        add [dv_fatstart],ax
        mov ax,[dv_fatsz+2]
        adc [dv_fatstart+2],ax
        loop .act
.mirr:  mov ax,[si+2Ch]                 ; root directory cluster
        mov [dv_rootclus],ax
        mov ax,[si+2Eh]
        mov [dv_rootclus+2],ax
        ; total sectors
        mov ax,[si+13h]
        xor dx,dx
        or ax,ax
        jnz .tot32
        mov ax,[si+20h]
        mov dx,[si+22h]
.tot32: call .nclus
        jc .bad
        mov cx,ax
        or cx,dx
        jz .bad
        add ax,1                        ; highest valid cluster number
        adc dx,0
        cmp dx,0FFFh                    ; at most 0FFFFFF6h
        jb .mc
        ja .bad
        cmp ax,0FFF6h
        ja .bad
.mc:    mov [dv_maxclust],ax
        mov [dv_maxclust+2],dx
        mov ax,[dv_rootclus]
        mov dx,[dv_rootclus+2]
        call clus_ok
        jc .bad
        mov byte [dv_fat32],1
        mov byte [dv_fat16],1
        mov bx,42h                      ; extended BPB (FAT32 position)

.serial:
        ; volume serial number (extended BPB at offset BX)
        xor ax,ax
        mov [dv_serial],ax
        mov [dv_serial+2],ax
        cmp byte [si+bx],29h
        jne .noser
        mov ax,[si+bx+1]
        mov [dv_serial],ax
        mov ax,[si+bx+3]
        mov [dv_serial+2],ax
.noser: mov word [dv_freehint],2
        mov word [dv_freehint+2],0
        ; FAT32 volumes are read and written with INT 21h/7305h where DOS
        ; offers it (MS-DOS 7.1 refuses INT 25h/26h on FAT32 drives)
        cmp byte [dv_fat32],0
        je .ok
        cmp byte [dv_ext],IO_7305
        je .ok
        mov al,[dv_ext]
        mov byte [dv_ext],IO_7305
        call disk_io                    ; boot sector again (into dirbuf)
        jnc .ok
        mov [dv_ext],al
.ok:    mov byte [dv_ok],1
        clc
.ret:   pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; .nclus - DX:AX = total sectors -> DX:AX = number of data clusters
;   CF if the data area would start behind the end of the disk
.nclus: sub ax,[dv_datastart]
        sbb dx,[dv_datastart+2]
        jc .ncx
        mov cl,[dv_spcshift]
        or cl,cl
        jz .ncx
.cdiv:  shr dx,1
        rcr ax,1
        dec cl
        jnz .cdiv
        clc
.ncx:   ret

; ---------------------------------------------------------------------------
; clus_ok - CF set unless DX:AX is a valid cluster number (2..maxclust)
; ---------------------------------------------------------------------------
clus_ok:
        or dx,dx
        jnz .hi
        cmp ax,2
        jb .no
.hi:    cmp dx,[dv_maxclust+2]
        jb .yes
        ja .no
        cmp ax,[dv_maxclust]
        ja .no
.yes:   clc
        ret
.no:    stc
        ret

; ---------------------------------------------------------------------------
; clus2sec - DX:AX = cluster -> DX:AX = first sector of cluster
; ---------------------------------------------------------------------------
clus2sec:
        push cx
        sub ax,2
        sbb dx,0
        mov cl,[dv_spcshift]
        or cl,cl
        jz .n
.l:     shl ax,1
        rcl dx,1
        dec cl
        jnz .l
.n:     add ax,[dv_datastart]
        adc dx,[dv_datastart+2]
        pop cx
        ret

; ---------------------------------------------------------------------------
; fat_locate - DX:AX = cluster -> sector (DX:AX relative to FAT) and
;   CX = byte offset in sector.  (internal helper)
; ---------------------------------------------------------------------------
fat_locate:
        cmp byte [dv_fat32],0
        je .f16
        mov cx,ax
        and cx,7Fh
        shl cx,1
        shl cx,1                        ; (cluster & 7Fh) * 4
        push cx
        mov cl,7
.sh:    shr dx,1
        rcr ax,1
        dec cl
        jnz .sh
        pop cx
        ret
.f16:   cmp byte [dv_fat16],0
        je .f12
        mov cx,ax
        xor ch,ch
        shl cx,1                        ; (cluster & 0FFh) * 2
        mov al,ah
        xor ah,ah                       ; cluster >> 8
        xor dx,dx
        ret
.f12:   mov cx,ax
        shr cx,1
        add cx,ax                       ; cluster * 3 / 2 (< 6200)
        mov ax,cx
        and cx,511
        push cx
        mov cl,9
        shr ax,cl
        pop cx
        xor dx,dx
        ret

; ---------------------------------------------------------------------------
; fat_get - DX:AX = cluster -> DX:AX = FAT entry (EOC/bad normalized to
;   0FFFFFFFFh).  CF on I/O error.
; ---------------------------------------------------------------------------
fat_get:
        push bx
        push cx
        push si
        push di
        mov di,ax                       ; remember cluster (FAT12 parity)
        call fat_locate
        mov si,c_fat
        call cache_get
        jc .ret
        add bx,cx
        cmp byte [dv_fat32],0
        je .f16
        mov ax,[bx]
        mov dx,[bx+2]
        and dh,0Fh                      ; 28 bit entries
        cmp dx,0FFFh
        jne .ok
        cmp ax,0FFF7h
        jb .ok
        jmp short .eoc
.f16:   xor dx,dx
        cmp byte [dv_fat16],0
        je .f12
        mov ax,[bx]
        cmp ax,0FFF7h
        jb .ok
        jmp short .eoc
.f12:   mov cl,[bx]                     ; low byte
        cmp bx,fatbuf+511
        jne .same
        push cx
        inc ax                          ; next FAT sector (DX stays 0)
        call cache_get
        pop cx
        jc .ret
        mov ch,[bx]
        jmp short .got
.same:  mov ch,[bx+1]
.got:   mov ax,cx
        test di,1
        jz .even
        mov cl,4
        shr ax,cl
.even:  and ax,0FFFh
        cmp ax,0FF7h
        jb .ok
.eoc:   mov ax,0FFFFh
        mov dx,ax
.ok:    clc
.ret:   pop di
        pop si
        pop cx
        pop bx
        ret

; ---------------------------------------------------------------------------
; fat_set - set FAT entry of cluster DX:AX to value CX:BX.  CF on error.
;   (0FFFFFFFFh = end of chain; the value is cut to the FAT's width, the
;   reserved high 4 bits of a FAT32 entry are kept)
; ---------------------------------------------------------------------------
fat_set:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        mov [fs_val],bx
        mov [fs_val+2],cx
        mov di,ax                       ; cluster
        mov bp,bx                       ; value
        call fat_locate
        mov si,c_fat
        call cache_get
        jc .ret
        add bx,cx
        cmp byte [dv_fat32],0
        je .f16
        mov ax,[fs_val]
        mov [bx],ax
        mov ax,[fs_val+2]
        and ah,0Fh
        mov dx,[bx+2]
        and dx,0F000h
        or ax,dx
        mov [bx+2],ax
        mov byte [si+C_DIRTY],1
        clc
        jmp .ret
.f16:   cmp byte [dv_fat16],0
        je .f12
        mov [bx],bp
        mov byte [si+C_DIRTY],1
        clc
        jmp .ret
.f12:   and bp,0FFFh
        test di,1
        jz .ev
        ; odd: value occupies high nibble of byte0 and all of byte1
        mov cl,4
        shl bp,cl
        mov dl,[bx]
        and dl,0Fh
        mov cx,bp
        or dl,cl
        mov [bx],dl
        mov byte [si+C_DIRTY],1
        cmp bx,fatbuf+511
        jne .o_same
        xor dx,dx
        inc ax
        call cache_get
        jc .ret
        mov cx,bp
        mov [bx],ch
        mov byte [si+C_DIRTY],1
        clc
        jmp short .ret
.o_same:
        mov cx,bp
        mov [bx+1],ch
        clc
        jmp short .ret
.ev:    ; even: low byte = value low 8 bits, next byte low nibble = bits 8-11
        mov cx,bp
        mov [bx],cl
        mov byte [si+C_DIRTY],1
        cmp bx,fatbuf+511
        jne .e_same
        xor dx,dx
        inc ax
        call cache_get
        jc .ret
        mov cx,bp
        mov dl,[bx]
        and dl,0F0h
        or dl,ch
        mov [bx],dl
        mov byte [si+C_DIRTY],1
        clc
        jmp short .ret
.e_same:
        mov dl,[bx+1]
        and dl,0F0h
        or dl,ch
        mov [bx+1],dl
        clc
.ret:   pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; ---------------------------------------------------------------------------
; Directory access.  A directory is identified by its first cluster
; (0 = root directory; on FAT32 that is the root cluster from the BPB).
; Entries are addressed by index.
; ---------------------------------------------------------------------------

; dir_set - DX:AX = first cluster of directory to work on (preserved)
dir_set:
        cmp byte [dv_fat32],0
        je .set
        push ax
        or ax,dx
        pop ax
        jnz .set
        mov ax,[dv_rootclus]
        mov dx,[dv_rootclus+2]
        call .set
        xor ax,ax
        xor dx,dx
        ret
.set:   mov [d_clus],ax
        mov [d_clus+2],dx
        mov [d_cc],ax
        mov [d_cc+2],dx
        mov word [d_ci],0
        ret

; DIRSET var - dir_set with the dword cluster at var (AX changed)
%macro DIRSET 1
        call dir_set_at
        dw %1
%endmacro

; dir_set_at - dir_set with the dword at the address that follows the call
dir_set_at:
        push bp
        mov bp,sp
        push si
        push dx
        mov si,[bp+2]
        add word [bp+2],2
        mov si,[si]
        mov ax,[si]
        mov dx,[si+2]
        call dir_set
        pop dx
        pop si
        pop bp
        ret

; CLUSCPY dst, src - copy a dword cluster number (AX changed)
%macro CLUSCPY 2
        mov ax,[%2]
        mov [%1],ax
        mov ax,[%2+2]
        mov [%1+2],ax
%endmacro

; ent_clus - BX = directory entry -> DX:AX = its first cluster
ent_clus:
        mov ax,[bx+1Ah]
        xor dx,dx
        cmp byte [dv_fat32],0
        je .x
        mov dx,[bx+14h]
.x:     ret

; dir_ptr - AX = entry index -> BX = pointer to the 32-byte entry in the
;   directory cache.  CF set if the index lies beyond the end of the
;   directory (or on I/O error; then [dir_ioerr] = 1).
dir_ptr:
        push ax
        push cx
        push dx
        push si
        mov byte [dir_ioerr],0
        mov si,ax                       ; index
        cmp word [d_clus+2],0
        jne .sub
        cmp word [d_clus],0
        jne .sub
        cmp ax,[dv_rootents]
        jae .end
        mov cl,4
        shr ax,cl
        add ax,[dv_rootstart]
        xor dx,dx
        jmp short .load
.sub:   mov cl,4
        shr ax,cl                       ; sector number inside directory
        mov dx,ax
        mov cl,[dv_spcshift]
        shr ax,cl                       ; cluster index
        push ax
        mov ax,1
        shl ax,cl
        dec ax
        and dx,ax                       ; sector inside cluster
        pop ax
        mov cx,ax                       ; wanted cluster index
        cmp cx,[d_ci]
        jae .walk
        mov ax,[d_clus]
        mov [d_cc],ax
        mov ax,[d_clus+2]
        mov [d_cc+2],ax
        mov word [d_ci],0
.walk:  cmp cx,[d_ci]
        je .have
        push dx
        mov ax,[d_cc]
        mov dx,[d_cc+2]
        call fat_get
        jc .ioep
        call clus_ok
        jc .endp
        mov [d_cc],ax
        mov [d_cc+2],dx
        pop dx
        inc word [d_ci]
        jmp short .walk
.ioep:  pop dx
        jmp short .ioe
.endp:  pop dx
        jmp short .end
.have:  push dx
        mov ax,[d_cc]
        mov dx,[d_cc+2]
        call clus2sec
        pop cx
        add ax,cx
        adc dx,0
.load:  push si
        mov si,c_dir
        call cache_get
        pop si
        jc .ioe
        mov ax,si
        and ax,15
        mov cl,5
        shl ax,cl
        add bx,ax
        clc
        jmp short .ret
.ioe:   mov byte [dir_ioerr],1
.end:   stc
.ret:   pop si
        pop dx
        pop cx
        pop ax
        ret

; dir_dirty - mark the directory cache sector as modified
dir_dirty:
        mov byte [c_dir+C_DIRTY],1
        ret

; ---------------------------------------------------------------------------
; dir_extend - append one zeroed cluster to the current directory (not
;   the fixed FAT12/FAT16 root).  CF on error (disk full / I/O).
; ---------------------------------------------------------------------------
dir_extend:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        mov ax,[d_clus]
        mov dx,[d_clus+2]
        call clus_ok
        jc .fail
        ; find last cluster of the chain
.last:  mov [xd_last],ax
        mov [xd_last+2],dx
        call fat_get
        jc .fail
        call clus_ok
        jnc .last
        ; search a free cluster, starting at the hint
        mov ax,[dv_maxclust]
        mov dx,[dv_maxclust+2]
        sub ax,1                        ; number of clusters to try
        sbb dx,0
        mov [xd_left],ax
        mov [xd_left+2],dx
        mov ax,[dv_freehint]
        mov dx,[dv_freehint+2]
.scan:  call clus_ok
        jnc .chk
        mov ax,2
        xor dx,dx
.chk:   mov [xd_cand],ax
        mov [xd_cand+2],dx
        call fat_get
        jc .fail
        or ax,dx
        jz .found
        mov ax,[xd_cand]
        mov dx,[xd_cand+2]
        add ax,1
        adc dx,0
        sub word [xd_left],1
        sbb word [xd_left+2],0
        mov bx,[xd_left]
        or bx,[xd_left+2]
        jnz .scan
.fail:  stc
        jmp .ret
.found: ; xd_cand = free cluster
        mov ax,[xd_cand]
        mov dx,[xd_cand+2]
        mov bx,0FFFFh
        mov cx,bx
        call fat_set                    ; end of chain
        jc .fail
        mov bx,ax
        mov cx,dx
        mov ax,[xd_last]
        mov dx,[xd_last+2]
        call fat_set                    ; link
        jc .fail
        mov ax,[xd_cand]
        mov dx,[xd_cand+2]
        add ax,1
        adc dx,0
        mov [dv_freehint],ax
        mov [dv_freehint+2],dx
        call cache_flush_all
        jc .fail
        ; zero the new cluster through the directory cache buffer
        mov byte [c_dir+C_VALID],0
        mov di,dirbuf
        mov cx,256
        xor ax,ax
        rep stosw
        mov ax,[xd_cand]
        mov dx,[xd_cand+2]
        call clus2sec
        xor ch,ch
        mov cl,[dv_spc]
        mov word [io_buf],dirbuf
        mov byte [io_write],1
.zl:    mov [io_sec],ax
        mov [io_sec+2],dx
        call disk_io
        jc .fail
        add ax,1
        adc dx,0
        loop .zl
        ; restart cluster cache of the directory walker
        mov ax,[d_clus]
        mov dx,[d_clus+2]
        call dir_set
        call free_unknown
        clc
.ret:   pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; ---------------------------------------------------------------------------
; free_unknown - we allocated a cluster behind DOS' back: mark the free
;   cluster count in DOS' drive parameter block as unknown (DOS 4+ layout,
;   offset 1Fh; DOS 3.x keeps it at 1Eh).  FAT32: INT 21h/7304h function 0
;   (set free count and next free cluster to "unknown"), which also updates
;   the FSINFO sector.
; ---------------------------------------------------------------------------
free_unknown:
        push ax
        push bx
        push dx
        push ds
        mov dl,[dv_drive]
        inc dl
        cmp byte [dv_fat32],0
        jne .f32
        mov bh,[cs:dosmajor]
        mov ah,32h
        int 21h
        cmp al,0FFh
        je .x
        cmp bh,4
        jb .d3
        mov word [bx+1Fh],0FFFFh
        jmp short .x
.d3:    mov word [bx+1Eh],0FFFFh
.x:     pop ds
        pop dx
        pop bx
        pop ax
        ret
.f32:   push cx
        push di
        push es
        push ds
        pop es
        mov ax,0FFFFh                   ; free count, next free: unknown
        mov [sdpb+8],ax
        mov [sdpb+10],ax
        mov [sdpb+12],ax
        mov [sdpb+14],ax
        call .sdpb
        jnc .f32x
        ; FreeDOS refuses "unknown": one cluster less than DOS counts now
        mov dl,[dv_drive]
        inc dl
        mov di,xdpb
        mov cx,XDPB_SZ
        mov ax,7302h
        int 21h
        jc .f32x
        mov ax,[xdpb+2+1Fh]             ; free clusters (dword)
        mov cx,[xdpb+2+21h]
        mov di,ax
        and di,cx
        inc di
        jz .f32x                        ; unknown anyway
        mov di,ax
        or di,cx
        jz .f32x
        sub ax,1
        sbb cx,0
        mov [sdpb+8],ax
        mov [sdpb+10],cx
        CLUSCPY sdpb+12,dv_freehint
        call .sdpb
.f32x:  pop es
        pop di
        pop cx
        jmp short .x
; INT 21h/7304h function 0 with the values in sdpb+8 (free count) and
; sdpb+12 (next free cluster)
.sdpb:  mov di,sdpb
        mov word [di],SDPB_SZ           ; size
        xor ax,ax
        mov [di+2],ax                   ; version
        mov [di+4],ax                   ; function 0
        mov [di+6],ax
        mov [di+16],ax                  ; reserved
        mov [di+18],ax
        mov [di+20],ax
        mov [di+22],ax
        mov dl,[dv_drive]
        inc dl
        mov cx,SDPB_SZ
        mov ax,7304h
        int 21h
        ret
