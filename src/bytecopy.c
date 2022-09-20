/*
 * bytecopy
 * GPL-3.0 License
 * 
 * J. Schmitz, 2022
 * 
 */

#define _LARGEFILE64_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <inttypes.h>
#include <string.h>
#include <endian.h>
#include <errno.h>
#include <getopt.h>

#define BUFFER_DEFAULT 1024 * 512
#define FD_IDX_DEFAULT 3

struct ioStatus {
    uint64_t in;
    uint64_t out;
    uint64_t rd;
    uint64_t wr;
    off64_t lenIn;
    off64_t lenOut;
    int fdIn;
    int fdOut;
    int fdIdx;
    char endian;
    void *buffer;
};

void msg(char *fmt, ...) {
    va_list args;
    if (fmt[0] != '+') fprintf(stderr, "bytecopy: ");
    if (fmt[0] != '\0') {
        va_start(args, fmt);
        vfprintf(stderr, fmt + (fmt[0] == '+' ? 1 : 0), args);
        va_end(args);
    }
}

void msgerr(char* s) {
    msg("");
    perror(s);
}

char parseNum(char *str, int64_t *n) {
    char *e;
    if (str[0] == '\0') {
        msg("got empty string for number\n");
        return 1;
    }
    *n = strtoll(str, &e, 0);
    if (e[0] == 'K' && e[1] == '\0') {
        *n *= 1024;
    } else if (e[0] == 'M' && e[1] == '\0') {
        *n *= 1024 * 1024;
    } else if (e[0] == 'G' && e[1] == '\0') {
        *n *= 1024 * 1024 * 1024;
    } else if (e[0] != '\0') {
        msg("error parsing number '%s'\n", str);
        return 1;
    }
    return 0;
}

char readIdx(struct ioStatus *io, off64_t *offset, int64_t idx, int64_t *val) {
    int n = (offset == NULL ? read(io->fdIdx, val, 8) : pread(io->fdIdx, val, 8, *offset + idx * 8));
    if (n == 0) {
        *val = -1;
    } else if (n < 0) {
        msg("error reading index %" PRId64 ": %s\n", idx, strerror(errno));
        return 1;
    } else if (n < 8) {
        msg("index %" PRId64 " could not be fully read\n", idx);
        return 1;
    }
    if (io->endian == 'u') {
        *val = le64toh(*val);
    } else if (io->endian == 'U') {
        *val = be64toh(*val);
    }
    return 0;
}

char readIdxStr(struct ioStatus *io, off64_t *offset, char *strIdx, int64_t *idx, int64_t *val) {
    if (strIdx[0] != '\0' && parseNum(strIdx, idx)) return 1;
    char rslt = readIdx(io, strIdx[0] == '\0' ? NULL : offset, *idx, val);
    if (rslt == 1) msg("at end of input trying to read index %" PRId64 "\n", *idx);
    return rslt;
}

char seek(int fd, off64_t *pos, char *ref) {
    off64_t off = lseek64(fd, *pos, SEEK_SET);
    if (off != *pos) {
        msg("seeking to %" PRId64 " in %s failed: ", *pos, ref);
        if (off == -1) msg("+%s\n", strerror(errno)); else msg("+actual position is %" PRId64 "\n", off);
        return 1;
    }
    *pos = off;
    return 0;
}

char seekEnd(int fd, off64_t *len, char *ref) {
    off64_t cur = lseek64(fd, 0, SEEK_CUR);
    *len = -1;
    if (cur != -1) {
        *len = lseek64(fd, 0, SEEK_END);
        if (*len != -1) {
            if (lseek64(fd, cur, SEEK_SET) == -1) *len = -1;
        }
    }
    if (*len == -1) {
        msg("failed to find end of %s: %s\n", ref, strerror(errno));
        return 1;
    }
    return 0;
}

char parseOffset(char *opt, off64_t *pos, struct ioStatus *io) {
    off64_t *len;
    int64_t offset = 0;
    bool in, add = true;
    
    if (opt[0] == ':' || opt[0] == '?') {
        in = opt[0] == ':';
        if (opt[1] != '\0') {
            if (opt[1] == '-') add = false;
            else if (opt[1] != '+') {
                msg("bad offset sign '%c'.\n", opt[1]);
                return 1;
            }
            if (parseNum(&opt[2], &offset)) return 1;
        }
        
        len = in ? &io->lenIn : &io->lenOut;
        if (*len == -1) if (seekEnd(in ? io->fdIn : io->fdOut, len, in ? "input" : "output")) return 1;
        if (add) *pos = *len + offset; else *pos = *len - offset;
        return 0;
    } else {
        return parseNum(opt, pos);
    }
}

void printFD(int fd, char c) {
    msg("%c (", c);
    switch (fd) {
        case STDIN_FILENO: msg("+stdin"); break;
        case STDOUT_FILENO: msg("+stdout"); break;
        case STDERR_FILENO: msg("+stderr"); break;
        default: msg("+#%d", fd);
    }
    msg("+)\n");
}

void printStats(struct ioStatus *io, char lineEnd) {
    msg("reads/writes: %" PRIu64 "/%" PRIu64 ", in/out: %" PRIu64 "/%" PRIu64 " bytes%c", io->rd, io->wr, io->in, io->out, lineEnd);
}

void printUsage() {
    fprintf(stderr, 
        "Usage: bytecopy [OPTIONS] [START] [END]\n"
        "       bytecopy [OPTIONS] [START] [+LENGTH]\n"
        "       bytecopy [OPTIONS] [+LENGTH]\n"
        "Copy bytes from input, beginning at START up to END\n"
        "or for LENGTH or till the end of input, to output.\n"
        "\n"
        "    -b SIZE     set maximum I/O buffer size (default: 512K)\n"
        "    -B          force buffering, do not write after partial read\n"
        "    -e          write final buffer even if empty\n"
        "    -E          do not consider premature end of input an error\n"
        "    -h          print this help and exit\n"
        "    -i FILE     open FILE for input, instead of reading from standard input (overrides -I)\n"
        "    -I FD       read from a different file descriptor (default: standard input)\n"
        "    -n          print each progress report on a new line\n"
        "    -o FILE     open FILE for output, instead of writing to standard output (overrides -O)\n"
        "    -O FD       write to a different file descriptor (default: standard output)\n"
        "    -q          don't print progress, only status messages to standard error\n"
        "    -Q          print no status, only errors to standard error (implies -q)\n"
        "    -r FILE     open FILE for reading index values (overrides -X)\n"
        "    -s          skip (read and discard) input up to START instead of seeking\n"
        "    -S          synchronize storage (flush to device) after each write (see -y and -Y)\n"
        "    -t          truncate (overwrite) output file (only works with -o)\n"
        "    -T SIZE     truncate or extend length of output file to SIZE, before copying\n"
        "    -u          assume little-endian byte order for index values\n"
        "    -U          assume big-endian byte order for index values\n"
        "    -w POS      seek to POS in output before writing (you will need to use -o or 1<> with this)\n"
        "    -x OFFSET   use OFFSET for reading index values\n"
        "    -X FD       read index values from a different file descriptor (default: 3)\n"
        "    -y          use data synchronized I/O mode on the output file (only works with -o)\n"
        "    -Y          use fully synchronized I/O mode on the output file (only works with -o)\n"
        "    -z          don't seek to end of output file (alias for -w -, default when not using -o)\n"
        "\n"
        "START, END, POS and OFFSET are zero-based byte offsets from the start of a file.\n"
        "Subtracting END form START yields the total number of bytes to copy.\n"
        "LENGTH specifies the number of bytes to copy. This is added to START, if given, to obtain END.\n"
        
        "If END is omitted or '-' is passed, copying will continue until the end of input.\n"
        "If START is omitted or '-' is passed, no seek operation on the input will be performed.\n"
        "Placeholder ':' refers to the length of the input and '?' to the initial length of the output."
        "\n"
        "Values may be specified as decimal or, prefixed with 0 as octal or, prefixed with 0x as hexadecimal.\n"
        "The suffixes K, M, G may be used to multiply a value by 1024, 1024^2 or 1024^3 respectively.\n"
        "\n"
        "Values for START and END may be read from an index, an array of 64-bit integers\n"
        "which are addressed using their zero-based position prefixed with '*'.\n"
        "As a shorthand, the range between two adjacent index values may be specified\n"
        "by passing the zero-based position of the range prefixed with '^' as START.\n"
        "Where the first range is from the beginning of the input to the first index value\n"
        "and the last range is from the last index value to the end of input.\n"
    );
}

int main(int argc, char **argv) {
    int64_t num;
    uint64_t pos = 0, total;
    off64_t offStart = 0, offIdx = 0, offEnd = -1, offWrite = -1;
    bool bSeekStart = true, bStatus = true, bStatusLF = false, bFlushEach = true, bIgnEnd = false, bWrEmpty = false, bSync = false;
    int opt, flagsOut = 0, bufferLen = BUFFER_DEFAULT, bufferPos, rd, wr, rq;
    char *pathIn = NULL, *pathOut = NULL, *pathRes = NULL, *strOutSeek = NULL, *strOutTruncate = NULL, cProg = 0;
    struct ioStatus io = {0, 0, 0, 0, -1, -1, STDIN_FILENO, STDOUT_FILENO, FD_IDX_DEFAULT, 0};
    
    // parse options
    static const struct option long_opts[] = {
        { "help", 0, 0, 'h' },
        { 0, 0, 0, 0 }
    };
    while ((opt = getopt_long(argc, argv, ":b:BeEhi:I:no:O:qQr:sStTuU:w:x:X:yYz", long_opts, NULL)) != -1) {
        if (opt == 'h') {
            printUsage();
            return 1;
        } else if (opt == 'b') {
            // buffer size
            if (parseNum(optarg, &num)) opt = '!';
            else bufferLen = num;
            if (bufferLen < 1) {
                msg("buffer size must be >0\n");
                return 1;
            }
        } else if (opt == 'B') {
            // force buffering
            bFlushEach = false;
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
            io.fdIn = atoi(optarg);
        } else if (opt == 'n') {
            // line-feed after status
            bStatusLF = true;
        } else if (opt == 'o') {
            // output file
            pathOut = optarg;
        } else if (opt == 'O') {
            // output fd
            io.fdOut = atoi(optarg);
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
            bSeekStart = false;
        } else if (opt == 'S') {
            // sync after write
            bSync = true;
        } else if (opt == 't') {
            // truncate output on open
            flagsOut |= O_TRUNC;
        } else if (opt == 'T') {
            // truncate output to length
            strOutTruncate = optarg;
        } else if (opt == 'u' || opt == 'U') {
            // specific endianness;
            io.endian = opt;
        } else if (opt == 'w') {
            // seek in output
            strOutSeek = optarg;
        } else if (opt == 'x') {
            if (parseNum(optarg, &offIdx)) opt = '!';
        } else if (opt == 'X') {
            // output fd
            io.fdIdx = atoi(optarg);
        } else if (opt == 'y') {
            // data synchronized output
            flagsOut |= O_DSYNC;
        } else if (opt == 'Y') {
            // fully synchronized output
            flagsOut |= O_SYNC;
        } else if (opt == 'z') {
            // don't seek in output
            strOutSeek = "-";
        }
        
        if (opt == '!') {
            msg("error at argument %d\n", optind - 1);
            return 1;
        } else if (opt == ':') {
            msg("missing argument to option '%c'\n", optopt);
            return 1;
        } else if (opt == '?') {
            msg("unknown option %s, try -h for help\n", argv[optind - 1]);
            return 1;
        }
    }
    
    if (pathOut == NULL) {
        if (flagsOut) {
            msg("Options -t, -y and -Y can only be used in combination with -o.\n");
            return 1;
        }
    } else {
        flagsOut |= O_WRONLY | O_CREAT;
    }
    
    if (pathRes != NULL) {
        // open resource file for index
        if ((io.fdIdx = open(pathRes, O_RDONLY)) == -1) {
            msg("failed to open resource file: %s: %s\n", pathRes, strerror(errno));
            return 1;
        }
        if (bStatus) msg("* %s\n", pathRes);
    }
    
    // open input
    if (pathIn != NULL) {
        if ((io.fdIn = open(pathIn, O_RDONLY)) == -1) {
            msg("failed to open input file: %s: %s\n", pathIn, strerror(errno));
            return 1;
        }
        if (bStatus) msg("< %s\n", pathIn);
    } else if (bStatus) printFD(io.fdIn, '<');
    
    // open output
    if (pathOut != NULL) {
        if ((io.fdOut = open(pathOut, flagsOut, 0666)) == -1) {
            msg("failed to open output file: %s: %s\n", pathOut, strerror(errno));
            return 1;
        }
        if (bStatus) msg("> %s\n", pathOut);
    } else if (bStatus) printFD(io.fdOut, '>');
    
    // parse range
    if (argc > optind) {
        if (argv[optind][0] == '^') {
            // range from index
            if (parseNum(&argv[optind][1], &num)) return 1;
            // start
            if (num > 0) if (readIdx(&io, &offIdx, num - 1, &offStart)) return 1;
            // end
            if (readIdx(&io, &offIdx, num, &offEnd)) return 1;
        } else {
            // start
            if (argv[optind][0] != '-' && argv[optind][0] != '+') {
                if (argv[optind][0] == '*') {
                    // from index
                    num = 0;
                    if (readIdxStr(&io, &offIdx, &argv[optind][1], &num, &offStart)) return 1;
                } else {
                    // direct or relative to end
                    if (parseOffset(argv[optind], &offStart, &io)) return 1;
                }
            } else {
                bSeekStart = false;
                if (argv[optind][0] == '+') optind--;
            }
            
            // end
            if (argc > ++optind && argv[optind][0] != '-') {
                if (argv[optind][0] == '*') {
                    // from index
                    num = 1;
                    if (readIdxStr(&io, &offIdx, &argv[optind][1], &num, &offEnd)) return 1;
                } else if (argv[optind][0] == '+') {
                    // offset
                    if (parseOffset(&argv[optind][1], &offEnd, &io)) return 1;
                    offEnd += offStart;
                } else {
                    // direct or relative to end
                    if (parseOffset(argv[optind], &offEnd, &io)) return 1;
                }
            }
        }
    } else bSeekStart = false;
    
    if (pathRes != NULL) close(io.fdIdx);
    
    if (argc > (optind + 1)) {
        msg("superfluous argument #%d: %s\n", optind + 1, argv[optind + 1]);
        return 1;
    }
    
    // check range
    if (offEnd >= 0 && offEnd < offStart) {
        msg("invalid range (%" PRId64 "<%" PRId64 ")\n", offEnd, offStart);
        return 1;
    }
    
    // truncate output to length
    if (strOutTruncate != NULL) {
        if (parseOffset(strOutTruncate, &num, &io)) return 1;
        if (ftruncate64(io.fdOut, num) == -1) {
            msg("failed to truncate output to %" PRId64 " bytes: %s\n", num, strerror(errno));
            return 1;
        }
        io.lenOut = num;
    }
    
    // seek output
    if (strOutSeek != NULL) {
        if (strOutSeek[0] != '-') {
            if (parseOffset(strOutSeek, &offWrite, &io)) return 1;
            if (seek(io.fdOut, &offWrite, "output")) return 1;
        }
    } else if (pathOut != NULL && !(flagsOut & O_TRUNC)) {
        offWrite = lseek64(io.fdOut, 0, SEEK_END);
    }
    
    // seek input
    if (bSeekStart) {
        if (seek(io.fdIn, &offStart, "input")) return 1;
        pos = offStart;
    }
    
    // allocate buffer
    if (bStatus) {
        msg("");
        if (offStart > pos) msg("+..");
        if (bSeekStart || offStart > 0) msg("+%" PRId64, offStart);
        msg("+..");
    }
    if (offEnd >= 0) {
        total = offEnd - pos;
        if (total < bufferLen) bufferLen = (total < 1 ? 1 : total);
        if (bStatus) msg("+%" PRId64 " (%" PRId64 " bytes)", offEnd, offEnd - offStart);
    }
    if (bStatus) {
        msg("+ -> ");
        if (offWrite >= 0) msg("+%" PRId64, offWrite);
        msg("+.. (buffer %d", bufferLen);
        if (offEnd >= 0) msg("+ * %" PRIu64, total / bufferLen + (total % bufferLen ? 1 : 0));
        msg("+)\n");
    }
    io.buffer = malloc(bufferLen);
    
    // copy
    bufferPos = 0;
    do {
        rq = (offEnd >= 0 && (pos + bufferLen) > offEnd ? offEnd - pos : bufferLen) - bufferPos;
        rd = read(io.fdIn, io.buffer + bufferPos, rq);
        io.rd++;
        if (rd < 0) break;
        io.in += rd;
        bufferPos += rd;
        if (bFlushEach || rd == 0 || rd == rq) {
            num = pos;
            pos += bufferPos;
            if (pos >= offStart) {
                num = offStart > num ? offStart - num : 0;
                rq = bufferPos - num;
                if (rq > 0 || bWrEmpty) {
                    wr = write(io.fdOut, io.buffer + num, rq);
                    if (bSync && wr != -1 && fsync(io.fdOut) == -1) msgerr("sync failed");
                    io.wr++;
                } else wr = 0;
                if (wr < 0 || wr != rq) break;
                io.out += wr;
            } else wr = rq = 0;
            bufferPos = 0;
        }
        // progress
        if (cProg >= 0) {
            if (!bStatusLF) fprintf(stderr, "\r");
            printStats(&io, ' ');
            if (offEnd >= 0) fprintf(stderr, "(%.1f%%) ", total == 0 ? 100.0 : (float)io.in / total * 100);
            if (bStatusLF) fprintf(stderr, "\n");
            cProg = 1;
        }
    } while (rd && (offEnd < 0 || pos < offEnd));
    
    // final stats
    if (cProg > 0 && !bStatusLF) fprintf(stderr, "\n");
    if (bStatus && cProg < 0) printStats(&io, '\n');
    
    // error handling
    if (rd < 0) {
        msgerr("error reading input");
        return 1;
    } else if (wr < 0) {
        msgerr("error writing output");
        return 1;
    } else if (wr != rq) {
        msg("no more space to write output (%d<%d)\n", wr, rq);
        return 1;
    }
    
    if (pathIn != NULL) close(io.fdIn);
    if (pathOut != NULL) close(io.fdOut);
    
    if (offEnd >= 0 && !bIgnEnd && io.out != (offEnd - offStart)) {
        msg("premature end of input (%" PRIu64 "<%" PRId64 ")\n", io.out, offEnd - offStart);
        return 1;
    }
    
    return 0;
}
