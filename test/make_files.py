import zipfile

def write(file, data, encoding='utf-8'):
    if isinstance(data, bytes):
        with open(file, 'wb') as f:
            f.write(data)
    else:
        with open(file, 'w', encoding=encoding) as f:
            f.write(data)

def zip(file, data, compress_type=None):
    with zipfile.ZipFile(file, 'w') as f:
        f.writestr('example.txt', data, compress_type)

MB = 0x100000
text = (open.__doc__ + '\n\n').encode() * 1000

for en in ['utf-8', 'utf-8-sig', 'utf-16', 'gbk', 'big5']:
    write(f'en-{en:-<9}.txt', f'你好, {en:-<9}!', en)

for en in ['utf-16-be', 'utf-16-le']:
    write(f'en-{en}.txt', f'\ufeff你好, {en}!', en)

write('size-0MB.txt', '')
write('size-1MB.txt', text[:MB])
write('size-2MB.txt', text[:MB*2])

write('test.txt', '1/1:test.txt:VVVV,')
write('example.txt', '1/2:example.txt:SGVsbG8g,2/2:V29ybGQh,')

zip('zip-4KB.zip', text, zipfile.ZIP_LZMA)
zip('zip-1MB.zip', text[:MB-120])
zip('zip-2MB.zip', text[:MB*2])
