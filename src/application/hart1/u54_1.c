#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <felix.h>
#include "fs/rootfs/dev/flx_fs_dev.h"
#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "mss_serial_port.h"
#include "drivers/flx_serial_core.h"
#include "shell/flx_sh.h"

volatile uint32_t count_sw_ints_h1 = 0U;
#include "kernel/syscalls.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "kernel/vfs.h"
#include "kernel/klog.h"

#include "drivers/mss/mss_mmuart/mss_uart.h"

#include "kernel/blk_manager.h" /* Make sure this is included at the top */
#include "kernel/sched.h"
#include "kernel/process.h"
#include <sys/utsname.h>

/* Hardware-specific polling transmission for early boot logs */
static void mss_early_putc(char c) {
    MSS_UART_polled_tx(&g_mss_uart1_lo, (const uint8_t *)&c, 1);
}

void felix_earlycon(void) {
/* =========================================================
     * EARLY CONSOLE (earlycon) INIT
     * ========================================================= */
    uint32_t hart_id = read_csr(mhartid);

    /* 1. Turn on the peripheral clock for UART1 */
    mss_config_clk_rst(MSS_PERIPH_MMUART1, (uint8_t)hart_id, PERIPHERAL_ON);

    /* 2. Configure the silicon for 115200 8N1 (No Interrupts yet!) */
    MSS_UART_init(&g_mss_uart1_lo, MSS_UART_115200_BAUD,
                  MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT);

    flx_klog_set_earlycon(mss_early_putc);

    kprintf("\r\n[Bootloader] CPU Hart %d awake. Early console active.\r\n", hart_id);
}
void test_memory_wrappers(void) {
    kprintf("\n--- STARTING MEMORY DIAGNOSTICS ---\n");

    /* Test 1: Standard Malloc & Free */
    kprintf("[Test 1] Standard malloc(128)... ");
    void *p1 = malloc(128);
    if (p1) {
        kprintf("Allocated (%p). Freeing... ", p1);
        free(p1);
        kprintf("OK.\n");
    } else {
        kprintf("FAILED.\n");
    }

    /* Test 2: Newlib Internal Allocator (strdup)
     * This is the exact function Linenoise uses when you hit ENTER! */
    kprintf("[Test 2] Newlib strdup()... ");
    char *p2 = strdup("Felix OS Test String");
    if (p2) {
        kprintf("Allocated (%p). Freeing... ", (void *)p2);
        free(p2);
        kprintf("OK.\n");
    } else {
        kprintf("FAILED.\n");
    }

    /* Test 3: Calloc & Realloc */
    kprintf("[Test 3] calloc & realloc... ");
    void *p3 = calloc(4, 32);
    if (p3) {
        p3 = realloc(p3, 256);
        kprintf("Allocated/Reallocated (%p). Freeing... ", p3);
        free(p3);
        kprintf("OK.\n");
    } else {
        kprintf("FAILED.\n");
    }

    kprintf("--- MEMORY DIAGNOSTICS COMPLETE ---\n\n");
}
static int board_init(void) {
    int status;

    /* Boot the VFS and OS Subsystems */
    felix_init();

    /* Allocate POSIX Identity (TLS is now active) */
    flx_proc_init_current(FLX_PROC_PID_INIT);

    /* Register Devices */
    status = felix_register_uart(1, &g_mss_uart1_lo, &g_mss_uart_ops);
    if (status) {
        kprintf("\r\n[Kernel] Failed to register uart. Status: %d\r\n", status);
    }

    /* Register all other mmuart nodes */
    flx_fs_dev_register_chr("mmuart0", 4, 0, &g_flx_serial_vfs_ops);
    flx_fs_dev_register_chr("mmuart1", 4, 1, &g_flx_serial_vfs_ops);
    flx_fs_dev_register_chr("mmuart2", 4, 2, &g_flx_serial_vfs_ops);
    flx_fs_dev_register_chr("mmuart3", 4, 3, &g_flx_serial_vfs_ops);
    flx_fs_dev_register_chr("mmuart4", 4, 4, &g_flx_serial_vfs_ops);
    flx_fs_dev_register_chr("tty01", 4, 1, &g_flx_serial_vfs_ops);
    flx_fs_dev_register_chr("console", 4, 1, &g_flx_serial_vfs_ops);

    /* Bind Standard I/O */
    if (felix_console_bind("/dev/console") != 0) {
        kprintf("\r\n[Kernel] FATAL: Failed to bind /dev/console! VFS Node missing!\r\n");
    }
    return status;
}
static void init_task(void *arg) {
    (void)arg;
    kprintf("\r\n[Kernel] init_task called!\r\n");

    int status = board_init();

#if 1
    flx_blk_manager_init();
    /* Spawn the background daemon to handle SD card hotplugging */
    void *blk_handle = NULL;
    status = sched_thread_create(&blk_handle, "BlkDaemon", 16384, 3, blk_manager_daemon_task, NULL);
    if (status) {
        kprintf("\r\n[Kernel] Failed to create BlkDaemon. Status: %d\r\n", status);
    }
#endif

    printf("\n[Kernel] FreeRTOS Engine is ticking. Entering Shell...\n");

#if 0
    while (1) {
        printf("Enter a char: ");
        putchar(getchar());
        printf("\n");
    }
#else
    //test_memory_wrappers();
    /* Launch User Space */
    status = flx_sh_main(0, NULL);

#endif


    kprintf("\r\n[Kernel] flx_sh_main returned. This should not happen. Status: %d\r\n", status);

    /* Teardown if it exits */
    flx_proc_exit(0);
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

#if 0
    /* Enable External (PLIC) and Software Interrupts, then turn on Global IRQs. */
    clear_csr(mie, MIP_MEIP);
    set_csr(mie, MIP_MSIP);
    set_csr(mie, MIP_MSIP | MIP_MEIP);
#endif

#if 0

    unsigned long mstatus_val = read_csr(mstatus);
    mstatus_val |= (1 << 13);
    set_csr(mstatus, mstatus_val);

    //__enable_irq();
#endif
    felix_earlycon();

    kprintf("[Kernel] After Earlycon\n");

    /* =========================================================
     * RTOS HANDOFF
     * ========================================================= */

#if 0
    flx_blk_manager_init();
    /* Spawn the background daemon to handle SD card hotplugging */
    void *blk_handle = NULL;
    int status = sched_thread_create(&blk_handle, "BlkDaemon", 16384, 3, blk_manager_daemon_task, NULL);
    if (status) {
        kprintf("\r\n[Kernel] Failed to create BlkDaemon. Status: %d\r\n", status);
    }
#endif

#if 1
    /* Spawn the primary OS Init Task */
    void *shell_handle = NULL;
    sched_thread_create(&shell_handle, "InitShell", 32768, 2, init_task, NULL);

    kprintf("[Kernel] After sched_thread_create\n");


    /* Forcefully disable Machine External Interrupts (Bit 11) in the 'mie' register */
    clear_csr(mie, MIP_MEIP);

    /* Start the FreeRTOS Engine! */
    if (active_scheduler && active_scheduler->start) {
        active_scheduler->start();
    }
#else
    board_init();
    while (1) {
        printf("Enter a char: ");
        putchar(getchar());
        printf("\n");
    }
#endif

    /* Fallback trap if the shell ever exits */
    while (1) {
        __asm("wfi");
    }
    /* never return */
}

