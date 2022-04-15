taskkill /f /im qrcode.exe
g++ -o qrcode.exe qrcodegen.cpp main.cpp -DWINVER=0x0A00 -DUNICODE  -mwindows -municode
start qrcode.exe
pause
