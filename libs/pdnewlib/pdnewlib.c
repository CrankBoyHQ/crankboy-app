#include "pd_api.h"

static const int PD_SEEK_SET = SEEK_SET;
static const int PD_SEEK_CUR = SEEK_CUR;
static const int PD_SEEK_END = SEEK_END;

// print out newlib commands as they are invoked
#define LOG_NEWLIB 0

// clash -- unistd.h defines these, could be different
#undef SEEK_SET
#undef SEEK_CUR
#undef SEEK_END

#if LOG_NEWLIB
#define LOG_NL(...) pd->system->logToConsole(__VA_ARGS__);
#else
#define LOG_NL(...) \
    do              \
    {               \
    } while (0)
#endif

#ifdef TARGET_PLAYDATE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <unistd.h>
#ifdef errno
#undef errno
#endif
extern int errno;

static PlaydateAPI* pd;


int eventHandler_pdnewlib(PlaydateAPI* _pd, PDSystemEvent event, uint32_t arg)
{
    if (event == kEventInit)
    {
        pd = _pd;
    }
    return 0;
}

void _exit(int code)
{

    while (1)
    {
        pd->system->error("exited with code %d.", code);
    }
}

void __exit(int code)
{
    _exit(code);
}

int _kill(int pid, int sig)
{
    LOG_NL("_kill %d, %d", pid, sig);
    return 0;
}
int _getpid(void)
{
    LOG_NL("_getpid");
    return 1;
}

#define HANDLE_STDIN 0
#define HANDLE_STDOUT 1
#define HANDLE_STDERR 2

#define MAXFILES 32
#define FILEHANDLEOFF 3
SDFile* openfiles[MAXFILES];

typedef struct
{
    char* v;
    size_t c;
    const char* fmt;
} buf_t;

// stdout, stderr
buf_t iobuffs[2] = {{NULL, 0, NULL}, {NULL, 0, "\e[0;31m%s\e[0m"}};

static void write_buf(buf_t* wbuf, char* data, int size)
{
    wbuf->v = realloc(wbuf->v, wbuf->c + size + 1);
    memcpy(wbuf->v + wbuf->c, data, size);
    wbuf->c += size;
    wbuf->v[wbuf->c] = 0;
}

// logs as far as the last newline.
static void log_buf_to_console(buf_t* buf)
{
    char* start = buf->v;
    while (1)
    {
        char* end = strchr(start, '\n');
        if (end == NULL)
        {
            break;
        }
        *end = 0;
        pd->system->logToConsole(buf->fmt ? buf->fmt : "%s", start);
        start = end + 1;
    }
    if (start != buf->v)
    {
        size_t newsize = buf->c + buf->v - start;
        memcpy(buf->v, start, newsize);
        buf->c = newsize;
        buf->v = realloc(buf->v, newsize ? newsize + 1 : 0);
    }
}

int _wait(int* status)
{
    errno = ECHILD;
    LOG_NL("_wait");
    return -1;
}

int _fork(void)
{
    LOG_NL("_fork");
    errno = ENOMEM;
    return -1;
}

int _execve(char* name, char** argv, char** env)
{
    LOG_NL("execve %s", name);
    errno = ENOMEM;
    return -1;
}

int _write(int handle, char* data, int size)
{
    if (handle >= FILEHANDLEOFF)
        LOG_NL("_write f=%d, len=0x%x, d=%p", handle, size, data);
    if (size == 0 || data == NULL)
        return 0;

    if (handle == HANDLE_STDOUT || handle == HANDLE_STDERR)
    {
        buf_t* wbuf = &iobuffs[handle == HANDLE_STDERR];
        write_buf(wbuf, data, size);
        log_buf_to_console(wbuf);
        return size;
    }
    else if (handle >= FILEHANDLEOFF && handle < FILEHANDLEOFF + MAXFILES)
    {
        SDFile* file = openfiles[handle - FILEHANDLEOFF];

        if (file)
        {
            int s = pd->file->write(file, data, size);
            if (s < 0)
            {
                errno = EINVAL;  // FIXME: is this right?
                return -1;
            }
            return s;
        }
    }

    errno = EBADF;
    return -1;
}

int _read(int handle, char* data, int size)
{
    if (handle >= FILEHANDLEOFF)
        LOG_NL("_read f=%d, len=0x%x, d=%p", handle, size, data);

    if (size == 0 || data == NULL)
        return 0;

    if (handle >= FILEHANDLEOFF && handle < FILEHANDLEOFF + MAXFILES)
    {
        SDFile* file = openfiles[handle - FILEHANDLEOFF];

        if (file)
        {
            int s = pd->file->read(file, data, size);
            if (s < 0)
            {
                errno = EINVAL;  // FIXME: is this right?
                return -1;
            }
            return s;
        }
    }

    errno = EBADF;
    return -1;
}

int _open(const char* name, int flags, int mode)
{
    LOG_NL("_open \"%s\", flags=%x, mode=%x", name, flags, mode);

    int force_data = 0;
    if (strncmp(name, "data:/", 5) == 0)
    {
        force_data = 1;
        name += 5;
    }

    if (flags & ~(O_RDONLY | O_RDWR | O_WRONLY | O_APPEND | O_CREAT | O_EXCL))
    {
        pd->system->logToConsole("ERROR _open: flag not supported\n");
        errno = ENOTSUP;
        return -1;
    }

    // pass 0: determine whether to check if file exists
    // pass 1: check if file exists
    SDFile* f = NULL;
    for (int pass = 0; pass <= 1; ++pass)
    {
        if (pass == 1)
        {
            f = pd->file->open(name, force_data ? kFileReadData : kFileRead);
        }

        if (!(flags & O_CREAT))
        {
            // fail if file does not exist
            if (pass == 0)
                continue;

            if (!f)
            {
                // FIXME: is EACCES the right code?
                // pd->system->logToConsole(" -> EACCES\n");
                errno = EACCES;
                return -1;
            }
        }
        else if (flags & O_EXCL)
        {
            if (pass == 0)
                continue;
            // fail if file already exists

            if (f)
            {
                if (pd->file->close(f))
                {
                    pd->system->logToConsole(
                        "ERROR _open: error closing after checking if file exists\n"
                    );
                }
                // pd->system->logToConsole(" -> EEXIST\n");
                errno = EEXIST;
                return -1;
            }
        }
        break;
    }
    if (f)
    {
        if (pd->file->close(f))
        {
            pd->system->logToConsole("ERROR _open: error closing after checking if file exists\n");
            errno = EIO;
            return -1;
        }
    }

    for (size_t i = 0; i < MAXFILES; ++i)
    {
        if (!openfiles[i])
        {
            FileOptions fo;

            switch (flags & (O_RDONLY | O_WRONLY | O_RDWR))
            {
            case O_RDONLY:
                fo = kFileRead;
                if (force_data)
                {
                    fo = kFileReadData;
                }
                break;
            case O_WRONLY:
            case O_RDWR:
                fo = kFileWrite;
                if (flags & O_APPEND)
                {
                    fo = kFileAppend;
                }
                break;
            default:
                pd->system->logToConsole("ERROR _open: invalid flag\n");
                errno = EINVAL;
                return -1;
            }

            openfiles[i] = pd->file->open(name, fo);
            if (openfiles[i] == NULL)
            {
                // pd->system->logToConsole(" -> ENOENT\n");
                errno = ENOENT;
                return -1;
            }

            // pd->system->logToConsole(" -> %d", (int)(FILEHANDLEOFF + i));
            return FILEHANDLEOFF + i;
        }
    }

    // pd->system->logToConsole(" -> ENFILE");
    errno = ENFILE;
    return -1;
}

int _close(int file)
{
    LOG_NL("_close f=%d", file);

    if (file >= FILEHANDLEOFF && file < FILEHANDLEOFF + MAXFILES)
    {
        file -= FILEHANDLEOFF;
        SDFile* f = openfiles[file];
        if (f != NULL)
        {
            if (pd->file->close(f))
            {
                errno = EIO;
                return -1;
            }
            else
            {
                openfiles[file] = NULL;
                return 0;
            }
        }
    }

    // TODO: errno (no such file handle)
    return -1;
}

int _mkdir(char* dir)
{
    LOG_NL("_mkdir \"%s\"", dir);
    return pd->file->mkdir(dir);
}

int _rename(char* src, char* dst)
{
    LOG_NL("_rename \"%s\" -> \"%s\"", src, dst);
    if (pd->file->rename(src, dst))
    {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int _unlink(char* p)
{
    LOG_NL("_unlink \"%s\"", p);
    if (pd->file->unlink(p, 1))
    {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

/*
int _link(char* old, char* new) {
    // TODO
    errno = EMLINK;
    return -1;
}
*/

int _isatty(int file)
{
    return file >= 0 && file <= HANDLE_STDERR;
}

int _lseek(int file, int pos, int whence)
{
    LOG_NL("_lseek f=%d, pos=%d, whence=%d", file, pos, whence);
    // translate whence
    int pdwhence;
    switch (whence)
    {
    case SEEK_SET:
        pdwhence = PD_SEEK_SET;
        break;
    case SEEK_CUR:
        pdwhence = PD_SEEK_CUR;
        break;
    case SEEK_END:
        pdwhence = PD_SEEK_END;
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    if (file >= FILEHANDLEOFF && file < MAXFILES + FILEHANDLEOFF)
    {
        SDFile* f = openfiles[file - FILEHANDLEOFF];
        if (f)
        {
            if (pd->file->seek(f, pos, whence))
            {
                errno = EINVAL;  // FIXME: is this right?
                return -1;
            }
            return 0;
        }
    }

    errno = EBADF;
    return -1;
}

int _fstat(int file, struct stat* st)
{
    LOG_NL("_fstat f=%d, %p", file, st);
    memset(stat, 0, sizeof(stat));
    if (_isatty(file))
    {
        st->st_mode = S_IFCHR;
    }
    else
    {
        // TODO: get filepath and then call _stat(...)
        errno = ENFILE;
        return -1;
    }
    return 0;
}

int _stat(char* file, struct stat* st)
{
    LOG_NL("_stat %p", st);
    memset(stat, 0, sizeof(stat));
    FileStat pdstat;

    if (pd->file->stat(file, &pdstat))
    {
        errno = ENOENT;
        return -1;
    }

    st->st_mode = pdstat.isdir ? S_IFDIR : S_IFREG;
    st->st_size = pdstat.size;
    st->st_blksize = 512;
    st->st_blocks = (pdstat.size + st->st_blksize - 1) / st->st_blksize;
    st->st_atime = (0);  // TODO
    st->st_mtime = st->st_atime;
    st->st_ctime = st->st_ctime;
    return 0;
}

clock_t _times(struct tms* buf)
{
    LOG_NL("_times %p", buf);
    // Get time in milliseconds since system start
    uint32_t ms = pd->system->getCurrentTimeMilliseconds();
    clock_t ticks = (clock_t)(ms / 10);  // pretend 1 tick = 10 ms = 100 Hz

    if (buf)
    {
        buf->tms_utime = ticks;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }

    return ticks;
}

int _gettimeofday(struct timeval* tv, void* tz)
{
    LOG_NL("_gettimeofday %p %p", tv, tz);
    (void)tz;  // timezone is obsolete and unused

    uint32_t ms = pd->system->getCurrentTimeMilliseconds();
    if (tv)
    {
        tv->tv_sec = ms / 1000;
        tv->tv_usec = (ms % 1000) * 1000;
    }
    return 0;
}

static char* __env[1] = {0};
char** _environ = __env;

int _link(char* oldpath, char* newpath)
{
    // TODO
    errno = EMLINK;
    return -1;
}

void pdnewlib_quit(void)
{
    if (iobuffs[0].v)
    {
        free(iobuffs[0].v);
        iobuffs[0].v = NULL;
    }
    if (iobuffs[1].v)
    {
        free(iobuffs[1].v);
        iobuffs[1].v = NULL;
    }
}

#else

int eventHandler_pdnewlib(PlaydateAPI* p, PDSystemEvent e, uint32_t a)
{
    return 0;
}

#endif
