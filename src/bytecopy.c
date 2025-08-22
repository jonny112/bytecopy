/*
 * bytecopy
 * GPL-3.0 License
 * 
 * J. Schmitz, 2009-2025
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
#include <locale.h>

#define BUFFER_DEFAULT 1024 * 512
#define FD_IDX_DEFAULT 3

struct ioStatus {
    uint64_t in;
    uint64_t out;
    uint64_t rd;
    uint64_t wr;
    off64_t lenIn;
    off64_t lenOut;
    off64_t total;
    int64_t offsetIn;
    int fdIn;
    int fdOut;
    int fdIdx;
    char endian;
    char prog;
    void *buffer;
};

struct optRef {
    int idx;
    char *str;
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
        if (*val == -1) return 0;
        msg("entry %" PRId64 " beyond end of index\n", idx);
        return 1;
    } else if (n < 0) {
        msg("error reading index entry %" PRId64 ": %s\n", idx, strerror(errno));
        return 1;
    } else if (n < 8) {
        msg("index entry %" PRId64 " could not be fully read\n", idx);
        return 1;
    }
    if (io->endian == 'u') {
        *val = le64toh(*val);
    } else if (io->endian == 'U') {
        *val = be64toh(*val);
    }
    *val += io->offsetIn;
    return 0;
}

char readIdxStr(struct ioStatus *io, off64_t *offset, char *strIdx, int64_t *idx, int64_t *val) {
    if (strIdx[0] != '\0' && parseNum(strIdx, idx)) return 1;
    *val = 0;
    return readIdx(io, strIdx[0] == '\0' ? NULL : offset, *idx, val);
}

char seek(int fd, off64_t *pos, char *ref) {
    off64_t off = lseek64(fd, *pos, SEEK_SET);
    if (off != *pos) {
        msg("seeking to %" PRId64 " in %s failed: ", *pos, ref);
        if (off == -1) {
            msg("+%s\n", strerror(errno));
        } else {
            msg("+actual position is %" PRId64 "\n", off);
        }
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
    
    if (opt[0] == 'i' || opt[0] == 'o') {
        in = opt[0] == 'i';
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

void printFD(int fd) {
    msg("+(");
    switch (fd) {
        case STDIN_FILENO: msg("+stdin"); break;
        case STDOUT_FILENO: msg("+stdout"); break;
        case STDERR_FILENO: msg("+stderr"); break;
        default: msg("+#%d", fd);
    }
    msg("+)\n");
}

void printStats(struct ioStatus *io, char lineEnd) {
    msg("reads/writes: %" PRIu64 "/%" PRIu64 ", bytes: %'" PRIu64 " in, %'" PRIu64 " out", io->rd, io->wr, io->in, io->out);
    if (io->total != -1) {
        if (io->prog > 1) fprintf(stderr, ", %'" PRId64 " total", io->total);
        fprintf(stderr, " (%.1f%%)", io->total == 0 ? 100.0 : (int)((float)io->in / io->total * 1000) / 10.0);
    }
    fprintf(stderr, "%c", lineEnd);
    if (io->prog < 1) io->prog = 1;
}

bool strIsChar(char *s, char c) {
    return s[0] == c && s[1] == '\0';
}

int errArg(int n) {
    msg("illegal argument #%d\n", n);
    return EXIT_FAILURE;
}

void printUsage() {
    fprintf(stderr,
        "Usage: bytecopy [OPTION]... START [END]\n"
        "       bytecopy [OPTION]... START [+LENGTH]\n"
        "       bytecopy [OPTION]... [+LENGTH [SLICE]]\n"
        "Copy bytes from input, beginning at START up to END\n"
        "or for LENGTH or till the end of input, to output.\n"
        "\n"
        "    -a OFFSET   adjust buffer size for initial cycle by OFFSET (number or r: input, w: output)\n"
        "    -b SIZE     buffer up to SIZE bytes per read/write cycle (default: 512K)\n"
        "    -B          force buffering, do not write after partial read\n"
        "    -e          write final buffer even if empty\n"
        "    -E          do not consider premature end of input an error\n"
        "    -h          print this help and exit\n"
        "    -i FILE     open FILE for input, instead of reading from standard input (overrides -I)\n"
        "    -I FD       read from the specified file descriptor (default: standard input)\n"
        "    -n          print each progress report on a new line\n"
        "    -o FILE     open FILE for output, instead of writing to standard output (overrides -O)\n"
        "    -O FD       write to the specified file descriptor (default: standard output)\n"
        "    -p          print progress but no status messages (implies -Q, overrides -q)\n"
        "    -P POS      use POS as offset for reading index values\n"
        "    -q          don't print progress, only status messages to standard error\n"
        "    -Q          print no status, only errors to standard error (implies -q unless -p)\n"
        "    -s          skip input (read and discard) up to START instead of seeking\n"
        "    -S          synchronize storage (flush to device) after each write (see -y and -Y)\n"
        "    -t          truncate (overwrite) output file (only works with -o)\n"
        "    -T SIZE     truncate or extend length of output file to SIZE, before copying\n"
        "    -u          assume little-endian byte order for index values\n"
        "    -U          assume big-endian byte order for index values\n"
        "    -w POS      seek to POS in output before writing (you will need to use -o or 1<> with this)\n"
        "    -x FILE     open FILE for reading index values (overrides -X)\n"
        "    -X FD       read index values from the specified file descriptor (default: 3)\n"
        "    -y          use data synchronized write mode (only works with -o)\n"
        "    -Y          use fully synchronized write mode (only works with -o)\n"
        "    -z          don't seek to end of output file (alias for -w '-', default when not using -o)\n"
        "    -Z OFFSET   add OFFSET (may be nagative) to index values and SLICE positions\n"
        "\n"
        "START, END and POS are zero-based byte offsets from the start of a file.\n"
        "Subtracting END form START yields the total number of bytes to copy.\n"
        "LENGTH specifies the number of bytes to copy. It is added to START to obtain END.\n"
        "SLICE calculates START as multiple of LENGTH. This copies the n-th slice of LENGTH size.\n"
        
        "If END is omitted or '-' is passed, copying will continue until the end of input.\n"
        "If START is omitted or '-' is passed, no seek operation on the input will be performed.\n"
        "Placeholder 'i' refers to the length of the input and 'o' to the initial length of the output."
        "\n"
        "Values may be specified as decimal or, prefixed with 0 as octal or, prefixed with 0x as hexadecimal.\n"
        "The suffixes K, M, G may be used to multiply a value by 1024, 1024^2 or 1024^3 respectively.\n"
        "\n"
        "Values for START and END may be read from an index, an array of 64-bit integers\n"
        "which are addressed using their zero-based position prefixed with ':' or '*'.\n"
        "As a shorthand, the range between two adjacent index values may be specified\n"
        "by passing the zero-based position of the range prefixed with '^' as START,\n"
        "where the first range is from the beginning of the input to the first index value\n"
        "and the last range is from the last index value to the end of input.\n"
        "\n"
        "See man page bytecopy(1) for more details.\n"
    );
}

int main(int argc, char **argv) {
    int64_t num;
    off64_t pos = 0, offStart = 0, offIdx = 0, offEnd = -1, offWrite = -1;
    bool bStart = false, bLen = false, bSeekStart = true, bStatus = true, bProgLF = false, bFlushEach = true, bIgnEnd = false, bWrEmpty = false, bSync = false;
    int opt, flagsOut = 0, bufferLen = BUFFER_DEFAULT, blockSize = 0, bufferPos, rd, wr, rq;
    char *pathIn = NULL, *pathOut = NULL, *pathRes = NULL, *strAlign = NULL;
    struct ioStatus io = {0, 0, 0, 0, -1, -1, -1, 0, STDIN_FILENO, STDOUT_FILENO, FD_IDX_DEFAULT, 0, 0};
    struct optRef optOutSeek = {0, NULL}, optOutTruncate = {0, NULL};

    setlocale(LC_ALL, "");

    // parse options
    static const struct option long_opts[] = {
        { "help", 0, 0, 'h' },
        { 0, 0, 0, 0 }
    };
    while ((opt = getopt_long(argc, argv, ":a:b:BeEhi:I:no:O:pP:qQsStT:uUw:x:X:yYzZ:", long_opts, NULL)) != -1) {
        if (opt == 'h') {
            if (argc > 2) {
                msg("-h/--help cannot be combined with other options\n");
                return EXIT_FAILURE;
            }
            printUsage();
            return EXIT_SUCCESS;
        } else if (opt == 'a') {
            // align buffer
            strAlign = optarg;
            if (optarg[0] != 'r' && optarg[0] != 'w') {
                if (parseNum(optarg, &num)) {
                    opt = '!';
                } else {
                    blockSize = num;
                }
            }
        } else if (opt == 'b') {
            // buffer size
            if (parseNum(optarg, &num)) {
                opt = '!';
            } else {
                bufferLen = num;
            }
            if (bufferLen < 1) {
                msg("buffer size must be >0\n");
                opt = '!';
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
            // line-feed after progress
            bProgLF = true;
        } else if (opt == 'o') {
            // output file
            pathOut = optarg;
        } else if (opt == 'O') {
            // output fd
            io.fdOut = atoi(optarg);
        } else if (opt == 'p') {
            // progress only
            bStatus = false;
            io.prog = 2;
        } else if (opt == 'P') {
            // index offset
            if (parseNum(optarg, &offIdx)) opt = '!';
        } else if (opt == 'q' && io.prog < 1) {
            // no progress
            io.prog = -1;
        } else if (opt == 'Q' && io.prog < 1) {
            // errors only
            io.prog = -1;
            bStatus = false;
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
            optOutTruncate.idx = optind - 1;
            optOutTruncate.str = optarg;
        } else if (opt == 'u' || opt == 'U') {
            // specific endianness;
            io.endian = opt;
        } else if (opt == 'w') {
            // seek in output
            if (strIsChar(optarg, '-')) {
                // explicitly don't
                optOutSeek.idx = -1;
            } else {
                optOutSeek.idx = optind - 1;
                optOutSeek.str = optarg;
            }
        } else if (opt == 'x') {
            // index file
            pathRes = optarg;
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
            optOutSeek.idx = -1;
        } else if (opt == 'Z') {
            // index values offset
            if (parseNum(optarg, &io.offsetIn)) opt = '!';
        }

        if (opt == '!') {
            return errArg(optind - 1);
        } else if (opt == ':') {
            msg("missing argument to option -%c\n", optopt);
            return EXIT_FAILURE;
        } else if (opt == '?') {
            if (optopt) {
                msg("unknown option -%c, try -h for help\n", optopt);
            } else {
                msg("bad option %s, try -h for help\n", argv[optind - 1]);
            }
            return EXIT_FAILURE;
        }
    }

    if (pathOut == NULL) {
        if (flagsOut) {
            msg("Options -t, -y and -Y can only be used in combination with -o.\n");
            return EXIT_FAILURE;
        }
    } else flagsOut |= O_WRONLY | O_CREAT;

    if (pathRes != NULL) {
        // open index file
        if ((io.fdIdx = open(pathRes, O_RDONLY)) == -1) {
            msg("failed to open index file: %s: %s\n", pathRes, strerror(errno));
            return EXIT_FAILURE;
        }
        if (bStatus) msg("index: %s\n", pathRes);
    }

    // open input
    if (pathIn != NULL && (io.fdIn = open(pathIn, O_RDONLY)) == -1) {
        msg("failed to open input file: %s: %s\n", pathIn, strerror(errno));
        return EXIT_FAILURE;
    }
    if (bStatus) {
        msg("reading: ");
        if (pathIn == NULL) {
            printFD(io.fdIn);
        } else {
            msg("+%s\n", pathIn);
        }
    }

    // open output
    if (pathOut != NULL && (io.fdOut = open(pathOut, flagsOut, 0666)) == -1) {
        msg("failed to open output file: %s: %s\n", pathOut, strerror(errno));
        return EXIT_FAILURE;
    }
    if (bStatus) {
        msg("writing: ");
        if (pathOut == NULL) {
            printFD(io.fdOut);
        } else {
            msg("+%s\n", pathOut);
        }
    }

    // parse range
    if (argc > optind) {
        if (argv[optind][0] == '^') {
            // range from index
            if (parseNum(&argv[optind][1], &num)) return errArg(optind);
            // start
            if (num > 0) if (readIdx(&io, &offIdx, num - 1, &offStart)) return errArg(optind);
            // end
            if (readIdx(&io, &offIdx, num, &offEnd)) return errArg(optind);
        } else {
            num = 0;

            // start
            if (argv[optind][0] == '+') {
                optind--;
            } else {
                bStart = true;
                if (!strIsChar(argv[optind], '-')) {
                    if (argv[optind][0] == '*' || argv[optind][0] == ':') {
                        // from index
                        if (readIdxStr(&io, &offIdx, &argv[optind][1], &num, &offStart)) return errArg(optind);
                        if (argv[optind][1] == '\0') num = 1;
                    } else {
                        // direct or relative to end
                        if (parseOffset(argv[optind], &offStart, &io)) return errArg(optind);
                    }
                } else bSeekStart = false;
            }

            // end
            if (argc > ++optind && !strIsChar(argv[optind], '-')) {
                if (argv[optind][0] == '+') {
                    // offset
                    if (parseOffset(&argv[optind][1], &offEnd, &io)) return errArg(optind);
                    offEnd += offStart;
                    bLen = !bStart;
                } else {
                    if (argv[optind][0] == '*' || argv[optind][0] == ':') {
                        // from index
                        if (readIdxStr(&io, &offIdx, &argv[optind][1], &num, &offEnd)) return errArg(optind);
                    } else {
                        // direct or relative to end
                        if (parseOffset(argv[optind], &offEnd, &io)) return errArg(optind);
                    }
                }
            }

            // slice
            if (bLen && argc > ++optind) {
                if (parseNum(argv[optind], &num)) return errArg(optind);
                offStart = num * offEnd + io.offsetIn;
                offEnd += offStart;
            } else bSeekStart &= bStart;
        }
    } else bSeekStart = false;

    if (pathRes != NULL) close(io.fdIdx);

    if (argc > (optind + 1)) {
        msg("superfluous argument #%d: %s\n", optind + 1, argv[optind + 1]);
        return EXIT_FAILURE;
    }

    // check range
    if (offEnd >= 0 && offEnd < offStart) {
        msg("invalid range (%" PRId64 "<%" PRId64 ")\n", offEnd, offStart);
        return EXIT_FAILURE;
    }

    // seek input
    if (bSeekStart) {
        if (seek(io.fdIn, &offStart, "input")) return 1;
        pos = offStart;
    }

    // truncate output
    if (optOutTruncate.idx != 0) {
        if (parseOffset(optOutTruncate.str, &num, &io)) return errArg(optOutTruncate.idx);
        if (ftruncate64(io.fdOut, num) == -1) {
            msg("failed to truncate output to %'" PRId64 " bytes: %s\n", num, strerror(errno));
            return EXIT_FAILURE;
        }
        if (bStatus) msg("output file truncated to %'" PRId64 " bytes\n", num);
        io.lenOut = num;
    }

    // seek output
    if (optOutSeek.idx != 0) {
        if (optOutSeek.idx > 0) {
            if (parseOffset(optOutSeek.str, &offWrite, &io)) return errArg(optOutSeek.idx);
            if (seek(io.fdOut, &offWrite, "output")) return EXIT_FAILURE;
        }
    } else if (pathOut != NULL && !(flagsOut & O_TRUNC)) {
        offWrite = lseek64(io.fdOut, 0, SEEK_END);
    }

    // buffer allignment
    if (strAlign != NULL) {
        if (strAlign[0] == 'r') blockSize = -offStart;
        else if (strAlign[0] == 'w' && offWrite != -1) blockSize = -offWrite;
        blockSize = ((blockSize % bufferLen) + bufferLen) % bufferLen;
    }
    if (blockSize == 0) blockSize = bufferLen;

    // allocate buffer
    if (bStatus) {
        msg("range: ");
        if (bSeekStart || offStart > 0) {
            if (offStart > pos) msg("+(skipping)..");
            msg("+%" PRId64, offStart);
        } else msg("+(initial)");
        msg("+..");
    }
    if (offEnd >= 0) {
        io.total = offEnd - pos;
        if (io.total < bufferLen) {
            bufferLen = (io.total < 1 ? 1 : io.total);
            if (blockSize > bufferLen) blockSize = bufferLen;
        }
        if (bStatus) msg("+%" PRId64 " (%'" PRId64 " bytes)", offEnd, offEnd - offStart);
    } else if (bStatus) msg("+(unbounded)");
    if (bStatus) {
        msg("+ -> ");
        if (offWrite >= 0) msg("+%" PRId64, offWrite); else msg(flagsOut & O_TRUNC ? "+(truncated)" : "+(default)");
        msg("+..");
        if (offWrite >= 0 && offEnd >= 0) msg("+%" PRId64, offWrite + (offEnd - offStart));
        msg("+ at ");
        if (blockSize != bufferLen) msg("+%d + ", blockSize);
        msg("+%'d bytes", bufferLen);
        if (io.total != -1) msg("+ * %" PRId64, ((io.total - (blockSize < bufferLen ? blockSize : 0)) + bufferLen - 1) / bufferLen);
        msg("+\n");
    }
    io.buffer = malloc(bufferLen);

    // copy
    bufferPos = 0;
    if (io.prog > 1) printStats(&io, bProgLF ? '\n' : ' ');
    do {
        rq = (offEnd >= 0 && (pos + bufferLen) > offEnd ? offEnd - pos : blockSize) - bufferPos;
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
            blockSize = bufferLen;
        }
        // progress
        if (io.prog >= 0) {
            if (!bProgLF) fprintf(stderr, "\r");
            printStats(&io, bProgLF ? '\n' : ' ');
        }
    } while (rd && (offEnd < 0 || pos < offEnd));

    // final stats
    if (io.prog > 0 && !bProgLF) fprintf(stderr, "\n");
    if (bStatus && io.prog < 0) printStats(&io, '\n');

    // error handling
    if (rd < 0) {
        msgerr("error reading input");
        return EXIT_FAILURE;
    } else if (wr < 0) {
        msgerr("error writing output");
        return EXIT_FAILURE;
    } else if (wr != rq) {
        msg("no more space to write output (%d<%d)\n", wr, rq);
        return EXIT_FAILURE;
    }

    if (pathIn != NULL) close(io.fdIn);
    if (pathOut != NULL) close(io.fdOut);
    
    if (offEnd >= 0 && !bIgnEnd && io.out != (offEnd - offStart)) {
        msg("premature end of input (%'" PRIu64 " < %'" PRId64 " bytes)\n", io.out, offEnd - offStart);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
