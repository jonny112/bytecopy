# bytecopy
Copy byte segments of arbitrary size from one file or device to another or between different locations in the same file.

## Usage
```sh
bytecopy [OPTION]... START [END]
bytecopy [OPTION]... START [+LENGTH]
bytecopy [OPTION]... [+LENGTH]
```
See [manpage](doc/bytecopy.man.txt) for full documentation or invoke with -h for options summary.

## Examples

Extract a range of bytes:
```sh
bytecopy 1000 1300 < source.file > new.file
```
```sh
bytecopy -i source.file 1000 +300 > new.file
```
```sh
bytecopy -i source.file -to new.file 1000 +300
```

Update contents of a file:
```sh
echo "Hello World!" > some.file
echo "Earth" | bytecopy -o some.file -w 6 +5
```

Copy from and to devices, monitoring progress:
```sh
bytecopy -i /dev/sdX +: | xz > disk.img.xz
```
```sh
xzcat disk.img.xz | bytecopy -Eyzo /dev/sdX +?
```

## Installation

This program should compile in any POSIX compatible gcc/glibc environment using the included Makefile. It merely utilizes standard functions, no additional libraries are needed.

## Bugs

Feel free to file issues and suggestions at:
https://github.com/jonny112/bytecopy/issues
