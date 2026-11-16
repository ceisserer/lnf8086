@echo off
A:\LFNTSR > A:\OUT.TXT
SET LT=A:\LT
D:
%LT% vol D:\ >> A:\OUT.TXT
%LT% md "D:\LONGDIR" >> A:\OUT.TXT
%LT% cd "D:\LONGDIR" >> A:\OUT.TXT
%LT% pwd >> A:\OUT.TXT
%LT% mk "D:\LONGDIR\FILE.TXT" "on fat32" >> A:\OUT.TXT
%LT% mknew "D:\LONGDIR\FILE.TXT" x >> A:\OUT.TXT
%LT% mk "D:\LONGDIR\SECOND.TXT" "second" >> A:\OUT.TXT
%LT% cat FILE.TXT >> A:\OUT.TXT
%LT% dir "*.*" >> A:\OUT.TXT
%LT% dir "*.txt" >> A:\OUT.TXT
%LT% ren FILE.TXT RENAMED.TXT >> A:\OUT.TXT
%LT% short RENAMED.TXT >> A:\OUT.TXT
%LT% long RENAMED.TXT >> A:\OUT.TXT
%LT% attr RENAMED.TXT >> A:\OUT.TXT
%LT% del RENAMED.TXT >> A:\OUT.TXT
%LT% delw "*.TXT" >> A:\OUT.TXT
%LT% dir "*.*" >> A:\OUT.TXT
%LT% cd \ >> A:\OUT.TXT
%LT% rd LONGDIR >> A:\OUT.TXT
%LT% md "Long Name On FAT32" >> A:\OUT.TXT
REM ---- FAT32: long names make the root directory (a cluster chain) grow
for %%i in (1 2 3 4 5 6 7 8 9 10) do %LT% mk "D:\A long file name in the FAT32 root directory %%i" %%i >> A:\NUL
for %%i in (11 12 13 14 15 16 17 18 19 20) do %LT% mk "D:\A long file name in the FAT32 root directory %%i" %%i >> A:\NUL
%LT% dir "D:\*directory 1*" >> A:\OUT.TXT
%LT% cat "D:\A long file name in the FAT32 root directory 17" >> A:\OUT.TXT
%LT% delw "D:\A long file name in the FAT32 root directory 1?" >> A:\OUT.TXT
%LT% dir "D:\*" >> A:\OUT.TXT
REM ---- cross device rename and growing directories with LFN on C:
C:
%LT% md "C:\Grow" >> A:\OUT.TXT
for %%i in (1 2 3 4 5 6 7 8 9 10) do %LT% mk "C:\Grow\A very long file name to make the directory grow quickly number %%i" %%i >> A:\NUL
for %%i in (11 12 13 14 15 16 17 18 19 20) do %LT% mk "C:\Grow\A very long file name to make the directory grow quickly number %%i" %%i >> A:\NUL
for %%i in (21 22 23 24 25 26 27 28 29 30) do %LT% mk "C:\Grow\A very long file name to make the directory grow quickly number %%i" %%i >> A:\NUL
for %%i in (31 32 33 34 35 36 37 38 39 40) do %LT% mk "C:\Grow\A very long file name to make the directory grow quickly number %%i" %%i >> A:\NUL
for %%i in (41 42 43 44 45 46 47 48 49 50) do %LT% mk "C:\Grow\A very long file name to make the directory grow quickly number %%i" %%i >> A:\NUL
%LT% cat "C:\Grow\A very long file name to make the directory grow quickly number 50" >> A:\OUT.TXT
%LT% ren "C:\Grow\A very long file name to make the directory grow quickly number 50" "D:\X.TXT" >> A:\OUT.TXT
%LT% dir "C:\Grow\*number 4*" >> A:\OUT.TXT
%LT% delw "C:\Grow\*" >> A:\OUT.TXT
%LT% rd "C:\Grow" >> A:\OUT.TXT
A:\EXIT
