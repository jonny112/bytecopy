# bytecopy
Copy arbitrary ranges of bytes from one file to another or between different locations in the same file.
Extracting a segment to a new file is possible as well as updating a segment within an existing file.

## Usage
```sh
bytecopy [OPTIONS] [START] [END|+LENGTH]
```
See [manpage](doc/bytecopy.man.txt) for full documentation or invoke with -h for options summary.

## Installation

The program should compile in any POSIX compatible gcc/glibc environment with the included Makefile.
It merely makes use of standard functions, no additional libraries are needed.

## Examples

Exctract a range of bytes:
```sh
bytecopy 1000 1300 < source.file > new.file
```
```sh
bytecopy -i source.file 1000 1300 > new.file
```
```sh
bytecopy -i source.file 1000 +300 > new.file
```

Update contents of a file:
```sh
echo "Hello World!" > some.file
echo "Earth" | bytecopy -o some.file -w 6 - 5
```

## Bugs

Feel free to file issues and sugestions at:
https://github.com/jonny112/bytecopy/issues
