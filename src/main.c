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

#define DSP_MAGIC		0x00
#define DSP_VERSION		0x04
#define DSP_SEQNUM		0x08
#define DSP_GENERATION		0x10
#define DSP_SAMPLE_SIZE		0x14
#define DSP_RING_ENTRIES	0x18
#define DSP_SAMPLES_PER_GROUP	0x1c
#define DSP_SHM_SIZE		0x20
#define DSP_IGROUP		0x28	/* last important group */
#define DSP_FIRST		0x30

#define DSP_GENERATION_BUSY	0x80000000

#define DSP_ALIGNMENT		0x1000
#define DSP_RING_BASE		0x1000

#define DSP_MAGIC_VALUE		0x50750a00

static const uint8_t *shm;
static const char *shm_device;
static unsigned long long shm_offset;
static unsigned long shm_size;
static const char *dsp_device = "/dev/zero";
static unsigned long long dsp_offset=0x1000000;
static int dsp_initialized;
static unsigned int generation;
static unsigned int sample_size;
static unsigned int sample_align;
static unsigned int ring_entries;
static unsigned int ring_samples_per_group;

static int dspcomm_remap(unsigned long size);

static int dsp_ready(void)
{
	uint32_t x;

	x = readl(shm + DSP_MAGIC);
	return x == DSP_MAGIC_VALUE;
}

static int dsp_begin(void)
{
	int ret;
	uint32_t x;

	if (!dsp_initialized) {
		if (!dsp_ready())
			return -ENXIO;
	}

	x = readl(shm + DSP_GENERATION);
	if (x == generation && dsp_initialized)
		return 0;
	dsp_initialized = 0;
	generation = x;

	if (x & DSP_GENERATION_BUSY)
		return -EAGAIN;

	x = readl(shm + DSP_SHM_SIZE);
	if (x < DSP_RING_BASE) {
		fprintf(stderr, "too small shared memory region: %d KiB\n",
				(unsigned int) x);
		return -EINVAL;
	}

	ret = dspcomm_remap(x);
	if (ret)
		return ret;

	x = readl(shm + DSP_SAMPLE_SIZE);
	sample_size = x;
	sample_align = (x + DSP_ALIGNMENT - 1) & ~(DSP_ALIGNMENT - 1);
	ring_samples_per_group = readl(shm + DSP_SAMPLES_PER_GROUP);
	ring_entries = readl(shm + DSP_RING_ENTRIES);
	if (ring_entries < 2) {
		fprintf(stderr, "too small ring entries, need >= 2\n");
		return -EINVAL;
	}

	if (ring_entries > (shm_size - DSP_RING_BASE) / sample_align) {
		fprintf(stderr, "invalid ring entries: %d * %d KiB > %ld\n",
				ring_entries, sample_align >> 10,
				shm_size - DSP_RING_BASE);
		return -EINVAL;
	}

	fprintf(stderr, "dspcomm: sample %d B, %d entries\n",
			sample_size, ring_entries);
	dsp_initialized = 1;
	return 0;
}

static int dsp_end(void)
{
	uint32_t x;

	x = readl(shm + DSP_GENERATION);
	if (x == generation && dsp_initialized) {
		if (!dsp_ready())
			return -ENXIO;
		return 0;
	}
	return -EAGAIN;
}

static unsigned int dsp_seqnum(void)
{
	uint32_t x;

	x = readl(shm + DSP_SEQNUM);
	return x;
}

static unsigned int dsp_first(void)
{
	uint32_t x;

	x = readl(shm + DSP_FIRST);
	return x;
}

static unsigned int dsp_igroup(void)
{
	uint32_t x;

	x = readl(shm + DSP_IGROUP);
	return x;
}

/*
 * dsp_sample_size - return sample size used by DSP
 */
unsigned int dsp_sample_size(void)
{
	return sample_size;
}

static const uint8_t *dsp_get_buffer(unsigned int seq)
{
	unsigned long offset;

	seq %= ring_entries;
	offset = seq * sample_align;
	return shm + DSP_RING_BASE + offset;
}

void __copy_from_dsp(unsigned int seq, unsigned long offset, void *dst,
		unsigned long count)
{
	const uint8_t *buffer = dsp_get_buffer(seq);

	memcpy_fromio(dst, buffer + offset, count);
}

int copy_from_dsp(unsigned int seq, unsigned long offset, void *dst,
		unsigned long count)
{
	unsigned int dsp_seq;
	unsigned int first;
	int ret;

	ret = dsp_begin();
	if (ret)
		goto err;

	dsp_seq = dsp_seqnum();
	first = dsp_first();

	ret = -ENOENT;
	if ((seq >= dsp_seq) || (seq < first) || (seq + ring_entries < seq)
			|| (seq + ring_entries < dsp_seq))
		goto err;

	__copy_from_dsp(seq, offset, dst, count);
	mb();

	dsp_seq = dsp_seqnum();
	first = dsp_first();
	ret = -ENOENT;
	if ((seq < first) || (seq + ring_entries < dsp_seq))
		goto err;

	ret = dsp_end();
	if (ret)
		goto err;

	return 0;

err:
	return ret;
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

int main() {
if (dspcomm_init(dsp_device, dsp_offset))
		die("cannot initialize DSP communication");
    for (;;) {}
}

//////main_loop()-> context_read()->pu_proto_cmd()-> pu_proto_read()-> copy_from_dsp()
