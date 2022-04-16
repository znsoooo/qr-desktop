@echo off
taskkill /f /im qrcode.exe
g++ -o qrcode.exe qrcodegen.cpp main.cpp -m32 -s -static -DWINVER=0x0A00 -DUNICODE  -mwindows -municode
upx -9 qrcode.exe
start qrcode.exe
pause
