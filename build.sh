#!/bin/bash
# ScanPort 编译脚本（Linux + MinGW-w64 交叉编译，32 位目标）
# Ubuntu: sudo apt install gcc-mingw-w64-i686
i686-w64-mingw32-windres scanport.rc -O coff -o scanport.res
i686-w64-mingw32-gcc -Os -s -mwindows -nostdlib -ffreestanding -fno-builtin -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-ident -finput-charset=UTF-8 -fexec-charset=GBK -Wl,-e,_Entry@0 -o ScanPort.exe main.c scanport.res -lkernel32 -luser32 -lgdi32 -lws2_32 -lcomctl32
