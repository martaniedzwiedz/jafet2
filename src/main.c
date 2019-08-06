#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <sys/mman.h>
#include <string.h>
#include <stdint-gcc.h>
//#include <asm_io.h>
//#include <dspcomm.h>
//#include <pu-shm.h>
#define DSP_RING_BASE		0x1000
static unsigned long long shm_offset;
static const u8 *shm;
static unsigned long shm_size;
static const char *shm_device;
static int dspcomm_remap(unsigned long size);

int dspcomm_init(const char *device, unsigned long long offset)
{
    unsigned long page_size = sysconf(_SC_PAGE_SIZE);

    if (shm_offset & (page_size - 1)) {
        fprintf(stderr, "error: offset must be page aligned\n");
        return -EINVAL;
    }

    shm_device = strdup(device);
    if (shm_device == NULL)
        return -EINVAL;

    shm_offset = offset;
    return dspcomm_remap(DSP_RING_BASE);
}

static int dspcomm_remap(unsigned long size)
{
    unsigned long page_size = sysconf(_SC_PAGE_SIZE);
    void *p;
    int ret;
    int fd;

    size = (size + page_size - 1) & ~(page_size - 1);
    if (shm_size == size)
        return 0;

    /*
     * read-only access is currently sufficient, we just want to
     * watch DSP sequence numbers and receive data blocks
     */
    fd = open(shm_device, O_RDONLY);
    if (fd == -1) {
        perror("cannot open DSP device");
        ret = -errno;
        goto out;
    }

    p = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, shm_offset);
    if (p == MAP_FAILED) {
        perror("cannot map DSP shared memory");
        ret = -errno;
        goto out_close;
    }
    fprintf(stderr, "dspcomm: shm: %ld KiB mapped at %p\n",
            size >> 10, p);

    if (shm)
        munmap((void *)(uintptr_t)shm, shm_size);
    shm = p;
    shm_size = size;
    ret = 0;
    out_close:
    close(fd);
    out:
    return ret;
}

int main() {

    for (;;) {}
}

//////main_loop()-> context_read()->pu_proto_cmd()-> pu_proto_read()-> copy_from_dsp()