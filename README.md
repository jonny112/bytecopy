# bytecopy
Copy byte arrays of arbitrary size from one file or device to another or between different locations in the same file. Extract a file segment to a new file or update a segment within an existing file.

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

## Installation

This program should compile in any POSIX compatible gcc/glibc environment using the included Makefile. It merely utilizes standard functions, no additional libraries are needed.

## Bugs

Feel free to file issues and suggestions at:
https://github.com/jonny112/bytecopy/issues
