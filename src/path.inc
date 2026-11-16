; ===========================================================================
; path.inc - long path -> short path resolution
; ===========================================================================

RF_PATTERN equ 1                ; last component is a search pattern
MAXCOMP    equ 64

; ---------------------------------------------------------------------------
; fetch_str - copy NUL terminated string AX:DX (user memory) to DI (ours)
;   max 260 chars.  CF if too long or empty.
; ---------------------------------------------------------------------------
fetch_str:
        push ax
        push cx
        push si
        push di
        push ds
        mov si,dx
        mov ds,ax
        mov cx,MAXPATH+1
.l:     lodsb
        stosb
        or al,al
        jz .end
        loop .l
        pop ds
        stc
        jmp short .x
.end:   pop ds
        cmp cx,MAXPATH+1                ; empty string?
        je .bad
        clc
        jmp short .x
.bad:   stc
.x:     pop di
        pop si
        pop cx
        pop ax
        ret

; put_str - copy NUL terminated SI (ours) to user memory [u_es]:DI.
;   CX = buffer size (0 = unlimited).  CF if it does not fit.
put_str:
        push ax
        push cx
        push si
        push di
        push es
        mov es,[u_es]
        jcxz .unl
        push cx
        call strlen
        mov ax,cx
        pop cx
        cmp ax,cx
        jae .big
.unl:   lodsb
        stosb
        or al,al
        jnz .unl
        clc
        jmp short .x
.big:   stc
.x:     pop es
        pop di
        pop si
        pop cx
        pop ax
        ret

; ---------------------------------------------------------------------------
; split_path - append the components of path SI to the component list
;   ("." dropped, ".." pops down to [minc]).  CF if too many/too long.
; ---------------------------------------------------------------------------
split_path:
        push ax
        push bx
        push cx
        push si
        push di
        mov di,[wptr]
.comp:  ; skip separators
        lodsb
        cmp al,'\'
        je .comp
        cmp al,'/'
        je .comp
        or al,al
        jz .done
        dec si
        mov bx,di                       ; start of component
.cc:    lodsb
        cmp al,'\'
        je .ce
        cmp al,'/'
        je .ce
        or al,al
        jz .ce
        cmp di,wpath+WPATHSZ-2
        jae .bad
        stosb
        jmp short .cc
.ce:    mov byte [di],0
        dec si                          ; re-read the separator / NUL
        ; "." / ".."
        cmp byte [bx],'.'
        jne .add
        cmp byte [bx+1],0
        je .drop
        cmp word [bx+1],'.'             ; ".", 0
        jne .add
        ; ".."
        mov ax,[ncomps]
        cmp ax,[minc]
        jbe .drop
        dec word [ncomps]
        mov di,[ncomps]
        shl di,1
        mov di,[comps+di]               ; reuse the popped space
        jmp short .comp
.drop:  mov di,bx
        jmp short .comp
.add:   inc di
        mov ax,[ncomps]
        cmp ax,MAXCOMP
        jae .bad
        push di
        mov di,ax
        shl di,1
        mov [comps+di],bx
        pop di
        inc word [ncomps]
        jmp short .comp
.done:  mov [wptr],di
        clc
        jmp short .x
.bad:   stc
.x:     pop di
        pop si
        pop cx
        pop bx
        pop ax
        ret

; trim_comp - remove trailing dots and blanks of component SI
;   CF if nothing remains
trim_comp:
        push bx
        push cx
        call strlen
        mov bx,si
        add bx,cx
.l:     cmp bx,si
        je .empty
        cmp byte [bx-1],'.'
        je .cut
        cmp byte [bx-1],' '
        jne .ok
.cut:   dec bx
        mov byte [bx],0
        jmp short .l
.ok:    clc
        jmp short .x
.empty: stc
.x:     pop cx
        pop bx
        ret

; app_comp - append '\' + string SI to the path whose end is [BX]
;   (BX = address of the end pointer variable); CF if it would get too long
;   DX = address of the buffer start (for the length check)
app_comp:
        push ax
        push cx
        push di
        call strlen
        mov di,[bx]
        mov ax,di
        sub ax,dx
        add ax,cx
        inc ax
        cmp ax,MAXPATH-1
        jae .big
        mov al,'\'
        stosb
        call strcpy
        mov [bx],di
        clc
        jmp short .x
.big:   stc
.x:     pop di
        pop cx
        pop ax
        ret

; ---------------------------------------------------------------------------
; resolve - resolve the path at SI.  AL = flags (RF_PATTERN)
;   out: CF set -> AX = DOS error code
;        [rp_mode] 0 = handled by us (FAT drive), 1 = pass through to DOS
;        pshort / plong   physical short / long path ("C:\DIR\FILE")
;        rp_srest/rp_lrest  where the part below the logical root starts
;        rp_slast/rp_llast  where the last component starts (after '\')
;        rp_found, rp_isroot, rp_parent (cluster of the containing dir)
;        lastname = last component as given; fr_* for found last component
; ---------------------------------------------------------------------------
resolve:
        push bx
        push cx
        push dx
        push si
        push di
        mov [rf_flags],al
        mov [rp_src],si
        mov byte [rp_mode],0
        mov byte [rp_isroot],0
        mov byte [rp_found],0
        mov byte [rp_isdev],0
        DBG 'R'
        ; UNC path -> network
        mov al,[si]
        cmp al,'\'
        je .unc1
        cmp al,'/'
        jne .drv
.unc1:  mov al,[si+1]
        cmp al,'\'
        je .pass
        cmp al,'/'
        je .pass
.drv:   cmp byte [si+1],':'
        jne .curd
        mov al,[si]
        call upcase
        sub al,'A'
        jb .baddrv
        cmp al,25
        ja .baddrv
        add si,2
        jmp short .hdrv
.curd:  mov ah,19h
        int 21h
.hdrv:  mov [rp_ldrive],al
        mov [rp_rest],si
        ; canonical name of "X:\" (resolves SUBST / JOIN)
        add al,'A'
        mov [tnin],al
        mov word [tnin+1],':\'
        mov byte [tnin+3],0
        push si
        mov si,tnin
        mov di,tnroot
        mov ah,60h
        int 21h
        pop si
        jc .baddrv
        cmp word [tnroot],'\\'
        je .pass
        mov al,[tnroot]
        call upcase
        mov [tnroot],al
        sub al,'A'
        jb .pass
        cmp al,25
        ja .pass
        mov [rp_pdrive],al
        ; remote drive?
        mov ax,4409h
        mov bl,[rp_ldrive]
        inc bl
        int 21h
        jc .baddrv
        DBG 'i'
        test dh,10h                     ; bit 12: remote
        jnz .pass
        mov al,[rp_pdrive]
        DBG 's'
        call drive_setup
        jc .pass
        DBG 'S'
        ; ---- build component list
        mov word [ncomps],0
        mov word [minc],0
        mov word [wptr],wpath
        push si
        mov si,tnroot+2
        call split_path
        pop si
        jc .toolong
        mov ax,[ncomps]
        mov [minc],ax
        mov al,[si]
        cmp al,'\'
        je .abs
        cmp al,'/'
        je .abs
        push si
        mov dl,[rp_ldrive]
        inc dl
        mov si,cwdbuf
        mov ah,47h
        int 21h
        jc .cwdbad
        call split_path
.cwdbad:
        pop si
        jc .toolong
.abs:   call split_path
        jc .toolong
        ; ---- walk
        mov al,[tnroot]
        mov ah,':'
        mov [pshort],ax
        mov [plong],ax
        mov byte [pshort+2],0
        mov byte [plong+2],0
        mov word [ps_end],pshort+2
        mov word [pl_end],plong+2
        mov word [rp_srest],0
        mov word [rp_lrest],0
        xor ax,ax
        mov [rp_parent],ax
        mov [rp_parent+2],ax
        mov [cur_clus],ax
        mov [cur_clus+2],ax
        mov ax,[ncomps]
        cmp ax,[minc]
        jne .walk
        ; the logical root itself
        test byte [rf_flags],RF_PATTERN
        jnz .pnf
        mov byte [rp_isroot],1
        mov byte [rp_found],1
        mov byte [lastname],0
        jmp .fin
.walk:  xor cx,cx                       ; CX = component index
.wl:    cmp cx,[minc]
        jne .nmin
        mov ax,[ps_end]
        mov [rp_srest],ax
        mov ax,[pl_end]
        mov [rp_lrest],ax
.nmin:  mov bx,cx
        shl bx,1
        mov si,[comps+bx]
        mov ax,[ncomps]
        dec ax
        cmp cx,ax
        je .last
        ; ---- intermediate component: must be an existing directory
        call trim_comp
        jc .pnf
        call has_wild
        jc .pnf
        DIRSET cur_clus
        call dir_find
        jc .pnf_io
        test byte [fr_ent+0Bh],10h
        jz .pnf
        push bx
        mov bx,fr_ent
        call ent_clus
        pop bx
        mov [cur_clus],ax
        mov [cur_clus+2],dx
        push si
        mov si,fr_sfn
        mov bx,ps_end
        mov dx,pshort
        call app_comp
        mov si,fr_name
        mov bx,pl_end
        mov dx,plong
        jc .tl2
        call app_comp
.tl2:   pop si
        jc .toolong
        inc cx
        jmp .wl
        ; ---- last component
.last:  CLUSCPY rp_parent,cur_clus
        mov di,lastname
        test byte [rf_flags],RF_PATTERN
        jnz .lpat
        call trim_comp
        jc .fnf
        call strcpy
        call has_wild
        jc .fnf
        call is_device                  ; CON, NUL, ... -> DOS
        jc .dev
        DIRSET cur_clus
        call dir_find
        jc .lnf
        mov byte [rp_found],1
        mov ax,[ps_end]
        inc ax
        mov [rp_slast],ax
        mov ax,[pl_end]
        inc ax
        mov [rp_llast],ax
        mov si,fr_sfn
        mov bx,ps_end
        mov dx,pshort
        call app_comp
        jc .toolong
        mov si,fr_name
        mov bx,pl_end
        mov dx,plong
        call app_comp
        jc .toolong
        jmp short .fin
.dev:   ; a device: DOS gets the short parent path + the device name
        mov byte [rp_isdev],1
        jmp short .lapp
.lnf:   cmp byte [dir_ioerr],0
        jne .ioerr
        jmp short .lapp
.lpat:  call strcpy
.lapp:  mov ax,[ps_end]
        inc ax
        mov [rp_slast],ax
        mov ax,[pl_end]
        inc ax
        mov [rp_llast],ax
        mov bx,ps_end
        mov dx,pshort
        call app_comp
        jc .toolong
        mov bx,pl_end
        mov dx,plong
        call app_comp
        jc .toolong
.fin:   ; logical rest pointers default to the end of the path
        cmp word [rp_srest],0
        jne .f1
        mov ax,[ps_end]
        mov [rp_srest],ax
.f1:    cmp word [rp_lrest],0
        jne .f2
        mov ax,[pl_end]
        mov [rp_lrest],ax
.f2:    cmp byte [rp_isdev],0
        je .f3
        mov byte [rp_mode],1
        call logical_short
.f3:    clc
        jmp short .ret
.pass:  ; let DOS handle it: copy the original path
        DBG 'P'
        mov byte [rp_mode],1
        mov si,[rp_src]
        mov di,outlog
        call strcpy
        clc
        jmp short .ret
.pnf_io:
        cmp byte [dir_ioerr],0
        jne .ioerr
.pnf:   mov ax,3
        jmp short .err
.fnf:   mov ax,2
        jmp short .err
.toolong:
        mov ax,3
        jmp short .err
.ioerr: mov ax,21                       ; drive not ready / I/O error
        jmp short .err
.baddrv:
        mov ax,15
.err:   stc
.ret:   pop di
        pop si
        pop dx
        pop cx
        pop bx
        ret

; ---------------------------------------------------------------------------
; mk_logical - build the path for DOS calls in outlog:
;   logical drive letter + ':' + short path below the logical root
;   (SI = start of the rest, i.e. [rp_srest]; "" -> "\")
; ---------------------------------------------------------------------------
mk_logical:
        push ax
        push si
        push di
        mov di,outlog
        mov al,[rp_ldrive]
        add al,'A'
        mov ah,':'
        stosw
        cmp byte [si],0
        jne .cp
        mov si,bslash
.cp:    call strcpy
        pop di
        pop si
        pop ax
        ret

bslash  db '\',0

; logical_short - outlog = logical short path of the resolved object
logical_short:
        push si
        mov si,[rp_srest]
        call mk_logical
        pop si
        ret

; set_last_short - replace the last component of pshort with the 11 byte
;   name at SI (formatted) and rebuild outlog
set_last_short:
        push di
        mov di,[rp_slast]
        call fmt_name11
        mov [ps_end],di
        pop di
        jmp short logical_short

; set_last_sfn - replace the last component of pshort with string SI
set_last_str:
        push di
        mov di,[rp_slast]
        call strcpy
        mov [ps_end],di
        pop di
        jmp short logical_short
