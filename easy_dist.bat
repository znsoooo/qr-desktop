@echo off
taskkill /f /im qrcode.exe
windres -i icon.rc -o icon.o -F pe-i386
gcc *.c icon.o -o qrcode.exe -m32 -s -static -mwindows
upx -9 qrcode.exe
start qrcode.exe
pause
