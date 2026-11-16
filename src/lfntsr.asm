; ===========================================================================
; LFNTSR - Long File Name API for plain DOS
;
; A TSR that implements the Windows 95 long file name API (INT 21h AX=71xxh)
; on FAT12/FAT16/FAT32 drives by reading and writing VFAT directory entries
; directly (INT 25h/26h, INT 21h/7305h).  Drives it cannot handle (network,
; CD-ROM) get the same API mapped onto the normal short name DOS calls.
;
; Requires: DOS 3.1+, 8086/8088 CPU.  Assemble with NASM:
;       nasm -f bin -o lfntsr.com lfntsr.asm
;
; Copyright (C) 2026 the lnf286 authors
;
; This program is free software: you can redistribute it and/or modify it
; under the terms of the GNU General Public License as published by the Free
; Software Foundation, either version 3 of the License, or (at your option)
; any later version.  This program is distributed in the hope that it will
; be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
; Public License (file LICENSE) for more details.
; ===========================================================================

        cpu 8086
        bits 16
        org 100h

VERSION equ 0110h               ; 1.10
MAGIC   equ 4C46h               ; 'LF' install check signature
MAXPATH equ 260
NHANDLES equ 8                  ; default number of find handles (/H:n)
MAXHANDLES equ 64
NTUNNELS equ 8                  ; default number of tunnel entries (/T:n)
MAXTUNNELS equ 32
HBASE   equ 1                   ; first find handle number
STACKSZ equ 1536
WPATHSZ equ 600

; find handle structure
H_USED  equ 0
H_MODE  equ 1                   ; 0 = FAT search, 1 = DOS search
H_DRIVE equ 2
H_ALLOW equ 3
H_REQ   equ 4
H_CLUS  equ 6                   ; directory (dword)
H_IDX   equ 10
H_LRU   equ 12
H_GEN   equ 14                  ; generation (high byte of the handle)
H_PAT   equ 15                  ; pattern (FAT) or DTA (DOS)
HSIZE   equ H_PAT + MAXPATH + 1

; DBG 'x' - with -DDEBUG write a character to the Bochs/QEMU debug port E9h
%macro DBG 1
%ifdef DEBUG
        pushf
        push ax
        mov al,%1
        out 0E9h,al
        pop ax
        popf
%endif
%endmacro

start:  jmp install

; ===========================================================================
; Resident part
; ===========================================================================
old21     dd 0
nuldev    dd 0                  ; NUL device header (List of Lists + 22h)
dosmajor  db 0
nhandles  dw NHANDLES           ; find handles (set by the installer)
handles   dw 0                  ; -> handle table (after the stack)
ntunnels  dw NTUNNELS           ; tunnel entries (0 = no tunnelling)
tunnels   dw 0                  ; -> tunnel table (after the handles)
dyn_end   dw 0                  ; end of the resident part
uptab     times 128 db 0        ; upper case table for 80h..FFh
cptab     times 128 dw 0        ; Unicode for OEM 80h..FFh

; ---------------------------------------------------------------------------
; INT 21h entry
;   AH=71h               long file name API (dispatch)
;   3Ch 5Bh 6Ch 41h 56h  plain DOS create / delete / rename and the FCB
;   13h 17h              delete / rename: long name tunnelling and removal
;                        of the long name entries (tunnel.inc)
; Our own DOS calls arrive while [busy] is set and go straight to DOS.
; ---------------------------------------------------------------------------
int21_handler:
        cmp ah,71h
        je .ours
        cmp byte [cs:busy],0
        jne .chain
        cmp ah,41h
        je .plain
        cmp ah,56h
        je .plain
        cmp ah,3Ch
        je .plain
        cmp ah,5Bh
        je .plain
        cmp ah,13h
        je .plain
        cmp ah,17h
        je .plain
        cmp ax,6C00h
        je .plain
.chain: jmp far [cs:old21]
.ours:  call sess_enter
        jc .busy
        call dispatch
        jnc .ok
        mov [u_ax],ax
.ok:    call set_user_cf
        call sess_leave
        iret
.busy:  ; re-entered (from an interrupt handler): a real error, not 7100h,
        ; so that callers do not fall back to the short name functions
        push bp
        mov bp,sp
        or word [bp+6],1
        pop bp
        mov ax,5
        iret
.plain: sti
        sub sp,PS_N*2                   ; room for the pre -> post state
        call plain_pre
        sti
        pushf
        call far [cs:old21]
        pushf                           ; DOS result flags
        call plain_post
        add sp,2+PS_N*2
        iret

; ---------------------------------------------------------------------------
; sess_enter - (near call on the caller's stack) start working on a call:
;   enter the Windows critical section (INT 2Fh/1681h, so that no other DOS
;   box runs LFNTSR code meanwhile; harmless without Windows), claim [busy],
;   save the registers in u_*, switch to our stack, DS = ES = CS, install
;   our INT 23h/24h handlers.
;   out: CF=1 busy (nothing changed, registers preserved)
; sess_leave - the reverse; registers come from u_*.  Interrupts stay
;   disabled on return (the caller's IRET restores them).
; ---------------------------------------------------------------------------
sess_enter:
        push ax
        mov ax,1681h
        int 2Fh
        pop ax
        cli
        cmp byte [cs:busy],0
        jne .busy
        mov byte [cs:busy],1
        pop word [cs:sess_ret]
        mov [cs:u_ax],ax
        mov [cs:u_bx],bx
        mov [cs:u_cx],cx
        mov [cs:u_dx],dx
        mov [cs:u_si],si
        mov [cs:u_di],di
        mov [cs:u_bp],bp
        mov [cs:u_ds],ds
        mov [cs:u_es],es
        mov [cs:u_ss],ss
        mov [cs:u_sp],sp
        mov ax,cs
        mov ss,ax
        mov sp,stack_top
        sti
        mov ds,ax
        mov es,ax
        cld
        call int24_set
        clc
        jmp [sess_ret]
.busy:  sti
        push ax
        mov ax,1682h
        int 2Fh
        pop ax
        stc
        ret

sess_leave:
        pop word [sess_ret]
        call int24_restore
        cli
        mov ss,[u_ss]
        mov sp,[u_sp]
        push word [sess_ret]
        push word [u_ax]
        mov bx,[u_bx]
        mov cx,[u_cx]
        mov dx,[u_dx]
        mov si,[u_si]
        mov di,[u_di]
        mov bp,[u_bp]
        mov es,[u_es]
        mov ds,[u_ds]
        mov byte [cs:busy],0
        mov ax,1682h
        int 2Fh
        cli
        pop ax
        ret

; set_user_cf - copy CF into the flags of the caller's INT 21h frame
;   (71xxh calls: the frame is at [u_ss:u_sp])
set_user_cf:
        push ax
        push bx
        push es
        mov al,0
        adc al,0
        mov es,[u_ss]
        mov bx,[u_sp]
        and byte [es:bx+4],0FEh
        or [es:bx+4],al
        pop es
        pop bx
        pop ax
        ret

; While we work: critical errors always "fail", Ctrl-C/Break is ignored
; (DOS must not abort the program while we are in the middle of a call).
int24_handler:
        mov al,3
int23_handler:
        iret

int24_set:
        push ax
        push es
        xor ax,ax
        mov es,ax
        cli
        mov ax,[es:23h*4]
        mov [sv_int23],ax
        mov ax,[es:23h*4+2]
        mov [sv_int23+2],ax
        mov ax,[es:24h*4]
        mov [sv_int24],ax
        mov ax,[es:24h*4+2]
        mov [sv_int24+2],ax
        mov word [es:23h*4],int23_handler
        mov [es:23h*4+2],cs
        mov word [es:24h*4],int24_handler
        mov [es:24h*4+2],cs
        sti
        pop es
        pop ax
        ret

int24_restore:
        push ax
        push es
        xor ax,ax
        mov es,ax
        cli
        mov ax,[sv_int23]
        mov [es:23h*4],ax
        mov ax,[sv_int23+2]
        mov [es:23h*4+2],ax
        mov ax,[sv_int24]
        mov [es:24h*4],ax
        mov ax,[sv_int24+2]
        mov [es:24h*4+2],ax
        sti
        pop es
        pop ax
        ret

; ---------------------------------------------------------------------------
; is_device - CF if the name SI (base name before the first dot) is the
;   name of a character device (walks the DOS device driver chain)
; ---------------------------------------------------------------------------
is_device:
        push ax
        push bx
        push cx
        push si
        push di
        push es
        mov di,devname
        mov cx,8
        mov al,' '
        rep stosb
        mov di,devname
        mov cx,8
.b:     lodsb
        or al,al
        jz .bd
        cmp al,'.'
        je .bd
        jcxz .no
        call upcase
        stosb
        dec cx
        jmp short .b
.bd:    cmp di,devname
        je .no
        les bx,[nuldev]
        mov ax,200                      ; guard against broken chains
.dl:    test byte [es:bx+5],80h         ; character device?
        jz .nx
        mov si,devname
        lea di,[bx+0Ah]
        mov cx,8
        repe cmpsb
        je .yes
.nx:    les bx,[es:bx]
        cmp bx,0FFFFh
        je .no
        dec ax
        jnz .dl
.no:    clc
        jmp short .x
.yes:   stc
.x:     pop es
        pop di
        pop si
        pop cx
        pop bx
        pop ax
        ret

; ---------------------------------------------------------------------------
; dispatch on AL
; ---------------------------------------------------------------------------
disp_tab:
        db 0Dh
        dw f_reset
        db 39h
        dw f_mkdir
        db 3Ah
        dw f_rmdir
        db 3Bh
        dw f_chdir
        db 41h
        dw f_delete
        db 43h
        dw f_attr
        db 47h
        dw f_getcwd
        db 4Eh
        dw f_findfirst
        db 4Fh
        dw f_findnext
        db 56h
        dw f_rename
        db 60h
        dw f_truename
        db 6Ch
        dw f_open
        db 0A0h
        dw f_volinfo
        db 0A1h
        dw f_findclose
        db 0A6h
        dw f_fileinfo
        db 0A7h
        dw f_timeconv
        db 0A8h
        dw f_genshort
        db 0A9h
        dw f_open
        db 0FFh
        dw f_check
DISP_N  equ ($-disp_tab)/3

dispatch:
        mov al,[u_ax]
        mov si,disp_tab
        mov cx,DISP_N
.l:     cmp al,[si]
        je .hit
        add si,3
        loop .l
        mov ax,7100h
        stc
        ret
.hit:   push word [si+1]
        ; every call starts with fresh sector caches
        mov byte [c_dir+C_VALID],0
        mov byte [c_fat+C_VALID],0
        mov byte [ma_nodir],0
        ret                             ; -> handler

%include "disk.inc"
%include "names.inc"
%include "time.inc"
%include "path.inc"
%include "funcs.inc"
%include "tunnel.inc"

; ---------------------------------------------------------------------------
; go_resident - last step of the installation (runs from resident code
; because it clears the uninitialized data that overlays the installer)
;   DX = paragraphs to keep
; ---------------------------------------------------------------------------
go_resident:
        mov bx,dx
        mov di,bss_start
        mov cx,[dyn_end]
        sub cx,di
        xor al,al
        rep stosb
        mov word [c_dir+C_BUF],dirbuf
        mov word [c_fat+C_BUF],fatbuf
        mov byte [c_fat+C_ISFAT],1
        mov ax,2521h
        mov dx,int21_handler
        int 21h
        mov dx,bx
        mov ax,3100h
        int 21h

resident_end:

; ===========================================================================
; Transient part (installer)
; ===========================================================================
%include "install.inc"
%include "codepage.inc"

transient_end:

; ===========================================================================
; Uninitialized resident data (overlays the installer)
; ===========================================================================
        absolute resident_end
        alignb 2
bss_start:
busy      resb 1
          resb 1
sess_ret  resw 1
u_ax      resw 1
u_bx      resw 1
u_cx      resw 1
u_dx      resw 1
u_si      resw 1
u_di      resw 1
u_bp      resw 1
u_ds      resw 1
u_es      resw 1
u_ss      resw 1
u_sp      resw 1
sv_int24  resd 1
sv_int23  resd 1
devname   resb 8
sv_dta    resd 1

; disk layer
io_sec    resd 1
io_buf    resw 1
io_write  resb 1
dir_ioerr resb 1
pkt_sec   resd 1                ; INT 25h/26h packet (keep together)
pkt_cnt   resw 1
pkt_buf   resd 1
c_dir     resb 10               ; cache descriptors
c_fat     resb 10
dv_ok     resb 1
dv_drive  resb 1
dv_fixed  resb 1
dv_ext    resb 1
dv_spc    resb 1
dv_spcshift resb 1
dv_nfats  resb 1                ; FAT copies to write
dv_fat16  resb 1
dv_fat32  resb 1
          resb 1
dv_fatstart resd 1              ; first (active) FAT
dv_fatsz  resd 1
dv_rootents resw 1              ; FAT12/16 fixed root directory
dv_rootstart resw 1
dv_datastart resd 1
dv_maxclust resd 1
dv_freehint resd 1
dv_rootclus resd 1              ; FAT32 root directory
dv_serial resd 1
d_clus    resd 1
d_cc      resd 1
d_ci      resw 1
xd_last   resd 1                ; dir_extend
xd_cand   resd 1
xd_left   resd 1
fs_val    resd 1                ; fat_set
SDPB_SZ   equ 24
sdpb      resb SDPB_SZ          ; INT 21h/7304h structure
XDPB_SZ   equ 64
xdpb      resb XDPB_SZ          ; INT 21h/7302h extended DPB

; directory scanning
it_idx    resw 1
lfn_start resw 1
lfn_len   resw 1
lfn_next  resb 1
lfn_chk   resb 1
lfn_lossy resb 1
lfn_first resb 1
fmt_flags resb 1
fr_haslfn resb 1
fr_uflags resb 1
p83_lower resb 1
fmt_first resw 1
fr_idx    resw 1
fr_lfnidx resw 1
sfn_idx   resw 1
fr_ent    resb 32
fr_sfn    resb 14
fr_name   resb 262
alias11   resb 12
needlfn   resb 1
ma_lossy  resb 1
ma_nodir  resb 1
          resb 1
ma_start  resw 1
ma_dot    resw 1
ma_baselen resw 1
ma_n      resd 1
ma_tail   resb 8
ma_base   resb 8

; time
acc       resw 4
t_time    resw 1
t_date    resw 1
t_ms      resb 1
t_sec     resb 1
t_min     resb 1
t_hour    resb 1

; path resolution
rf_flags  resb 1
rp_mode   resb 1
rp_isroot resb 1
rp_found  resb 1
rp_isdev  resb 1
rp_ldrive resb 1
rp_pdrive resb 1
rp_src    resw 1
rp_rest   resw 1
rp_srest  resw 1
rp_lrest  resw 1
rp_slast  resw 1
rp_llast  resw 1
rp_parent resd 1
cur_clus  resd 1
ps_end    resw 1
pl_end    resw 1
wptr      resw 1
ncomps    resw 1
minc      resw 1
comps     resw MAXCOMP
tnin      resb 4
tnroot    resb 128
cwdbuf    resb 68
wpath     resb WPATHSZ

; handlers
run_past  resb 1
wl_n      resb 1
wl_chk    resb 1
o_haslfn  resb 1
o_drive   resb 1
del_fn    resb 1
eo_exists resb 1
fi_drive  resb 1
at_fn     resb 1
wl_idx    resw 1
wl_name   resw 1
wl_len    resw 1
al_idx    resw 1
o_parent  resd 1
o_idx     resw 1
o_lfnidx  resw 1
n_parent  resd 1
dw_count  resw 1
dw_err    resw 1
dw_it     resw 1
dw_dirend resw 1
dw_dta    resb 44
op_n      resw 1
op_run    resw 1
vi_size   resw 1
fi_time   resw 1
fi_date   resw 1
fi_svsi   resw 1
fi_size   resd 1
lru_ctr   resw 1
o_name11  resb 12
tmpent    resb 32
nf_attr   resb 1

; plain DOS calls (tunnel.inc); ps_flags .. ps_cx is the state (PS_N words)
pat_wild  resb 1
sc_skip   resb 1
tk_drive  resb 1
sw_seq    resb 1
sw_chk    resb 1
          alignb 2
ps_vars:
ps_flags  resw 1
ps_drive  resw 1
ps_clus   resd 1
ps_a      resw 1
ps_b      resw 1
ps_dx     resw 1
ps_cx     resw 1
ps_dosf   resw 1
tk_clus   resd 1
tk_name   resw 1
t_hit     resw 1
sw_start  resw 1
pat11     resb 12

inpath    resb MAXPATH+2
inpath2   resb MAXPATH+2
lastname  resb MAXPATH+2
pshort    resb MAXPATH+16
plong     resb MAXPATH+16
outlog    resb MAXPATH+16
oldlog    resb MAXPATH+16
tmpbuf    resb MAXPATH+16
mt_tmp    resb MAXPATH+2

          alignb 2
dirbuf    resb 512
fatbuf    resb 512
          alignb 2
          resb STACKSZ
stack_top:
bss_end:                        ; handle and tunnel tables follow
