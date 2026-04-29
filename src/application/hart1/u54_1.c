#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // For free()
#include <errno.h>
#include <felix.h>
#include <fs/rootfs/dev/flx_fs_dev.h.bak>
#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "mss_serial_port.h"
#include "shell/flx_sh.h"
//#include "fs/bin/flx_fs_bin.h"
//#include "kernel/process.h"
//#include "kernel/devices.h"
//#include "drivers/serial.h"
//#include "linenoise.h"

volatile uint32_t count_sw_ints_h1 = 0U;
#include "kernel/syscalls.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "kernel/vfs.h"

#include "drivers/mss/mss_mmuart/mss_uart.h"

#include "kernel/blk_manager.h" /* Make sure this is included at the top */
#if 0
/* =========================================================================
 * RISC-V Machine Timer Interrupt (10ms Heartbeat)
 * ========================================================================= */
void SysTick_Handler(void) {
    /* * The HAL automatically clears the interrupt flag and schedules the
     * next timer event for us. We just do our background OS work here!
     */
    flx_blk_manager_tick();
}
#endif
#if 0
/* VFS Read Wrapper */
static ssize_t mmuart_vfs_read(FlxFile_t *file, void *buf, size_t count) {
    flx_blk_manager_tick();

    /* Extract the minor number from the device ID (assigned during registration) */
    int minor = file->node->dev_id & 0xFF;

    switch (minor) {
        case 0: return MSS_UART_get_rx(&g_mss_uart0_lo, buf, count);
        case 1: {
            return MSS_UART_get_rx(&g_mss_uart1_lo, buf, count);
        }
        case 2: return MSS_UART_get_rx(&g_mss_uart2_lo, buf, count);
        case 3: return MSS_UART_get_rx(&g_mss_uart3_lo, buf, count);
        case 4: return MSS_UART_get_rx(&g_mss_uart4_lo, buf, count);
        default: return -1;
    }
}

/* VFS Write Wrapper */
static ssize_t mmuart_vfs_write(FlxFile_t *file, const void *buf, size_t count) {
    int minor = file->node->dev_id & 0xFF;

    switch (minor) {
        case 0: MSS_UART_polled_tx(&g_mss_uart0_lo, buf, count); return count;
        case 1: MSS_UART_polled_tx(&g_mss_uart1_lo, buf, count); return count;
        case 2: MSS_UART_polled_tx(&g_mss_uart2_lo, buf, count); return count;
        case 3: MSS_UART_polled_tx(&g_mss_uart3_lo, buf, count); return count;
        case 4: MSS_UART_polled_tx(&g_mss_uart4_lo, buf, count); return count;
        default: return -1;
    }
}
#endif

static ssize_t mmuart_vfs_read(FlxFile_t *file, void *buf, size_t count) {
    int minor = file->node->dev_id & 0xFF;
    FlxDev_t *dev = flx_dev_get(minor);
    if (!dev) return -ENODEV;

    /* POSIX Blocking Read: Wait for user input.
     * Pump the kernel idle loop (which ticks the SD card) while waiting! */
    ssize_t ret;
    while ((ret = serial_read(dev, (char *)buf, count)) == 0) {
        flx_blk_manager_tick();
    }
    return ret;
}

static ssize_t mmuart_vfs_write(FlxFile_t *file, const void *buf, size_t count) {
    int minor = file->node->dev_id & 0xFF;
    FlxDev_t *dev = flx_dev_get(minor);
    if (!dev) return -ENODEV;
    flx_blk_manager_tick();

    return serial_write(dev, (const char *)buf, count);
}

/* Create the standard operations struct */
static FlxVfsOps_t mmuart_ops = {
    .read = mmuart_vfs_read,
    .write = mmuart_vfs_write
};

void test_sd_card(void) {
    printf("\n--- Starting SD Card Test ---\n");

    /* 1. Mount the SD Card to the /mnt directory */
    printf("Mounting drive '0:' to /mnt...\n");
    int res = sys_mount("0:", "/mnt", "fatfs", 0, NULL);
    if (res < 0) {
        printf("[ERROR] Failed to mount SD Card! Error code: %d\n", res);
        return;
    }
    printf("[SUCCESS] SD Card mounted!\n");

    /* 2. Write a file using POSIX syscalls */
    printf("Creating /mnt/hello.txt...\n");
    int fd = sys_open("/mnt/hello.txt", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd >= 0) {
        const char *msg = "Hello from Felix OS via Microchip HAL!\n";
        sys_write(fd, msg, strlen(msg));
        sys_close(fd);
        printf("[SUCCESS] File written.\n");
    } else {
        printf("[ERROR] Failed to open file for writing! (fd = %d)\n", fd);
    }

    /* 3. Read the file back */
    printf("Reading /mnt/hello.txt...\n");
    fd = sys_open("/mnt/hello.txt", O_RDONLY, 0);
    if (fd >= 0) {
        char buf[64] = {0};
        sys_read(fd, buf, sizeof(buf) - 1);
        printf("[SUCCESS] File contents: '%s'\n", buf);
        sys_close(fd);
    } else {
         printf("[ERROR] Failed to open file for reading!\n");
    }

    /* 4. Cleanly Unmount */
    res = sys_umount("/mnt");
    if (res == 0) {
        printf("[SUCCESS] SD Card unmounted safely.\n");
    } else {
        printf("[ERROR] Failed to unmount! Error code: %d\n", res);
    }

    printf("--- SD Card Test Complete ---\n\n");
}

void u54_1(void)
{
    /* Global Interrupts are OFF at this point (from boot) */
    PLIC_init();

    /* Clear any old software interrupts and unmask MSIP to allow WFI wake-up */
    clear_soft_interrupt();
    set_csr(mie, MIP_MSIP);

    /* Sleep until E51 signals us.
     * Because MIE is 0, the CPU won't trap to the ISR, it just wakes up. */
    do {
        __asm("wfi");
    } while(0 == (read_csr(mip) & MIP_MSIP));

    /* Awake! Clear the SW interrupt manually before enabling global IRQs. */
    clear_soft_interrupt();

    /* Enable External (PLIC) and Software Interrupts, then turn on Global IRQs. */
    set_csr(mie, MIP_MSIP | MIP_MEIP);
    __enable_irq();

    /* Boot the Felix Operating System */
    felix_init();

    /* Register UART1_LO as POSIX Device ID 0 (stdin/stdout/stderr) */
    felix_register_uart(1, &g_mss_uart1_lo, &g_mss_uart_ops);

    flx_fs_dev_register_chr("mmuart0", 4, 0, &mmuart_ops);
    flx_fs_dev_register_chr("mmuart1", 4, 1, &mmuart_ops);
    flx_fs_dev_register_chr("mmuart2", 4, 2, &mmuart_ops);
    flx_fs_dev_register_chr("mmuart3", 4, 3, &mmuart_ops);
    flx_fs_dev_register_chr("mmuart4", 4, 4, &mmuart_ops);
    flx_fs_dev_register_chr("tty01", 4, 1, &mmuart_ops);
    flx_fs_dev_register_chr("console", 4, 1, &mmuart_ops);

    /* Tell the OS to use that VFS Node for printing! */
    felix_console_bind("/dev/console");
    /* =========================================================
     * ENTER USER SPACE
     * ========================================================= */

    clearerr(stdin);
    clearerr(stdout);
    clearerr(stderr);

    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    /* Put this right before flx_sh_main() is called */
    extern FlxDev_t* flx_dev_get(int minor);


    FlxDev_t *uart_dev = flx_dev_get(1); /* Assuming 0 is your console UART */
    if (uart_dev) {
        const char *msg = "\r\n[Kernel] Entering Shell...\r\n";
        serial_write(uart_dev, msg, strlen(msg));
    }

    //test_sd_card();

    /* Start the RISC-V System Timer (10,000 ticks = 10ms) */
    //SysTick_Config();

    /* Launch the Shell! This function contains the linenoise REPL
     * and should not return under normal circumstances. */
    flx_sh_main(0, NULL);

    /* Fallback trap if the shell ever exits */
    while (1) {
        __asm("wfi");
    }
    /* never return */
}

