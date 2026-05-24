# clip-parser
A CLI-based parser for Clip Studio Paint files (.clip), written in C.

## Building and running
### Linux
``` sh
$ cc -std=c89 -Wall -Werror clip.c -o clip
$ ./clip FILENAME
```
### Windows
```bat
cl clip.c /Fo:clip.obj /Fe:clip.exe /link ws2_32.lib
clip.exe FILENAME
```
