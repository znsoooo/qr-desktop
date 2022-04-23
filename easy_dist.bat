@echo off
taskkill /f /im qrcode.exe
windres -i icon.rc -o icon.o -F pe-i386
g++ main.cpp qrcodegen.cpp icon.o -o qrcode.exe -m32 -s -static -DUNICODE -mwindows -municode
upx -9 qrcode.exe
start qrcode.exe
pause
