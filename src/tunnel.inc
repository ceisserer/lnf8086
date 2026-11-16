; ===========================================================================
; tunnel.inc - plain DOS create / delete / rename (3Ch 5Bh 6Ch 41h 56h, FCB
;   13h 17h): long name tunnelling and removal of long name entries
;
; Deleting or renaming a file by its short name through the plain DOS
; functions leaves its long name entries behind (orphans), and many DOS and
; Windows 3.x programs save by writing a temporary file and deleting or
; renaming the old one, which loses the long name.  Like Windows 95 we
;   * remove the long name entries of files deleted / renamed by DOS,
;   * remember the long name (and creation time) of such a file for about
;     15 seconds, keyed by drive, directory and short name ("tunnel"),
;   * give a file created (or renamed to) that short name in the same
;     directory within that time the remembered long name again.
;
; plain_pre runs before the DOS call, plain_post after it.  The state that
; plain_post needs is kept on the caller's stack (PS_N words), so another
; call in between (another DOS box under Windows) cannot disturb it.  The
; DOS call itself runs on the caller's stack with the caller's INT 23h/24h
; handlers and outside the Windows critical section, exactly as without
; LFNTSR.
; ===========================================================================

PS_N     equ 8                  ; state words: ps_flags .. ps_cx
PS_DX    equ 6                  ; index of ps_dx
PF_KILL  equ 01h                ; long name entries ps_a..ps_b-1 of entry ps_b
PF_SWEEP equ 02h                ; sweep the whole directory
PF_MADE  equ 04h                ; we created entries ps_a, ps_b entries long
PF_DX    equ 08h                ; restore the caller's DX (6Ch) from ps_dx
PF_RO    equ 10h                ; set read-only after the create
PF_DEST  equ 20h                ; rename: tunnel to the new name

; tunnel entry
T_USED  equ 0
T_DRIVE equ 1
T_CLUS  equ 2                   ; directory (dword)
T_TICK  equ 6                   ; BIOS tick count when recorded
T_CMS   equ 8                   ; creation time 10 ms
T_NAME  equ 9                   ; 11 byte short name
T_CTIME equ 20
T_CDATE equ 22
T_LONG  equ 24                  ; long name (OEM, NUL terminated)
TSIZE   equ T_LONG+256
T_TICKS equ 273                 ; 15 seconds at 18.2 ticks/s

; ---------------------------------------------------------------------------
; plain_pre - before the DOS call.  Stack: [ret] [PS_N state words] [frame]
; ---------------------------------------------------------------------------
plain_pre:
        push bp
        mov bp,sp
        mov word [bp+4],0               ; state: nothing to do afterwards
        pop bp
        call sess_enter
        jc .x
        xor ax,ax
        mov di,ps_vars
        mov cx,PS_N
        rep stosw
        call pre_work
        mov es,[u_ss]                   ; state -> caller's stack
        mov di,[u_sp]
        inc di
        inc di
        mov si,ps_vars
        mov cx,PS_N
        rep movsw
        call sess_leave
.x:     ret

; ---------------------------------------------------------------------------
; plain_post - after the DOS call.
;   Stack: [ret] [DOS flags] [PS_N state words] [IP CS FLAGS]
; ---------------------------------------------------------------------------
plain_post:
        push bp
        mov bp,sp
        push ax
        mov ax,[bp+4]                   ; CF of DOS -> caller's flags
        and ax,1
        and word [bp+6+PS_N*2+4],0FFFEh
        or [bp+6+PS_N*2+4],ax
        test word [bp+6],PF_DX
        jz .nodx
        mov dx,[bp+6+PS_DX*2]
.nodx:  cmp word [bp+6],0
        pop ax
        pop bp
        je .x
        call sess_enter
        jc .x
        mov si,[u_sp]
        push ds
        mov ds,[u_ss]
        lodsw                           ; our return address
        lodsw
        mov [cs:ps_dosf],ax
        mov di,ps_vars
        mov cx,PS_N
        rep movsw
        pop ds
        call post_work
        call sess_leave
.x:     ret

; ---------------------------------------------------------------------------
; pre_work - [u_ax+1] = DOS function
; ---------------------------------------------------------------------------
pre_work:
        mov al,[u_ax+1]
        cmp al,41h
        je pre_delete
        cmp al,56h
        je pre_rename
        cmp al,13h
        je pre_fcb
        cmp al,17h
        je pre_fcb
        jmp pre_create

; 41h delete (DS:DX)
pre_delete:
        mov byte [sc_skip],18h          ; files only
        call get_path_dsdx
        jc pre_ret
pre_scan_path:
        call dos_sync
        mov si,inpath
        mov al,RF_PATTERN
        call resolve
        jc pre_ret
        cmp byte [rp_mode],0
        jne pre_ret
        mov si,lastname
        call pat_to_11
        jc pre_ret
        jmp scan_matches
pre_ret:
        ret

; 56h rename (DS:DX old, ES:DI new)
pre_rename:
        mov byte [sc_skip],08h          ; files and directories
        or word [ps_flags],PF_DEST
        call get_path_dsdx
        jc pre_ret
        jmp short pre_scan_path

; 13h / 17h FCB delete / rename (DS:DX FCB, normal or extended)
pre_fcb:
        push ds
        mov si,[u_dx]
        mov ds,[u_ds]
        mov di,tmpent
        mov cx,7+12
        rep movsb
        pop ds
        mov si,tmpent
        mov ah,18h                      ; files only
        cmp byte [si],0FFh
        jne .norm
        test byte [si+6],10h
        jz .ext
        cmp byte [u_ax+1],17h
        jne .ext
        mov ah,08h                      ; rename with directory attribute
.ext:   add si,7
.norm:  mov [sc_skip],ah
        lodsb                           ; drive (0 = default)
        or al,al
        jnz .drv
        mov ah,19h
        int 21h
        inc al
.drv:   add al,'A'-1
        mov [inpath],al
        mov word [inpath+1],':*'
        mov byte [inpath+3],0
        mov di,pat11
        mov cx,11
.p:     lodsb
        call upcase
        stosb
        loop .p
        call pat_wildness
        call dos_sync
        mov si,inpath
        mov al,RF_PATTERN
        call resolve
        jc pre_ret
        cmp byte [rp_mode],0
        jne pre_ret
        jmp scan_matches

; 3Ch / 5Bh create (DS:DX, CX attributes), 6C00h extended open (DS:SI,
; BX mode, CX attributes, DX action): if the new file's short name is in the
; tunnel, create its entries (long name + empty short entry) ourselves and
; let DOS open the existing empty file.
pre_create:
        call tunnel_any
        jc .x
        cmp byte [u_ax+1],6Ch
        jne .h
        test byte [u_dx],0F0h           ; "create if it does not exist"?
        jz .x
        call get_path_dssi
        jmp short .p
.h:     call get_path_dsdx
.p:     jc .x
        test byte [u_cx],18h            ; volume label / directory
        jnz .x
        call dos_sync
        mov si,inpath
        xor al,al
        call resolve
        jc .x
        cmp byte [rp_mode],0
        jne .x
        cmp byte [rp_isroot],0
        jne .x
        cmp byte [rp_found],0
        jne .x
        mov si,lastname
        call parse83
        jc .x
        mov al,[rp_pdrive]
        mov [tk_drive],al
        CLUSCPY tk_clus,rp_parent
        mov word [tk_name],alias11
        call tunnel_find
        jc .x
        mov [t_hit],bx
        ; the long name must still be free in this directory
        DIRSET rp_parent
        lea si,[bx+T_LONG]
        call dir_find
        jnc .drop
        cmp byte [dir_ioerr],0
        jne .x
        mov si,[t_hit]
        add si,T_LONG
        mov al,[u_cx]
        and al,06h                      ; hidden, system
        or al,20h                       ; archive
        call new_lfn_file
        jc .fail
        mov ax,[op_run]
        add ax,[op_n]
        dec ax
        call dir_ptr
        jc .fail
        mov si,[t_hit]
        call t_ctime
        mov byte [si+T_USED],0
        call dos_sync
        ; state for plain_post
        or word [ps_flags],PF_MADE
        mov al,[rp_pdrive]
        xor ah,ah
        mov [ps_drive],ax
        CLUSCPY ps_clus,rp_parent
        mov ax,[op_run]
        mov [ps_a],ax
        mov ax,[op_n]
        mov [ps_b],ax
        mov ax,[u_cx]
        mov [ps_cx],ax
        test al,1
        jz .nro
        or word [ps_flags],PF_RO
.nro:   ; DOS now opens the (existing, empty) file
        mov al,[u_ax+1]
        cmp al,5Bh
        jne .n5b
        mov byte [u_ax+1],3Ch           ; create new -> create/truncate
        ret
.n5b:   cmp al,6Ch
        jne .x
        mov ax,[u_dx]
        mov [ps_dx],ax
        or word [ps_flags],PF_DX
        test al,0Fh
        jnz .x
        or byte [u_dx],01h              ; "fail if it exists" -> "open"
.x:     ret
.drop:  mov bx,[t_hit]                  ; long name taken meanwhile
        mov byte [bx+T_USED],0
        ret
.fail:  call dos_sync
        ret

; t_ctime - creation time of tunnel entry SI -> short entry BX (dirty)
t_ctime:
        push ax
        mov al,[si+T_CMS]
        mov [bx+0Dh],al
        mov ax,[si+T_CTIME]
        mov [bx+0Eh],ax
        mov ax,[si+T_CDATE]
        mov [bx+10h],ax
        call dir_dirty
        pop ax
        ret

; ---------------------------------------------------------------------------
; post_work - after the DOS call; state in ps_*, DOS flags in ps_dosf,
;   DOS results in u_*
; ---------------------------------------------------------------------------
post_work:
        test word [ps_flags],PF_MADE
        jz .nm
        call post_create
.nm:    test word [ps_flags],PF_KILL|PF_SWEEP
        jz .nk
        call post_cleanup
.nk:    test word [ps_flags],PF_DEST
        jz .x
        test byte [ps_dosf],1           ; rename failed?
        jnz .x
        call post_dest
.x:     ret

; the create: undo our entries if DOS failed, else fix the results
post_create:
        call dos_sync
        mov al,[ps_drive]
        call drive_setup
        jc .x
        DIRSET ps_clus
        test byte [ps_dosf],1
        jnz .undo
        test word [ps_flags],PF_DX
        jz .c
        mov word [u_cx],2               ; 6Ch: "file created"
        call get_path_dssi
        jmp short .ro
.c:     call get_path_dsdx
.ro:    jc .x
        test word [ps_flags],PF_RO
        jz .x
        mov cx,[ps_cx]
        and cx,27h
        mov dx,inpath
        mov ax,4301h
        int 21h
        jmp short .sync
.undo:  mov ax,[ps_a]
        mov bx,ax
        add bx,[ps_b]
        dec bx                          ; BX = short entry
        call kill_lfn
        mov ax,bx
        call dir_ptr
        jc .sync
        mov al,[bx]
        or al,al
        jz .sync
        cmp al,0E5h
        je .sync
        cmp word [bx+1Ah],0             ; still our empty entry?
        jne .sync
        cmp word [bx+14h],0
        jne .sync
        mov byte [bx],0E5h
        call dir_dirty
.sync:  call dos_sync
.x:     ret

; delete / rename: remove the long name entries that lost their short entry
post_cleanup:
        call dos_sync
        mov al,[ps_drive]
        call drive_setup
        jc .x
        DIRSET ps_clus
        test word [ps_flags],PF_SWEEP
        jz .one
        call sweep_orphans
        jmp short .done
.one:   mov ax,[ps_a]                   ; do ps_a..ps_b-1 still belong to ps_b?
        call dir_ptr
        jc .done
        cmp byte [bx+0Bh],0Fh
        jne .done
        mov dl,[bx+0Dh]
        mov ax,[ps_b]
        call dir_ptr
        jc .done
        mov al,[bx]
        or al,al
        jz .kill
        cmp al,0E5h
        je .kill
        mov si,bx
        call sfn_checksum
        cmp al,dl
        je .done
.kill:  mov ax,[ps_a]
        mov bx,[ps_b]
        call kill_lfn
.done:  call dos_sync
.x:     ret

; rename succeeded: is the new short name in the tunnel?
post_dest:
        call tunnel_any
        jc .x
        mov ax,[u_es]
        mov dx,[u_di]
        mov di,inpath2
        call fetch_str
        jc .x
        call dos_sync
        mov si,inpath2
        xor al,al
        call resolve
        jc .x
        cmp byte [rp_mode],0
        jne .x
        cmp byte [rp_isroot],0
        jne .x
        cmp byte [rp_found],0
        je .x
        cmp byte [fr_haslfn],0
        jne .x
        mov si,fr_ent
        mov di,o_name11
        mov cx,11
        rep movsb
        mov ax,[fr_idx]
        mov [o_idx],ax
        mov al,[rp_pdrive]
        mov [tk_drive],al
        CLUSCPY tk_clus,rp_parent
        mov word [tk_name],o_name11
        call tunnel_find
        jc .x
        mov [t_hit],bx
        DIRSET rp_parent
        lea si,[bx+T_LONG]
        call dir_find
        jnc .drop
        cmp byte [dir_ioerr],0
        jne .x
        mov ax,[o_idx]
        call dir_ptr
        jc .x
        mov si,[t_hit]
        call t_ctime
        mov byte [si+T_USED],0
        add si,T_LONG
        mov ax,[o_idx]
        call attach_lfn
        call dos_sync
.x:     ret
.drop:  mov bx,[t_hit]
        mov byte [bx+T_USED],0
        ret

; ---------------------------------------------------------------------------
; pat_to_11 - DOS name / pattern SI -> pat11 (11 bytes, '*' expanded to
;   '?', too long parts truncated as DOS does), [pat_wild].  CF if invalid.
; pat_wildness - set [pat_wild] from pat11
; ---------------------------------------------------------------------------
pat_to_11:
        push ax
        push bx
        push cx
        push si
        push di
        mov di,pat11
        mov cx,11
        mov al,' '
        rep stosb
        mov di,pat11
        mov cx,8
        xor bl,bl
        cmp byte [si],'.'
        je .bad
.l:     lodsb
        or al,al
        jz .end
        cmp al,'.'
        je .dot
        cmp al,'*'
        je .star
        jcxz .l
        call upcase
        cmp di,pat11
        jne .st
        cmp al,0E5h
        jne .st
        mov al,05h
.st:    stosb
        dec cx
        jmp short .l
.star:  mov al,'?'
        rep stosb
        jmp short .l
.dot:   or bl,bl
        jnz .bad
        inc bl
        mov di,pat11+8
        mov cx,3
        jmp short .l
.end:   cmp byte [pat11],' '
        je .bad
        call pat_wildness
        clc
        jmp short .x
.bad:   stc
.x:     pop di
        pop si
        pop cx
        pop bx
        pop ax
        ret

pat_wildness:
        push ax
        push cx
        push di
        mov di,pat11
        mov cx,11
        mov al,'?'
        repne scasb
        mov al,0
        jne .s
        inc al
.s:     mov [pat_wild],al
        pop di
        pop cx
        pop ax
        ret

; match11 - does the short name of fr_ent match pat11?  ZF=1 yes
match11:
        push ax
        push cx
        push si
        push di
        mov si,pat11
        mov di,fr_ent
        mov cx,11
.l:     lodsb
        mov ah,[di]
        inc di
        cmp al,'?'
        je .n
        cmp al,ah
        jne .x
.n:     loop .l
        cmp al,al
.x:     pop di
        pop si
        pop cx
        pop ax
        ret

; ---------------------------------------------------------------------------
; scan_matches - before a delete / rename: find the entries of directory
;   [rp_parent] that match pat11 (attributes [sc_skip] excluded), put their
;   long names into the tunnel and note in ps_* how to clean up afterwards.
; ---------------------------------------------------------------------------
scan_matches:
        mov al,[rp_pdrive]
        xor ah,ah
        mov [ps_drive],ax
        CLUSCPY ps_clus,rp_parent
        DIRSET ps_clus
        call dir_rewind
.l:     call dir_next
        jc .x
        mov al,[fr_ent+0Bh]
        test al,[sc_skip]
        jnz .l
        call match11
        jne .l
        cmp byte [fr_haslfn],0
        je .nolfn
        call tunnel_add
        cmp byte [pat_wild],0
        jne .wild
        or word [ps_flags],PF_KILL
        mov ax,[fr_lfnidx]
        mov [ps_a],ax
        mov ax,[fr_idx]
        mov [ps_b],ax
        ret
.wild:  or word [ps_flags],PF_SWEEP
        jmp short .l
.nolfn: cmp byte [pat_wild],0
        jne .l
.x:     ret

; ---------------------------------------------------------------------------
; sweep_orphans - mark the long name entries of the current directory that
;   do not belong to a short entry as deleted
; ---------------------------------------------------------------------------
sweep_orphans:
        push ax
        push bx
        push cx
        push dx
        push si
        xor ax,ax
        mov word [sw_start],0FFFFh
.l:     call dir_ptr
        jc .end
        mov cl,[bx]
        or cl,cl
        jz .end
        cmp cl,0E5h
        je .brk
        mov ch,[bx+0Dh]
        cmp byte [bx+0Bh],0Fh
        jne .sfn
        test cl,40h                     ; first (= last part) of a long name
        jz .cont
        call .flush
        mov [sw_start],ax
        and cl,1Fh
        mov [sw_seq],cl
        mov [sw_chk],ch
        jmp short .n
.cont:  cmp word [sw_start],0FFFFh
        je .lone
        and cl,1Fh
        inc cl
        cmp cl,[sw_seq]
        jne .bad
        cmp ch,[sw_chk]
        jne .bad
        dec byte [sw_seq]
        jmp short .n
.bad:   call .flush
.lone:  mov [sw_start],ax               ; orphan, removed at the next stop
        mov byte [sw_seq],0FFh
        jmp short .n
.sfn:   cmp word [sw_start],0FFFFh
        je .n
        cmp byte [sw_seq],1
        jne .brk
        mov si,bx
        push ax
        call sfn_checksum
        mov dl,al
        pop ax
        cmp dl,[sw_chk]
        jne .brk
        mov word [sw_start],0FFFFh      ; a valid long name
        jmp short .n
.brk:   call .flush
.n:     inc ax
        jmp .l
.end:   call .flush
        call cache_flush_all
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret
; kill the pending entries [sw_start] .. AX-1
.flush: cmp word [sw_start],0FFFFh
        je .f
        push bx
        mov bx,ax
        mov ax,[sw_start]
        call kill_lfn
        mov ax,bx
        pop bx
        mov word [sw_start],0FFFFh
.f:     ret

; ---------------------------------------------------------------------------
; Tunnel table
; ticks       - AX = BIOS tick count (low word)
; tunnel_any  - CF=0 if the tunnel holds a live entry
; tunnel_find - key tk_drive/tk_clus/[tk_name] -> BX = live entry, CF if none
; tunnel_add  - remember the found entry (fr_*) of [rp_pdrive]/[rp_parent]
; ---------------------------------------------------------------------------
ticks:  push es
        xor ax,ax
        mov es,ax
        mov ax,[es:046Ch]
        pop es
        ret

; t_live - SI = used entry, DX = now -> CF if expired (then freed)
t_live: push ax
        mov ax,dx
        sub ax,[si+T_TICK]
        cmp ax,T_TICKS
        jbe .ok
        mov byte [si+T_USED],0
        stc
        jmp short .x
.ok:    clc
.x:     pop ax
        ret

; t_samekey - SI = entry -> ZF=1 if it has the key tk_*
t_samekey:
        push ax
        push cx
        push si
        push di
        mov al,[tk_drive]
        cmp al,[si+T_DRIVE]
        jne .x
        mov ax,[tk_clus]
        cmp ax,[si+T_CLUS]
        jne .x
        mov ax,[tk_clus+2]
        cmp ax,[si+T_CLUS+2]
        jne .x
        add si,T_NAME
        mov di,[tk_name]
        mov cx,11
        repe cmpsb
.x:     pop di
        pop si
        pop cx
        pop ax
        ret

tunnel_any:
        push ax
        push cx
        push dx
        push si
        call ticks
        mov dx,ax
        mov si,[tunnels]
        mov cx,[ntunnels]
        jcxz .no
.l:     cmp byte [si+T_USED],0
        je .n
        call t_live
        jnc .x
.n:     add si,TSIZE
        loop .l
.no:    stc
.x:     pop si
        pop dx
        pop cx
        pop ax
        ret

tunnel_find:
        push ax
        push cx
        push dx
        push si
        call ticks
        mov dx,ax
        mov si,[tunnels]
        mov cx,[ntunnels]
        jcxz .no
.l:     cmp byte [si+T_USED],0
        je .n
        call t_live
        jc .n
        call t_samekey
        je .hit
.n:     add si,TSIZE
        loop .l
.no:    stc
        jmp short .x
.hit:   mov bx,si
        clc
.x:     pop si
        pop dx
        pop cx
        pop ax
        ret

tunnel_add:
        cmp word [ntunnels],0
        je .r
        cmp byte [fr_uflags],0          ; not representable in the code page
        jne .r
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        mov al,[rp_pdrive]
        mov [tk_drive],al
        CLUSCPY tk_clus,rp_parent
        mov word [tk_name],fr_ent
        call ticks
        mov dx,ax
        ; same key, else a free entry, else the oldest one
        mov si,[tunnels]
        mov cx,[ntunnels]
        mov bx,si
        xor bp,bp
.l:     cmp byte [si+T_USED],0
        je .take
        call t_samekey
        je .take
        mov ax,dx
        sub ax,[si+T_TICK]
        cmp ax,bp
        jb .n
        mov bp,ax
        mov bx,si
.n:     add si,TSIZE
        loop .l
        mov si,bx
.take:  mov byte [si+T_USED],1
        mov al,[tk_drive]
        mov [si+T_DRIVE],al
        mov ax,[tk_clus]
        mov [si+T_CLUS],ax
        mov ax,[tk_clus+2]
        mov [si+T_CLUS+2],ax
        mov [si+T_TICK],dx
        mov al,[fr_ent+0Dh]
        mov [si+T_CMS],al
        mov ax,[fr_ent+0Eh]
        mov [si+T_CTIME],ax
        mov ax,[fr_ent+10h]
        mov [si+T_CDATE],ax
        lea di,[si+T_NAME]
        push si
        mov si,fr_ent
        mov cx,11
        rep movsb
        pop si
        lea di,[si+T_LONG]
        mov si,fr_name
        call strcpy
        pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
.r:     ret
