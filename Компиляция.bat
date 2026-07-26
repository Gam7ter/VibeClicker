@echo off
windres resource.rc -o resource.o
gcc Click.c resource.o -o Click.exe -lwinmm
pause