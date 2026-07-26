@echo off
windres resource.rc -o resource.o
gcc main.c resource.o -o VibeClicker.exe -lwinmm
pause