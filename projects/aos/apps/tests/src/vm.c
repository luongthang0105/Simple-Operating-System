#include <tests/vm.h>
#include <utils/page.h>
#include <tests/macros.h>
#include <unistd.h>
#include <fcntl.h>
#include <sos.h>
#include <stdlib.h>

#define NBLOCKS 9
#define NPAGES_PER_BLOCK 28
#define TEST_ADDRESS 0x8000000000


/* called from pt_test */
static void
do_pt_test(char **buf)
{
    int i;


    /* set */
    for (int b = 0; b < NBLOCKS; b++) {
        for (int p = 0; p < NPAGES_PER_BLOCK; p++) {
          buf[b][p * PAGE_SIZE_4K] = p;
        }
    }


    /* check */
    for (int b = 0; b < NBLOCKS; b++) {
        for (int p = 0; p < NPAGES_PER_BLOCK; p++) {
          assert(buf[b][p * PAGE_SIZE_4K] == p);
        }
    }
}


static void stack_test() {
     /* need a decent sized stack */
    char buf1[NBLOCKS][NPAGES_PER_BLOCK * PAGE_SIZE_4K];
    char *buf1_ptrs[NBLOCKS];


    /* check the stack is above phys mem */
    for (int b = 0; b < NBLOCKS; b++) {
        buf1_ptrs[b] = buf1[b];
    }
    assert((void *) buf1 > (void *) TEST_ADDRESS);


    // /* stack test */
    do_pt_test(buf1_ptrs);
}

static void heap_test() {
    char *buf2[NBLOCKS];

    /* heap test */
    for (int b = 0; b < NBLOCKS; b++) {
        buf2[b] = malloc(NPAGES_PER_BLOCK * PAGE_SIZE_4K);
        assert(buf2[b]);
    }
    do_pt_test(buf2);
    for (int b = 0; b < NBLOCKS; b++) {
        free(buf2[b]);
    }
}

#define BLOCK_SIZE (512 * 1024)  // 512KB, above the 224KB threshold

static void mmap_test() {
    char *bufs[NBLOCKS];

    for (int b = 0; b < NBLOCKS; b++) {
        bufs[b] = malloc(BLOCK_SIZE);
        assert(bufs[b] != NULL); 

        for (size_t i = 0; i < BLOCK_SIZE; i += PAGE_SIZE_4K) {
            bufs[b][i] = (char)(i & 0xFF);
        }
    }

    for (int b = 0; b < NBLOCKS; b++) {
        for (size_t i = 0; i < BLOCK_SIZE; i += PAGE_SIZE_4K) {
            assert(bufs[b][i] == (char)(i & 0xFF));
        }
    }

    for (int b = 0; b < NBLOCKS; b++) {
        free(bufs[b]);
    }
}

/* size constants */
#define KB 1024
#define MB (KB*KB)
#define TOTAL_SIZE (1 * MB)
#define LOOPS (TOTAL_SIZE/PAGE_SIZE_4K)
static char thrash_buf[TOTAL_SIZE];
static void thrash() {
    size_t sz = PAGE_SIZE_4K;
    size_t step = 2;


    int fd = sos_open("thrash_test", O_WRONLY);
    assert(fd != -1);


    for (size_t j = 0; j < LOOPS; j++) {
        for (size_t k = 0; k < sz; k += step) {
            size_t index_to_write = (j * sz) + k;
            uintptr_t addr = (&thrash_buf[index_to_write]);
            thrash_buf[index_to_write] = (char)(addr & (0xFF)); // write the last byte of the address
        }
        int ret = sos_write(fd, &thrash_buf[j * sz], sz);
        assert(ret == sz);
    }
    sos_close(fd);


    fd = sos_open("thrash_test", O_RDONLY);
    assert(fd != -1);
   
    for (size_t j = 0; j < LOOPS; j++) {
        size_t ret = sos_read(fd, &thrash_buf[j * sz], sz);
        assert(ret == sz);
        for (size_t k = 0; k < sz; k += step) {
            size_t index_to_read = (j * sz) + k;
           
            uintptr_t addr = (&thrash_buf[index_to_read]);
            char expected = addr & 0xFF;
           
            if (thrash_buf[index_to_read] != expected) {
                printf("expected: %p, given: %p\n", expected, thrash_buf[index_to_read]);
            }
            assert(thrash_buf[index_to_read] == expected);
        }
    }
    sos_close(fd);
}


void test_virtual_memory()
{
    RUN_TEST(stack_test);
    RUN_TEST(heap_test);
    RUN_TEST(thrash);
    RUN_TEST(mmap_test);

}

