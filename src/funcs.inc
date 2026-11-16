; ===========================================================================
; funcs.inc - directory modification helpers and the INT 21h/71xxh handlers
; Handlers: in  - caller registers in u_ax..u_es
;           out - CF=0 success (outputs stored in u_*), CF=1 AX = error
; ===========================================================================

; ---------------------------------------------------------------------------
; lfn_count - SI = long name -> CX = number of LFN entries needed
; ---------------------------------------------------------------------------
lfn_count:
        push ax
        push dx
        call strlen
        mov ax,cx
        add ax,12
        xor dx,dx
        mov cx,13
        div cx
        mov cx,ax
        pop dx
        pop ax
        ret

; ---------------------------------------------------------------------------
; find_run - find CX consecutive free entries in the current directory,
;   extending a subdirectory when necessary.
;   out: AX = first index, CF on error (AX = DOS error code)
; ---------------------------------------------------------------------------
find_run:
        push bx
        push dx
        push si
        xor ax,ax                       ; index
        xor dx,dx                       ; run length
        xor si,si                       ; run start
        mov byte [run_past],0
.l:     call dir_ptr
        jnc .have
        cmp byte [dir_ioerr],0
        jne .ioe
        call dir_extend
        jc .full
        call dir_ptr
        jc .ioe
.have:  cmp byte [run_past],0
        jne .free
        cmp byte [bx],0
        je .past
        cmp byte [bx],0E5h
        je .free
        xor dx,dx
        jmp short .nx
.past:  mov byte [run_past],1
.free:  or dx,dx
        jnz .inc
        mov si,ax
.inc:   inc dx
        cmp dx,cx
        je .done
.nx:    inc ax
        jmp short .l
.done:  mov ax,si
        clc
        jmp short .x
.ioe:   mov ax,21
        stc
        jmp short .x
.full:  mov ax,5                        ; directory / disk full
        stc
.x:     pop si
        pop dx
        pop bx
        ret

; fix_end - after writing CX entries starting at AX that may have consumed
;   the end-of-directory marker: make sure the entry following them ends
;   the directory again.
fix_end:
        cmp byte [run_past],0
        je .x
        push ax
        push bx
        add ax,cx
        call dir_ptr
        jc .n
        cmp byte [bx],0
        je .n
        mov byte [bx],0
        call dir_dirty
.n:     pop bx
        pop ax
.x:     ret

; ---------------------------------------------------------------------------
; write_lfn - write CX long name entries for name SI starting at entry AX,
;   DL = checksum of the short name.  CF on error.
; ---------------------------------------------------------------------------
write_lfn:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push bp
        mov [wl_idx],ax
        mov [wl_n],cl
        mov [wl_chk],dl
        mov [wl_name],si
        push cx
        call strlen
        mov [wl_len],cx
        pop cx
        mov bp,cx                       ; BP = sequence number k
.ent:   mov ax,[wl_idx]
        call dir_ptr
        jc .err
        call dir_dirty
        mov ax,bp
        cmp al,[wl_n]
        jne .nl
        or al,40h
.nl:    mov [bx],al
        mov byte [bx+0Bh],0Fh
        mov byte [bx+0Ch],0
        mov al,[wl_chk]
        mov [bx+0Dh],al
        mov word [bx+1Ah],0
        ; first char position (k-1)*13
        mov ax,bp
        dec ax
        mov cl,13
        mul cl
        mov dx,ax                       ; DX = pos
        mov si,LFNORD
        mov cx,13
.ch:    cmp dx,[wl_len]
        jb .chr
        je .zero
        mov ax,0FFFFh
        jmp short .st
.zero:  xor ax,ax
        jmp short .st
.chr:   push si
        mov si,[wl_name]
        add si,dx
        mov al,[si]
        pop si
        call oem2uni
.st:    push bx
        mov di,ax
        mov al,[si]
        xor ah,ah
        add bx,ax
        mov [bx],di
        pop bx
        inc si
        inc dx
        loop .ch
        inc word [wl_idx]
        dec bp
        jnz .ent
        clc
        jmp short .x
.err:   stc
.x:     pop bp
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; ---------------------------------------------------------------------------
; kill_lfn - mark the long name entries AX .. BX-1 of the current directory
;   as deleted (only entries that still are LFN entries).
; ---------------------------------------------------------------------------
kill_lfn:
        push ax
        push bx
        push cx
        mov cx,bx
.l:     cmp ax,cx
        jae .x
        call dir_ptr
        jc .x
        cmp byte [bx+0Bh],0Fh
        jne .n
        cmp byte [bx],0E5h
        je .n
        cmp byte [bx],0
        je .n
        mov byte [bx],0E5h
        call dir_dirty
.n:     inc ax
        jmp short .l
.x:     pop cx
        pop bx
        pop ax
        ret

; ---------------------------------------------------------------------------
; attach_lfn - give the short entry AX of the current directory the long
;   name SI.  The entry is moved when there is no room in front of it.
;   CF on error (AX = error code).
; ---------------------------------------------------------------------------
attach_lfn:
        push bx
        push cx
        push dx
        push si
        push di
        mov [al_idx],ax
        call dir_ptr
        jc .ioe
        ; keep a copy of the short entry
        push si
        mov si,bx
        mov di,tmpent
        mov cx,16
        rep movsw
        pop si
        push si
        mov si,tmpent
        call sfn_checksum
        pop si
        mov dl,al                       ; DL = checksum
        call lfn_count                  ; CX = entries
        ; enough free (deleted) entries right in front of it?
        mov ax,[al_idx]
        cmp ax,cx
        jb .move
        sub ax,cx
        mov bx,ax
        push cx
.chk:   push bx
        call dir_ptr
        jc .chkf
        cmp byte [bx],0E5h
        pop bx
        jne .chkn
        inc ax
        loop .chk
        pop cx
        mov ax,bx
        mov byte [run_past],0
        call write_lfn
        jc .ioe
        jmp short .ok
.chkf:  pop bx
.chkn:  pop cx
.move:  inc cx
        call find_run                   ; AX = start of CX free entries
        jc .x
        dec cx
        call write_lfn
        jc .ioe
        push ax
        add ax,cx
        call dir_ptr
        jc .ioe2
        push si
        mov di,bx
        mov si,tmpent
        mov cx,16
        rep movsw
        pop si
        call dir_dirty
        pop ax
        push ax
        call lfn_count
        inc cx
        call fix_end
        ; delete the old short entry
        mov ax,[al_idx]
        call dir_ptr
        jc .ioe2
        mov byte [bx],0E5h
        call dir_dirty
        pop ax
.ok:    call cache_flush_all
        jc .ioe
        clc
        jmp short .x
.ioe2:  pop ax
.ioe:   mov ax,21
        stc
.x:     pop di
        pop si
        pop dx
        pop cx
        pop bx
        ret

; ---------------------------------------------------------------------------
; Common handler helpers
; ---------------------------------------------------------------------------

; get_path_dsdx / get_path_dssi - fetch the caller's path into inpath
get_path_dsdx:
        mov dx,[u_dx]
        jmp short get_path_ds
get_path_dssi:
        mov dx,[u_si]
get_path_ds:
        mov ax,[u_ds]
        mov di,inpath
        call fetch_str
        jnc .ok
        mov ax,3
.ok:    ret

; dos_path_call - DOS function AH with DS:DX = outlog; result as from DOS
dos_path_call:
        mov dx,outlog
        int 21h
        ret

; save_found - remember the found object (for later updates)
save_found:
        push ax
        CLUSCPY o_parent,rp_parent
        mov ax,[fr_idx]
        mov [o_idx],ax
        mov ax,[fr_lfnidx]
        mov [o_lfnidx],ax
        mov al,[fr_haslfn]
        mov [o_haslfn],al
        pop ax
        ret

; drop_old_lfn - delete the long name entries of the saved object
drop_old_lfn:
        cmp byte [o_haslfn],0
        je .x
        push ax
        push bx
        call dos_sync
        DIRSET o_parent
        mov ax,[o_lfnidx]
        mov bx,[o_idx]
        call kill_lfn
        call cache_flush_all
        pop bx
        pop ax
.x:     ret

; ---------------------------------------------------------------------------
; 710Dh - reset drive
; ---------------------------------------------------------------------------
f_reset:
        mov byte [dv_ok],0
        mov ah,0Dh
        int 21h
        clc
        ret

; ---------------------------------------------------------------------------
; 7139h - make directory (DS:DX)
; ---------------------------------------------------------------------------
f_mkdir:
        call get_path_dsdx
        jc .ret
        mov si,inpath
        xor al,al
        call resolve
        jc .ret
        cmp byte [rp_mode],0
        je .loc
        mov ah,39h
        jmp dos_path_call
.loc:   cmp byte [rp_found],0
        jne .exists
        mov si,lastname
        call check_lfn
        jc .badname
        DIRSET rp_parent
        call make_alias
        jc .noalias
        mov si,alias11
        call set_last_short
        call dos_sync
        mov ah,39h
        call dos_path_call
        jc .ret
        cmp byte [needlfn],0
        je .done
        call dos_sync
        DIRSET rp_parent
        mov si,alias11
        call sfn_exists
        jnc .gone
        mov ax,[sfn_idx]
        mov si,lastname
        call attach_lfn
        jnc .done
        ; no room for the long name: undo
        push ax
        call dos_sync
        mov ah,3Ah
        call dos_path_call
        pop ax
        stc
        jmp short .ret
.done:  call dos_sync
        clc
.ret:   ret
.gone:  call dos_sync
        clc
        ret
.exists:
        mov ax,5
        stc
        ret
.badname:
        mov ax,3
        stc
        ret
.noalias:
        mov ax,5
        stc
        ret

; ---------------------------------------------------------------------------
; 713Ah - remove directory (DS:DX), 7141h - delete file (DS:DX)
; ---------------------------------------------------------------------------
f_rmdir:
        mov byte [del_fn],3Ah
        jmp short del_common
f_delete:
        mov byte [del_fn],41h
        cmp word [u_si],0
        je del_common
        jmp f_delwild
del_common:
        call get_path_dsdx
        jc .ret
        mov si,inpath
        xor al,al
        call resolve
        jc .ret
        mov ah,[del_fn]
        cmp byte [rp_mode],0
        je .loc
        jmp dos_path_call
.loc:   cmp byte [rp_isroot],0
        jne .acc
        cmp byte [rp_found],0
        je .nf
        call save_found
        call logical_short
        call dos_sync
        mov ah,[del_fn]
        call dos_path_call
        jc .ret
        call drop_old_lfn
        call dos_sync
        clc
.ret:   ret
.acc:   mov ax,5
        stc
        ret
.nf:    mov ax,2
        cmp byte [del_fn],41h
        je .nf2
        mov ax,3
.nf2:   stc
        ret

; 7141h with SI=1: wildcards and attribute masks (CL allowed, CH required)
f_delwild:
        call get_path_dsdx
        jc .ret
        mov si,inpath
        mov al,RF_PATTERN
        call resolve
        jc .ret
        cmp byte [rp_mode],0
        je .loc
        jmp delwild_dos
.loc:   mov word [dw_count],0
        mov word [dw_err],2
        DIRSET rp_parent
        call dir_rewind
.l:     call dir_next
        jc .end
        mov al,[fr_ent+0Bh]
        test al,18h
        jnz .l
        mov ah,[u_cx]                   ; allowed
        not ah
        and ah,al
        test ah,06h
        jnz .l
        mov ah,[u_cx+1]                 ; required
        and al,ah
        cmp al,ah
        jne .l
        mov si,lastname
        call match_entry
        jne .l
        ; delete it
        mov ax,[it_idx]
        mov [dw_it],ax
        call save_found
        mov si,fr_sfn
        call set_last_str
        call dos_sync
        mov ah,41h
        call dos_path_call
        jnc .okd
        mov [dw_err],ax
        jmp short .cont
.okd:   inc word [dw_count]
        call drop_old_lfn
.cont:  call dos_sync
        DIRSET rp_parent
        mov ax,[dw_it]
        mov [it_idx],ax
        jmp short .l
.end:   cmp byte [dir_ioerr],0
        jne .ioe
        cmp word [dw_count],0
        je .none
        clc
.ret:   ret
.none:  mov ax,[dw_err]
        stc
        ret
.ioe:   call dos_sync
        mov ax,21
        stc
        ret

;---------------------------------------------------------------------------
; delwild_dos - wildcard delete on a pass-through drive: search with DOS
;   (4Eh/4Fh, attributes CL allowed / CH required) and delete every match.
;   outlog = path as given by the caller.
; ---------------------------------------------------------------------------
delwild_dos:
        ; directory part of the path (up to and including the last '\' / ':')
        mov si,outlog
        mov di,tmpbuf
        mov bx,di                       ; end of directory part
.cp:    lodsb
        stosb
        cmp al,'\'
        je .sep
        cmp al,'/'
        je .sep
        cmp al,':'
        jne .nsep
.sep:   mov bx,di
.nsep:  or al,al
        jnz .cp
        mov [dw_dirend],bx
        mov word [dw_count],0
        mov word [dw_err],2
        mov ah,2Fh                      ; save caller's DTA
        int 21h
        mov [sv_dta],bx
        mov [sv_dta+2],es
        push ds
        pop es
        mov dx,dw_dta
        mov ah,1Ah
        int 21h
        mov dx,outlog
        mov cl,[u_cx]
        and cl,06h                      ; hidden/system only, no dirs
        xor ch,ch
        mov ah,4Eh
.find:  int 21h
        jc .done
        mov al,[dw_dta+15h]
        mov ah,[u_cx+1]
        and al,ah
        cmp al,ah
        jne .next
        test byte [dw_dta+15h],18h
        jnz .next
        mov si,dw_dta+1Eh
        mov di,[dw_dirend]
        call strcpy
        mov dx,tmpbuf
        mov ah,41h
        int 21h
        jnc .okd
        mov [dw_err],ax
        jmp short .next
.okd:   inc word [dw_count]
.next:  mov ah,4Fh
        jmp short .find
.done:  push ds
        mov dx,[sv_dta]
        mov ds,[sv_dta+2]
        mov ah,1Ah
        int 21h
        pop ds
        cmp word [dw_count],0
        je .none
        clc
        ret
.none:  mov ax,[dw_err]
        stc
        ret

; ---------------------------------------------------------------------------
; 713Bh - change directory (DS:DX)
; ---------------------------------------------------------------------------
f_chdir:
        call get_path_dsdx
        jc .ret
        mov si,inpath
        xor al,al
        call resolve
        jc .ret
        mov ah,3Bh
        cmp byte [rp_mode],0
        je .loc
        jmp dos_path_call
.loc:   cmp byte [rp_isroot],0
        jne .go
        cmp byte [rp_found],0
        je .nf
        test byte [fr_ent+0Bh],10h
        jz .nf
.go:    call logical_short
        mov ah,3Bh
        jmp dos_path_call
.nf:    mov ax,3
        stc
.ret:   ret

; ---------------------------------------------------------------------------
; 7143h - get/set attributes and time stamps (DS:DX, BL = subfunction)
; ---------------------------------------------------------------------------
f_attr:
        call get_path_dsdx
        jc .ret
        mov si,inpath
        xor al,al
        call resolve
        jc .ret
        mov bl,[u_bx]
        cmp byte [rp_mode],0
        je .loc
        cmp bl,1
        ja .inv
        mov al,bl
        mov ah,43h
        mov cx,[u_cx]
        call dos_path_call
        jc .ret
        mov [u_cx],cx
        ret
.loc:   cmp bl,1
        ja .ext
        cmp byte [rp_isroot],0
        jne .acc
        cmp byte [rp_found],0
        je .nf
        call logical_short
        mov al,bl
        mov ah,43h
        mov cx,[u_cx]
        call dos_sync
        call dos_path_call
        jc .ret
        mov [u_cx],cx
        call dos_sync
        clc
.ret:   ret
.ext:   cmp bl,8
        ja .inv
        cmp byte [rp_isroot],0
        jne .acc
        cmp byte [rp_found],0
        je .nf
        DIRSET rp_parent
        mov [at_fn],bl
        mov ax,[fr_idx]
        call dir_ptr
        jc .ioe
        mov cl,[at_fn]
        mov ax,[u_cx]
        mov dx,[u_di]
        cmp cl,2
        je .size
        cmp cl,3
        je .setw
        cmp cl,4
        je .getw
        cmp cl,5
        je .seta
        cmp cl,6
        je .geta
        cmp cl,7
        je .setc
        ; 8: get creation
        mov ax,[bx+10h]
        mov [u_di],ax
        mov ax,[bx+0Eh]
        mov [u_cx],ax
        mov al,[bx+0Dh]
        xor ah,ah
        mov [u_si],ax
        clc
        ret
.size:  mov ax,[bx+1Ch]
        mov [u_ax],ax
        mov ax,[bx+1Eh]
        mov [u_dx],ax
        clc
        ret
.setw:  mov [bx+16h],ax
        mov [bx+18h],dx
        jmp short .wr
.getw:  mov ax,[bx+16h]
        mov [u_cx],ax
        mov ax,[bx+18h]
        mov [u_di],ax
        clc
        ret
.seta:  mov [bx+12h],dx
        jmp short .wr
.geta:  mov ax,[bx+12h]
        mov [u_di],ax
        mov word [u_cx],0
        clc
        ret
.setc:  mov [bx+10h],dx
        mov [bx+0Eh],ax
        mov al,[u_si]
        mov [bx+0Dh],al
.wr:    call dir_dirty
        call dos_sync
        clc
        ret
.ioe:   mov ax,21
        stc
        ret
.inv:   mov ax,1
        stc
        ret
.acc:   mov ax,5
        stc
        ret
.nf:    mov ax,2
        stc
        ret

; ---------------------------------------------------------------------------
; 7147h - get current directory (DL = drive, DS:SI = 261 byte buffer)
; ---------------------------------------------------------------------------
f_getcwd:
        mov al,[u_dx]
        or al,al
        jnz .drv
        mov ah,19h
        int 21h
        inc al
.drv:   cmp al,26
        ja .bad
        add al,'A'-1
        mov [inpath],al
        mov word [inpath+1],':.'
        mov byte [inpath+3],0
        mov si,inpath
        xor al,al
        call resolve
        jc .dos
        cmp byte [rp_mode],0
        jne .dos
        mov si,[rp_lrest]
        cmp byte [si],'\'
        jne .cp
        inc si
.cp:    mov ax,[u_ds]
        mov di,[u_si]
        mov cx,MAXPATH+1
        call put_str_seg
        mov ax,3
        jc .ret
        clc
.ret:   ret
.dos:   ; let DOS answer (short names)
        push ds
        mov dl,[u_dx]
        mov si,[u_si]
        mov ds,[u_ds]
        mov ah,47h
        int 21h
        pop ds
        ret
.bad:   mov ax,15
        stc
        ret

; put_str_seg - copy NUL terminated SI to AX:DI, max CX bytes incl. NUL
put_str_seg:
        push word [u_es]
        mov [u_es],ax
        call put_str
        pop word [u_es]
        ret

; ---------------------------------------------------------------------------
; Find handles
; ---------------------------------------------------------------------------
; h_alloc - BX = free (or least recently used) handle structure.
;   Every allocation bumps the structure's generation, which is part of the
;   handle number: a search whose handle was taken over gets "invalid
;   handle" instead of the entries of another search.
h_alloc:
        push ax
        push cx
        push dx
        push si
        mov si,[handles]
        mov bx,si
        mov dx,0FFFFh
        mov cx,[nhandles]
.l:     cmp byte [si+H_USED],0
        je .take
        mov ax,[si+H_LRU]
        cmp ax,dx
        jae .n
        mov dx,ax
        mov bx,si
.n:     add si,HSIZE
        loop .l
        jmp short .got
.take:  mov bx,si
.got:   mov byte [bx+H_USED],1
        inc byte [bx+H_GEN]
        jnz .g
        inc byte [bx+H_GEN]             ; generation 0 is never used
.g:     call h_touch
        pop si
        pop dx
        pop cx
        pop ax
        ret

h_touch:
        push ax
        inc word [lru_ctr]
        mov ax,[lru_ctr]
        mov [bx+H_LRU],ax
        pop ax
        ret

; h_get - [u_bx] = handle number -> BX = structure, CF if invalid
h_get:
        push ax
        push cx
        push dx
        mov ax,[u_bx]
        mov cl,ah                       ; generation
        xor ah,ah
        sub ax,HBASE
        jb .bad
        cmp ax,[nhandles]
        jae .bad
        mov dx,HSIZE
        mul dx
        add ax,[handles]
        mov bx,ax
        cmp byte [bx+H_USED],0
        je .bad
        cmp cl,[bx+H_GEN]
        jne .bad
        clc
        jmp short .x
.bad:   stc
.x:     pop dx
        pop cx
        pop ax
        ret

; h_number - BX = structure -> AX = handle number (generation << 8 | index)
h_number:
        push dx
        push cx
        mov ax,bx
        sub ax,[handles]
        xor dx,dx
        mov cx,HSIZE
        div cx
        add ax,HBASE
        mov ah,[bx+H_GEN]
        pop cx
        pop dx
        ret

; ---------------------------------------------------------------------------
; fill_find - write the WIN32_FIND_DATA record for the current entry
;   (fr_*) to the caller's ES:DI.  [u_si] = 1 -> DOS date/time format.
; ---------------------------------------------------------------------------
fill_find:
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push es
        mov es,[u_es]
        mov di,[u_di]
        ; attributes
        mov al,[fr_ent+0Bh]
        xor ah,ah
        stosw
        xor ax,ax
        stosw
        ; creation
        mov dx,[fr_ent+10h]
        mov cx,[fr_ent+0Eh]
        mov bl,[fr_ent+0Dh]
        call put_time
        ; last access (date only)
        mov dx,[fr_ent+12h]
        xor cx,cx
        xor bl,bl
        call put_time
        ; last write
        mov dx,[fr_ent+18h]
        mov cx,[fr_ent+16h]
        xor bl,bl
        call put_time
        ; size high, size low, reserved
        xor ax,ax
        stosw
        stosw
        mov ax,[fr_ent+1Ch]
        stosw
        mov ax,[fr_ent+1Eh]
        stosw
        xor ax,ax
        mov cx,4
        rep stosw
        ; names
        push di
        mov si,fr_name
        call strcpy
        pop di
        add di,260
        mov byte [es:di],0
        cmp byte [fr_haslfn],0
        je .ns
        mov si,fr_sfn
        call strcpy
.ns:    pop es
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        ret

; put_time - store DX=date CX=time BL=10ms at ES:DI (8 bytes) in the
;   format selected by [u_si]; date 0 -> all zero
put_time:
        or dx,dx
        jz .zero
        cmp word [u_si],1
        je .dos
        call dos2ft
        jmp acc_store
.dos:   mov ax,cx
        stosw
        mov ax,dx
        stosw
        xor ax,ax
        stosw
        stosw
        ret
.zero:  xor ax,ax
        stosw
        stosw
        stosw
        stosw
        ret

; ---------------------------------------------------------------------------
; find_local - continue the search of handle BX on a FAT drive
;   CF -> AX = error
; ---------------------------------------------------------------------------
find_local:
        mov al,[bx+H_DRIVE]
        call drive_setup
        jc .nomore
        mov ax,[bx+H_CLUS]
        mov dx,[bx+H_CLUS+2]
        call dir_set
        mov ax,[bx+H_IDX]
        mov [it_idx],ax
.l:     call dir_next
        jc .end
        mov al,[fr_ent+0Bh]
        test al,08h
        jz .nolab
        test byte [bx+H_ALLOW],08h
        jz .l
.nolab: mov ah,[bx+H_ALLOW]
        not ah
        and ah,al
        test ah,16h
        jnz .l
        mov ah,[bx+H_REQ]
        and al,ah
        cmp al,ah
        jne .l
        lea si,[bx+H_PAT]
        call match_entry
        jne .l
        mov ax,[it_idx]
        mov [bx+H_IDX],ax
        call fill_find
        mov al,[fr_uflags]
        xor ah,ah
        mov [u_cx],ax
        clc
        ret
.end:   mov ax,[it_idx]
        mov [bx+H_IDX],ax
        cmp byte [dir_ioerr],0
        jne .ioe
.nomore:
        mov ax,18
        stc
        ret
.ioe:   mov ax,21
        stc
        ret

; ---------------------------------------------------------------------------
; find_dos - first (AH=4Eh) / next (AH=4Fh) search through DOS for handle
;   BX (non-FAT drives); the handle's pattern area holds the DTA.
; ---------------------------------------------------------------------------
find_dos:
        push ax
        mov ah,2Fh                      ; save caller's DTA
        push bx
        int 21h
        mov [sv_dta],bx
        mov [sv_dta+2],es
        pop bx
        push ds
        pop es
        lea dx,[bx+H_PAT]
        mov ah,1Ah
        int 21h
        pop ax
        mov dx,outlog
        mov cl,[bx+H_ALLOW]
        xor ch,ch
.again: int 21h
        jc .done
        mov al,[bx+H_PAT+15h]
        and al,[bx+H_REQ]
        cmp al,[bx+H_REQ]
        je .ok
        mov ah,4Fh
        jmp short .again
.ok:    ; build fr_* from the DTA
        lea si,[bx+H_PAT+1Eh]
        mov di,fr_name
        call strcpy
        mov byte [fr_haslfn],0
        mov byte [fr_uflags],0
        mov di,fr_ent
        mov cx,16
        xor ax,ax
        rep stosw
        mov al,[bx+H_PAT+15h]
        mov [fr_ent+0Bh],al
        mov ax,[bx+H_PAT+16h]
        mov [fr_ent+16h],ax
        mov [fr_ent+0Eh],ax
        mov ax,[bx+H_PAT+18h]
        mov [fr_ent+18h],ax
        mov [fr_ent+10h],ax
        mov [fr_ent+12h],ax
        mov ax,[bx+H_PAT+1Ah]
        mov [fr_ent+1Ch],ax
        mov ax,[bx+H_PAT+1Ch]
        mov [fr_ent+1Eh],ax
        call fill_find
        mov word [u_cx],0
        clc
.done:  pushf
        push ax
        push ds
        mov dx,[sv_dta]
        mov ds,[sv_dta+2]
        mov ah,1Ah
        int 21h
        pop ds
        pop ax
        popf
        ret

; ---------------------------------------------------------------------------
; 714Eh - find first (CL allowed, CH required attributes, DS:DX filespec,
;         ES:DI find data, SI date format) -> AX handle, CX unicode flags
; ---------------------------------------------------------------------------
f_findfirst:
        call get_path_dsdx
        jc .ret
        mov si,inpath
        mov al,RF_PATTERN
        call resolve
        jc .ret
        call h_alloc
        mov ax,[u_cx]
        mov [bx+H_ALLOW],al
        mov [bx+H_REQ],ah
        mov al,[rp_mode]
        mov [bx+H_MODE],al
        or al,al
        jz .loc
        mov ah,4Eh
        call find_dos
        jmp short .res
.loc:   mov al,[rp_pdrive]
        mov [bx+H_DRIVE],al
        CLUSCPY bx+H_CLUS,rp_parent
        mov word [bx+H_IDX],0
        mov si,lastname
        lea di,[bx+H_PAT]
        call strcpy
        call find_local
.res:   jc .fail
        call h_number
        mov [u_ax],ax
        clc
.ret:   ret
.fail:  mov byte [bx+H_USED],0
        cmp ax,18
        jne .f2
        mov ax,2                        ; nothing found at all
        cmp byte [rp_mode],0
        je .f2
        mov ax,18
.f2:    stc
        ret

; 714Fh - find next (BX handle, ES:DI find data, SI date format)
f_findnext:
        call h_get
        jc .badh
        call h_touch
        cmp byte [bx+H_MODE],0
        je .loc
        mov ah,4Fh
        jmp find_dos
.loc:   jmp find_local
.badh:  mov ax,6
        stc
        ret

; 71A1h - find close (BX handle)
f_findclose:
        call h_get
        jc .badh
        mov byte [bx+H_USED],0
        clc
        ret
.badh:  mov ax,6
        stc
        ret

; ---------------------------------------------------------------------------
; 7156h - rename (DS:DX old name, ES:DI new name)
; ---------------------------------------------------------------------------
f_rename:
        call get_path_dsdx
        jc .ret
        mov ax,[u_es]
        mov dx,[u_di]
        mov di,inpath2
        call fetch_str
        jnc .f2
        mov ax,3
.ret:   ret
.f2:    mov si,inpath
        xor al,al
        call resolve
        jc .ret
        cmp byte [rp_mode],0
        je .loc
        ; pass through: rename the names as given
        mov dx,inpath
        mov di,inpath2
        mov ah,56h
        int 21h
        ret
.loc:   cmp byte [rp_isroot],0
        jne .acc
        cmp byte [rp_found],0
        je .nf
        call save_found
        mov al,[rp_pdrive]
        mov [o_drive],al
        call logical_short
        mov si,outlog
        mov di,oldlog
        call strcpy
        ; copy the old short name for the same-entry test
        mov si,fr_ent
        mov di,o_name11
        mov cx,11
        rep movsb
        ; resolve the new name
        mov si,inpath2
        xor al,al
        call resolve
        jc .ret
        cmp byte [rp_mode],0
        jne .xdev
        mov al,[rp_pdrive]
        cmp al,[o_drive]
        jne .xdev
        cmp byte [rp_isroot],0
        jne .acc
        mov si,lastname
        call check_lfn
        jc .badname
        cmp byte [rp_found],0
        je .new
        ; target exists: only allowed if it is the same entry (case change)
        mov ax,[rp_parent]
        cmp ax,[o_parent]
        jne .acc
        mov ax,[rp_parent+2]
        cmp ax,[o_parent+2]
        jne .acc
        mov ax,[fr_idx]
        cmp ax,[o_idx]
        jne .acc
        ; new long name for the same short entry
        mov si,lastname
        call parse83
        mov byte [needlfn],1
        jc .cc
        push si
        push di
        mov si,alias11
        mov di,o_name11
        mov cx,11
        repe cmpsb
        pop di
        pop si
        jne .cc
        mov [needlfn],al                ; plain 8.3: LFN only for lower case
.cc:    call drop_old_lfn
        cmp byte [needlfn],0
        je .ok
        DIRSET o_parent
        mov ax,[o_idx]
        mov si,lastname
        call attach_lfn
        jc .r2
.ok:    call dos_sync
        clc
.r2:    ret
.new:   CLUSCPY n_parent,rp_parent
        DIRSET n_parent
        mov si,lastname
        call make_alias
        jc .acc
        mov si,alias11
        call set_last_short
        call dos_sync
        mov dx,oldlog
        mov di,outlog
        mov ah,56h
        int 21h
        jc .r2
        call drop_old_lfn
        cmp byte [needlfn],0
        je .ok
        call dos_sync
        DIRSET n_parent
        mov si,alias11
        call sfn_exists
        jnc .ok
        mov ax,[sfn_idx]
        mov si,lastname
        call attach_lfn
        jc .r2
        jmp short .ok
.acc:   mov ax,5
        stc
        ret
.nf:    mov ax,2
        stc
        ret
.xdev:  mov ax,17
        stc
        ret
.badname:
        mov ax,3
        stc
        ret

; ---------------------------------------------------------------------------
; 7160h - truename (CL=0 full, 1 short, 2 long path; CH bit 7: keep SUBST)
;         DS:SI input, ES:DI 261 byte output buffer
; ---------------------------------------------------------------------------
f_truename:
        call get_path_dssi
        jc .ret
        mov si,inpath
        xor al,al
        call resolve
        jc .ret
        mov cl,[u_cx]
        cmp cl,2
        ja .inv
        cmp byte [rp_mode],0
        je .loc
        mov si,outlog
        jmp short .dos60
.loc:   cmp byte [rp_found],0
        jne .exist
        or cl,cl
        jnz .nf
        ; CL=0 for a name that does not exist (yet): parent + last name
        mov bx,[rp_slast]
        dec bx
        mov al,[bx]
        mov byte [bx],0
        push ax
        push bx
        call logical_short
        pop bx
        pop ax
        mov [bx],al
        mov si,outlog
        mov di,tmpbuf
        mov ah,60h
        int 21h
        jc .ret
        mov si,tmpbuf
        call strlen
        mov di,si
        add di,cx
        cmp byte [di-1],'\'
        je .app
        mov al,'\'
        stosb
.app:   mov si,lastname
        call strcpy
        mov si,tmpbuf
        jmp short .out
.exist: cmp cl,2
        je .long
        test byte [u_cx+1],80h
        jz .short
        call logical_short
        mov si,outlog
        jmp short .out
.short: call logical_short
        mov si,outlog
.dos60: mov di,tmpbuf
        mov ah,60h
        int 21h
        jc .ret
        mov si,tmpbuf
        jmp short .out
.long:  test byte [u_cx+1],80h
        jz .lphys
        mov si,[rp_lrest]
        call mk_logical
        mov si,outlog
        jmp short .out
.lphys: mov si,plong
        cmp byte [plong+2],0
        jne .out
        mov word [plong+2],'\'
.out:   mov ax,[u_es]
        mov di,[u_di]
        mov cx,MAXPATH+1
        call put_str_seg
        jc .big
        clc
.ret:   ret
.big:   mov ax,3
        stc
        ret
.nf:    mov ax,2
        stc
        ret
.inv:   mov ax,1
        stc
        ret

; ---------------------------------------------------------------------------
; ext_open - DOS extended open/create of outlog
;   BX mode, CX attributes, DX action, [eo_exists] -> AX handle, CX action
; ---------------------------------------------------------------------------
ext_open:
        cmp byte [dosmajor],4
        jb .emul
        mov si,outlog
        mov ax,6C00h
        int 21h
        ret
.emul:  cmp byte [eo_exists],0
        je .create
        and dx,3
        cmp dx,1
        jne .trunc
        mov al,bl
        mov ah,3Dh
        call dos_path_call
        mov cx,1
        ret
.trunc: mov ah,3Ch
        call dos_path_call
        mov cx,3
        ret
.create:
        mov ah,5Bh
        call dos_path_call
        mov cx,2
        ret

; ---------------------------------------------------------------------------
; 716Ch / 71A9h - extended open/create (BX mode, CX attr, DX action,
;                 DS:SI name) -> AX handle, CX action taken
; ---------------------------------------------------------------------------
f_open:
        call get_path_dssi
        jc .ret
        mov si,inpath
        xor al,al
        call resolve
        jc .ret
        mov bx,[u_bx]
        mov cx,[u_cx]
        mov dx,[u_dx]
        cmp byte [rp_mode],0
        je .loc
        mov byte [eo_exists],1          ; best guess for DOS 3
        mov ax,4300h
        call dos_path_call
        jnc .pe
        mov byte [eo_exists],0
.pe:    mov bx,[u_bx]
        mov cx,[u_cx]
        mov dx,[u_dx]
        call ext_open
        jmp .result
.loc:   cmp byte [rp_isroot],0
        jne .acc
        cmp byte [rp_found],0
        je .new
        test byte [fr_ent+0Bh],18h
        jnz .acc
        test dl,3
        jz .exists
        mov byte [eo_exists],1
        call logical_short
        call dos_sync
        mov bx,[u_bx]
        mov cx,[u_cx]
        mov dx,[u_dx]
        call ext_open
        jmp .result
.new:   test dl,10h
        jz .nf
        mov si,lastname
        call check_lfn
        jc .badname
        DIRSET rp_parent
        call make_alias
        jc .acc
        mov si,alias11
        call set_last_short
        mov byte [eo_exists],0
        cmp byte [needlfn],0
        jne .lfn
        call dos_sync
        mov bx,[u_bx]
        mov cx,[u_cx]
        mov dx,10h
        call ext_open
        jmp .result
.lfn:   ; create the entries ourselves, then let DOS open the empty file
        mov si,lastname
        mov al,[u_cx]
        and al,06h                      ; hidden, system
        or al,20h                       ; archive
        call new_lfn_file
        jc .nlf
        call dos_sync
        ; open it
        mov bx,[u_bx]
        mov cx,[u_cx]
        and cx,0FEh                     ; read-only is set afterwards
        mov dx,1
        mov byte [eo_exists],1
        call ext_open
        jc .undo
        mov [u_ax],ax
        test byte [u_cx],1
        jz .created
        mov cx,[u_cx]
        and cx,27h
        mov ax,4301h
        call dos_path_call
.created:
        mov word [u_cx],2
        call dos_sync
        clc
        ret
.undo:  push ax
        DIRSET rp_parent
        mov ax,[op_run]
        mov bx,ax
        add bx,[op_n]
        call kill_lfn
        dec bx
        mov ax,bx
        call dir_ptr
        jc .u2
        mov byte [bx],0E5h
        call dir_dirty
.u2:    call dos_sync
        pop ax
        stc
        ret
.result:
        jc .ret
        mov [u_ax],ax
        mov [u_cx],cx
        call dos_sync
        clc
.ret:   ret
.nlf:   cmp ax,21
        je .ioe
        ret
.ioe:   call dos_sync
        mov ax,21
        stc
        ret
.acc:   mov ax,5
        stc
        ret
.exists:
        mov ax,80
        stc
        ret
.nf:    mov ax,2
        stc
        ret
.badname:
        mov ax,3
        stc
        ret

; ---------------------------------------------------------------------------
; new_lfn_file - write the entries of a new, empty file: long name SI,
;   short name alias11, attributes AL, in directory [rp_parent].
;   out: [op_run] = first entry, [op_n] = number of entries (short entry
;        last); CF -> AX = 5 (directory full) or 21 (I/O error)
; ---------------------------------------------------------------------------
new_lfn_file:
        push bx
        push cx
        push dx
        push si
        push di
        mov [nf_attr],al
        call lfn_count
        inc cx
        mov [op_n],cx
        DIRSET rp_parent
        call find_run
        jc .x
        mov [op_run],ax
        dec cx
        push ax
        push si
        mov si,alias11
        call sfn_checksum
        mov dl,al
        pop si
        pop ax
        call write_lfn
        jc .ioe
        add ax,cx
        call dir_ptr
        jc .ioe
        call dir_dirty
        ; build the short entry
        mov di,bx
        mov si,alias11
        mov cx,11
        rep movsb
        mov al,[nf_attr]
        stosb                           ; 0Bh
        xor al,al
        stosb                           ; 0Ch
        push bx
        call now_dos                    ; DX date, CX time, BL 10ms
        mov al,bl
        pop bx
        stosb                           ; 0Dh creation 10ms
        mov ax,cx
        stosw                           ; 0Eh creation time
        mov ax,dx
        stosw                           ; 10h creation date
        stosw                           ; 12h access date
        xor ax,ax
        stosw                           ; 14h
        mov ax,cx
        stosw                           ; 16h write time
        mov ax,dx
        stosw                           ; 18h write date
        xor ax,ax
        stosw                           ; 1Ah cluster
        stosw                           ; 1Ch size
        stosw
        mov ax,[op_run]
        mov cx,[op_n]
        call fix_end
        clc
        jmp short .x
.ioe:   mov ax,21
        stc
.x:     pop di
        pop si
        pop dx
        pop cx
        pop bx
        ret

; now_dos - current date/time in DOS format: DX date, CX time, BL 10ms
now_dos:
        push ax
        mov ah,2Ah
        int 21h                         ; CX year, DH month, DL day
        sub cx,1980
        jnb .y
        xor cx,cx
.y:     mov ax,cx
        mov cl,9
        shl ax,cl
        push ax
        mov al,dh
        xor ah,ah
        mov cl,5
        shl ax,cl
        xor dh,dh
        or ax,dx
        pop dx
        or dx,ax
        push dx
        mov ah,2Ch
        int 21h                         ; CH h, CL m, DH s, DL 1/100
        mov al,ch
        xor ah,ah
        push cx
        mov cl,11
        shl ax,cl
        pop cx
        mov bx,ax
        mov al,cl
        xor ah,ah
        mov cl,5
        shl ax,cl
        or bx,ax
        mov al,dh
        shr al,1
        pushf
        xor ah,ah
        or bx,ax
        mov cx,bx
        mov bl,dl
        popf
        jnc .ev
        add bl,100
.ev:    pop dx
        pop ax
        ret

; ---------------------------------------------------------------------------
; 71A0h - volume information (DS:DX root name, ES:DI fs name buffer, CX size)
; ---------------------------------------------------------------------------
f_volinfo:
        mov ax,[u_cx]
        mov [vi_size],ax
        call get_path_dsdx
        jc .ret
        mov si,inpath
        xor al,al
        call resolve
        jc .ret
        cmp byte [rp_mode],0
        jne .nolfn
        mov word [u_bx],4006h           ; LFN API, case preserved, Unicode
        mov word [u_cx],255
        mov word [u_dx],260
        jmp short .name
.nolfn: mov word [u_bx],0
        mov word [u_cx],12
        mov word [u_dx],80
.name:  mov cx,[vi_size]
        mov si,fatname
        cmp byte [rp_mode],0
        jne .nm
        cmp byte [dv_fat32],0
        je .nm
        mov si,fat32name
.nm:        mov ax,[u_es]
        mov di,[u_di]
        jcxz .ok
        call put_str_seg
.ok:    clc
.ret:   ret

fatname db 'FAT',0
fat32name db 'FAT32',0

; ---------------------------------------------------------------------------
; 71A6h - file information by handle (BX handle, DS:DX 52 byte buffer)
; ---------------------------------------------------------------------------
f_fileinfo:
        mov bx,[u_bx]
        mov ax,4400h
        int 21h
        jc .ret
        test dl,80h
        jnz .acc
        and dl,3Fh
        mov [fi_drive],dl
        mov ax,5700h
        int 21h
        jc .ret
        mov [fi_time],cx
        mov [fi_date],dx
        mov ax,4201h                    ; current position
        xor cx,cx
        xor dx,dx
        int 21h
        jc .ret
        push dx
        push ax
        mov ax,4202h
        xor cx,cx
        xor dx,dx
        int 21h
        mov [fi_size],ax
        mov [fi_size+2],dx
        pop dx
        pop cx
        mov ax,4200h
        int 21h
        xor ax,ax
        mov [dv_serial],ax
        mov [dv_serial+2],ax
        mov al,[fi_drive]
        call drive_setup
        ; fill
        push es
        mov es,[u_ds]
        mov di,[u_dx]
        mov ax,[u_si]
        mov [fi_svsi],ax
        mov ax,20h
        stosw
        xor ax,ax
        stosw
        mov word [u_si],0
        mov cx,3
.t:     push cx
        mov dx,[fi_date]
        mov cx,[fi_time]
        xor bl,bl
        call put_time
        pop cx
        loop .t
        mov ax,[dv_serial]
        stosw
        mov ax,[dv_serial+2]
        stosw
        xor ax,ax
        stosw
        stosw
        mov ax,[fi_size]
        stosw
        mov ax,[fi_size+2]
        stosw
        mov ax,1
        stosw
        xor ax,ax
        stosw
        stosw
        stosw
        stosw
        stosw
        pop es
        mov ax,[fi_svsi]
        mov [u_si],ax
        clc
.ret:   ret
.acc:   mov ax,5
        stc
        ret

; ---------------------------------------------------------------------------
; 71A7h - time conversion
;   BL=0: DS:SI FILETIME -> CX time, DX date, BH 10ms
;   BL=1: CX time, DX date, BH 10ms -> ES:DI FILETIME
; ---------------------------------------------------------------------------
f_timeconv:
        mov bl,[u_bx]
        cmp bl,1
        ja .inv
        je .todos
        push ds
        mov si,[u_si]
        mov ds,[u_ds]
        mov di,acc
        mov cx,4
        rep movsw
        pop ds
        call ft2dos
        jc .inv
        mov [u_cx],cx
        mov [u_dx],dx
        mov [u_bx+1],bh
        clc
        ret
.todos: mov cx,[u_cx]
        mov dx,[u_dx]
        mov bl,[u_bx+1]
        call dos2ft
        push es
        mov es,[u_es]
        mov di,[u_di]
        call acc_store
        pop es
        clc
        ret
.inv:   mov ax,1
        stc
        ret

; ---------------------------------------------------------------------------
; 71A8h - generate short name (DS:SI long name, ES:DI buffer,
;         DH=0 11 byte FCB format, DH=1 "NAME.EXT")
; ---------------------------------------------------------------------------
f_genshort:
        call get_path_dssi
        jc .ret
        mov si,inpath
        call check_lfn
        jc .bad
        mov byte [ma_nodir],1
        call make_alias
        mov byte [ma_nodir],0
        jc .bad
        push es
        mov es,[u_es]
        mov di,[u_di]
        cmp byte [u_dx+1],0
        jne .dot
        mov si,alias11
        mov cx,11
        rep movsb
        pop es
        clc
        ret
.dot:   pop es
        mov si,alias11
        mov di,tmpbuf
        call fmt_name11
        mov si,tmpbuf
        mov ax,[u_es]
        mov di,[u_di]
        xor cx,cx
        call put_str_seg
        clc
.ret:   ret
.bad:   mov ax,3
        stc
        ret

; ---------------------------------------------------------------------------
; 71FFh - installation check (BX = 'LF' magic) -> AX=0, BX=segment, CX=ver
; ---------------------------------------------------------------------------
f_check:
        cmp word [u_bx],MAGIC
        jne .no
        mov word [u_ax],0
        mov [u_bx],cs
        mov word [u_cx],VERSION
        clc
        ret
.no:    mov ax,7100h
        stc
        ret
