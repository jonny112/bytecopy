/*
 * bytecopy
 * GPL-3.0 License
 * 
 * J. Schmitz, 2021
 * 
 */

#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>

#define BUFFER_DEFAULT 1024 * 512

char parseNum(char *str, uint64_t *n) {
    char *e;
    if (str[0] == '\0') {
        fprintf(stderr, "bytecopy: got empty string for number\n");
        return 1;
    }
    *n = strtoull(str, &e, 0);
    if (e[0] == 'K' && e[1] == '\0') {
        *n *= 1024;
    } else if (e[0] == 'M' && e[1] == '\0') {
        *n *= 1024 * 1024;
    } else if (e[0] == 'G' && e[1] == '\0') {
        *n *= 1024 * 1024 * 1024;
    } else if (e[0] != '\0') {
        fprintf(stderr, "bytecopy: error parsing number '%s'\n", str);
        return 1;
    }
    return 0;
}

char readIdx(int fd, uint64_t *offset, uint64_t idx, uint64_t *val) {
    int n = (offset == NULL ? read(fd, val, 8) : pread(fd, val, 8, *offset + idx * 8));
    if (n < 0) {
        fprintf(stderr, "bytecopy: error reading index %" PRIu64 ": %s\n", idx, strerror(errno));
    } else if (n == 0) {
        return 1;
    } else if (n < 8) {
        fprintf(stderr, "bytecopy: index %" PRIu64 " could not be fully read\n", idx);
    } else {
        return 0;
    }
    return 2;
}

char readIdxStr(int fd, uint64_t *offset, char *strIdx, uint64_t *idx, uint64_t *val) {
    if (strIdx[0] != '\0' && parseNum(strIdx, idx)) return 1;
    char rslt = readIdx(fd, strIdx[0] == '\0' ? NULL : offset, *idx, val);
    if (rslt == 1) fprintf(stderr, "bytecopy: at end of input trying to read index %" PRIu64 "\n", *idx);
    return rslt;
}

char seek(int fd, uint64_t *pos, char *ref) {
    uint64_t n = lseek64(fd, *pos, SEEK_SET);
    if (n != *pos) {
        fprintf(stderr, "bytecopy: seeking to %" PRIu64 " in %s failed: ", *pos, ref);
        if (n == -1) fprintf(stderr, "%s\n", strerror(errno)); else fprintf(stderr, "position is %" PRIu64 "\n", n);
        return 1;
    }
    *pos = n;
    return 0;
}

struct ioStats {
    uint64_t in;
    uint64_t out;
    uint64_t rd;
    uint64_t wr;
};

void printStats(struct ioStats *io, char lineEnd) {
    fprintf(stderr, "bytecopy: reads/writes: %" PRIu64 "/%" PRIu64 ", in/out: %" PRIu64 "/%" PRIu64 " bytes%c", io->rd, io->wr, io->in, io->out, lineEnd);
}

int main(int argc, char **argv) {
    uint8_t *buffer;
    uint64_t n, pos = 0, posStart = 0, total, posIdx = 0, posWrite, posEnd;
    bool bEnd = false, bInSeek = true, bStatus = true, bFlush = true, bIgnEnd = false, bWrEmpty = false;
    int opt, fdIn = 0, fdOut = 1, fdIdx = 3, rd, wr, rq, bufferLen = BUFFER_DEFAULT, bufferPos;
    char *pathIn = NULL, *pathOut = NULL, *pathRes = NULL, cOutSeek = 0, cProg = 0;
    struct ioStats io = {0, 0, 0, 0};
    
    // parse options
    while ((opt = getopt(argc, argv, ":b:BeEhi:I:o:O:qQr:sw:x:X:")) != -1) {
        if (opt == 'h') {
            fprintf(stderr, 
                "Usage: bytecopy [OPTIONS] [START] [END|+LENGTH]\n"
                "Copy bytes from input, beginning at START up to END\n"
                "or for LENGTH or till the end of input, to output.\n"
                "\n"
                "    -b SIZE     set I/O buffer size (default: 512K)\n"
                "    -B          force buffering, do not write output after each read\n"
                "    -e          write final buffer even if empty\n"
                "    -E          do not consider premature end of input an error\n"
                "    -h          print this help and exit\n"
                "    -i FILE     open FILE for input instead of reading from standard input (overrides -I)\n"
                "    -I FD       read from a different file descriptor (default: standard input)\n"
                "    -o FILE     open FILE for output instead of writing to standard output (overrides -O)\n"
                "    -O FD       write to a different file descriptor (default: standard output)\n"
                "    -q          don't print progress to standard error\n"
                "    -Q          print no status, only errors to standard error\n"
                "    -r FILE     open FILE for reading the index (overrides -X)\n"
                "    -s          skip (read/discard) input up to START instead of seeking\n"
                "    -w POS      seek to POS in output before writing (you will want to use -o or 1<> with this)\n"
                "    -x OFFSET   use OFFSET when reading index values\n"
                "    -X FD       read index values from a different file descriptor (default: 3)\n"
                "\n"
                "START, END, POS and OFFSET are zero-based byte offsets from the start of a file.\n"
                "LENGTH is a byte count added to START to obtain END.\n"
                "Subtracting END form START yields the total number of bytes to copy.\n"
                "If END is omitted or '-' is passed, copying will continue until the end of input.\n"
                "If START is omitted or '-' is passed, no seek operation on the input will be performed.\n"
                "\n"
                "Values may be specified as decimal or, prefixed with 0 as octal or, prefixed with 0x as hexadecimal.\n"
                "The suffixes K, M, G may be used to multiply a value by 1024, 1024^2 or 1024^3 respectively.\n"
                "\n"
                "Values for START and END may be read from an index consisting of unsigned 64-bit integers\n"
                "which are addressed using their zero-based position prefixed with '*'.\n"
                "As a shorthand, the range between two adjacent index values may be specified\n"
                "by passing the zero-based position of the range prefixed with '^' to START.\n"
                "Where the first range is from the beginning of the input to the first index value\n"
                "and the last range is from the last index value to the end of input.\n"
            );
            return 1;
        } else if (opt == 'b') {
            // buffer size
            if (parseNum(optarg, &n)) opt = '!';
            else bufferLen = n;
            if (bufferLen < 1) {
                fprintf(stderr, "bytecopy: buffer size must be >0\n");
                return 1;
            }
        } else if (opt == 'B') {
            // force buffering
            bFlush = false;
        } else if (opt == 'e') {
            // write empty buffer
            bWrEmpty = true;
        } else if (opt == 'E') {
            // pass premature end
            bIgnEnd = true;
        } else if (opt == 'i') {
            // input file
            pathIn = optarg;
        } else if (opt == 'I') {
            // input fd
            fdIn = atoi(optarg);
        } else if (opt == 'o') {
            // output file
            pathOut = optarg;
        } else if (opt == 'O') {
            // output fd
            fdOut = atoi(optarg);
        } else if (opt == 'q') {
            // no progress
            cProg = -1;
        } else if (opt == 'Q') {
            // errors only
            cProg = -1;
            bStatus = false;
        } else if (opt == 'r') {
            // resource file
            pathRes = optarg;
        } else if (opt == 's') {
            // don't seek in input
            bInSeek = false;
        } else if (opt == 'w') {
            // seek in output
            if (optarg[0] == '-') {
                // don't seek to append
                cOutSeek = -1;
            } else {
                if (parseNum(optarg, &posWrite)) opt = '!';
                cOutSeek = 1;
            }
        } else if (opt == 'x') {
            if (parseNum(optarg, &posIdx)) opt = '!';
        } else if (opt == 'X') {
            // output fd
            fdIdx = atoi(optarg);
        }
        
        if (opt == '!') {
            fprintf(stderr, "bytecopy: error at argument %d\n", optind - 1);
            return 1;
        } else if (opt == ':') {
            fprintf(stderr, "bytecopy: missing argument to option '%c'\n", optopt);
            return 1;
        } else if (opt == '?') {
            fprintf(stderr, "bytecopy: unknown option '%c', try -h for help\n", optopt);
            return 1;
        }
    }
    
    if (pathRes != NULL) {
        // open resource file for index
        if ((fdIdx = open(pathRes, O_RDONLY)) == -1) {
            fprintf(stderr, "bytecopy: failed to open resource file: %s: %s\n", pathRes, strerror(errno));
            return 1;
        }
    }
    
    // parse range
    if (argc > optind) {
        if (argv[optind][0] == '^') {
            // range from index
            if (parseNum(&argv[optind][1], &n)) return 1;
            // start
            if (n > 0) if (readIdx(fdIdx, &posIdx, n - 1, &posStart)) return 1;
            // end
            switch (readIdx(fdIdx, &posIdx, n, &posEnd)) {
                case 0:
                    bEnd = true;
                    break;
                case 1:
                    // last/unbound
                    break;
                default:
                    return 1;
            }
        } else {
            // start
            if (argv[optind][0] != '-') {
                if (argv[optind][0] == '*') {
                    // from index
                    n = 0;
                    if (readIdxStr(fdIdx, &posIdx, &argv[optind][1], &n, &posStart)) return 1;
                } else {
                    // direct
                    if (parseNum(argv[optind], &posStart)) return 1;
                }
            } else bInSeek = false;
            
            // end
            if (argc > ++optind && argv[optind][0] != '-') {
                if (argv[optind][0] == '*') {
                    // from index
                    n = 1;
                    if (readIdxStr(fdIdx, &posIdx, &argv[optind][1], &n, &posEnd)) return 1;
                } else if (argv[optind][0] == '+') {
                    // offset
                    if (parseNum(&argv[optind][1], &posEnd)) return 1;
                    posEnd += posStart;
                } else {
                    // direct
                    if (parseNum(argv[optind], &posEnd)) return 1;
                }
                bEnd = true;
            }
        }
    } else bInSeek = false;
    
    if (pathRes != NULL) close(fdIdx);
    
    if (argc > (optind + 1)) {
        fprintf(stderr, "bytecopy: superfluous argument %d: %s\n", optind, argv[optind + 1]);
        return 1;
    }
    
    // check range
    if (bEnd && posEnd < posStart) {
        fprintf(stderr, "bytecopy: invalid range (%" PRIu64 "<%" PRIu64 ")\n", posEnd, posStart);
        return 1;
    }
    
    // prep input
    if (pathIn != NULL) {
        if ((fdIn = open(pathIn, O_RDONLY)) == -1) {
            fprintf(stderr, "bytecopy: failed to open input file: %s: %s\n", pathIn, strerror(errno));
            return 1;
        }
    }
    if (bInSeek) {
        if (seek(fdIn, &posStart, "input")) return 1;
        pos = posStart;
    }
    
    // prep output
    if (pathOut != NULL) {
        if ((fdOut = open(pathOut, O_WRONLY | O_CREAT, 0666)) == -1) {
            fprintf(stderr, "bytecopy: failed to open output file: %s: %s\n", pathOut, strerror(errno));
            return 1;
        }
    }
    if (cOutSeek == 1) {
        if (seek(fdOut, &posWrite, "output")) return 1;
    } else if (pathOut != NULL && cOutSeek != -1) {
        posWrite = lseek64(fdOut, 0, SEEK_END);
    }
    
    // allocate buffer
    if (bStatus) {
        fprintf(stderr, "bytecopy: ");
        if (posStart > pos) fprintf(stderr, "..");
        if (bInSeek || posStart > 0) fprintf(stderr, "%" PRIu64, posStart);
        fprintf(stderr, "..");
    }
    if (bEnd) {
        total = posEnd - pos;
        if (total < bufferLen) bufferLen = (total < 1 ? 1 : total);
        if (bStatus) fprintf(stderr, "%" PRIu64 " (%" PRIu64 " bytes)", posEnd, posEnd - posStart);
    }
    if (bStatus) {
        fprintf(stderr, " -> ");
        if (cOutSeek == 1 || (pathOut != NULL && cOutSeek != -1)) fprintf(stderr, "%" PRIu64, posWrite);
        fprintf(stderr, ".. (buffer %d", bufferLen);
        if (bEnd) fprintf(stderr, " * %" PRIu64, total / bufferLen + (total % bufferLen ? 1 : 0));
        fprintf(stderr, ")\n");
    }
    buffer = malloc(bufferLen);
    
    // copy
    bufferPos = 0;
    do {
        rq = (bEnd && (pos + bufferLen) > posEnd ? posEnd - pos : bufferLen) - bufferPos;
        rd = read(fdIn, buffer + bufferPos, rq);
        io.rd++;
        if (rd < 0) break;
        io.in += rd;
        bufferPos += rd;
        if (bFlush || rd == 0 || rd == rq) {
            n = pos;
            pos += bufferPos;
            if (pos >= posStart) {
                n = posStart > n ? posStart - n : 0;
                rq = bufferPos - n;
                if (rq > 0 || bWrEmpty) {
                    wr = write(fdOut, buffer + n, rq);
                    io.wr++;
                } else wr = 0;
                if (wr < 0 || wr != rq) break;
                io.out += wr;
            } else wr = rq = 0;
            bufferPos = 0;
        }
        // progress
        if (cProg >= 0) {
            fprintf(stderr, "\r");
            printStats(&io, ' ');
            if (bEnd) fprintf(stderr, "(%.1f%%) ", total == 0 ? 100.0 : (float)io.in / total * 100);
            cProg = 1;
        }
    } while (rd && (!bEnd || pos < posEnd));
    
    // final stats
    if (cProg > 0) fprintf(stderr, "\n");
    if (bStatus && cProg < 0) printStats(&io, '\n');
    
    // error handling
    if (rd < 0) {
        perror("bytecopy: error reading input");
        return 1;
    } else if (wr < 0) {
        perror("bytecopy: error writing output");
        return 1;
    } else if (wr != rq) {
        fprintf(stderr, "bytecopy: no more space to write output (%d<%d)\n", wr, rq);
        return 1;
    }
    
    if (pathIn != NULL) close(fdIn);
    if (pathOut != NULL) close(fdOut);
    
    if (bEnd && !bIgnEnd && io.out != (posEnd - posStart)) {
        fprintf(stderr, "bytecopy: premature end of input (%" PRIu64 "<%" PRIu64 ")\n", io.out, posEnd - posStart);
        return 1;
    }
    
    return 0;
}
