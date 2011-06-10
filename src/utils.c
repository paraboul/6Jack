
#include "common.h"
#include "utils.h"

int upipe_init(Upipe * const upipe)
{
    int fds[2];
    
    if (pipe(fds) != 0) {
        upipe->fd_read = upipe->fd_write = -1;
        return -1;
    }
    upipe->fd_read  = fds[0];
    upipe->fd_write = fds[1];
    
    return 0;
}

void upipe_free(Upipe * const upipe)
{
    if (upipe->fd_read != -1) {
        close(upipe->fd_read);
        upipe->fd_read = -1;
    }
    if (upipe->fd_write != -1) {
        close(upipe->fd_write);
        upipe->fd_write = -1;
    }
}

int safe_write(const int fd, const void * const buf_, size_t count,
               const int timeout)
{
    const char *buf = (const char *) buf_;
    ssize_t written;
    struct pollfd pfd;
    
    pfd.fd = fd;
    pfd.events = POLLOUT;
    
    while (count > (size_t) 0) {
        for (;;) {
            if ((written = write(fd, buf, count)) <= (ssize_t) 0) {
                if (errno == EAGAIN) {
                    if (poll(&pfd, (nfds_t) 1, timeout) == 0) {
                        errno = ETIMEDOUT;
                        return -1;
                    }
                } else if (errno != EINTR) {
                    return -1;
                }
                continue;
            }
            break;
        }
        buf += written;
        count -= written;
    }
    return 0;
}

ssize_t safe_read(const int fd, void * const buf_, size_t count)
{
    unsigned char *buf = (unsigned char *) buf_;
    ssize_t readnb;
    
    do {
        while ((readnb = read(fd, buf, count)) < (ssize_t) 0 &&
               errno == EINTR);
        if (readnb < (ssize_t) 0 || readnb > (ssize_t) count) {
            return readnb;
        }
        if (readnb == (ssize_t) 0) {
ret:
            return (ssize_t) (buf - (unsigned char *) buf_);
        }
        count -= readnb;
        buf += readnb;
    } while (count > (ssize_t) 0);
    goto ret;
}

ssize_t safe_read_partial(const int fd, void * const buf_,
                          const size_t max_count)
{
    unsigned char * const buf = (unsigned char * const) buf_;
    ssize_t readnb;

    while ((readnb = read(fd, buf, max_count)) < (ssize_t) 0 &&
           errno == EINTR);
    return readnb;
}