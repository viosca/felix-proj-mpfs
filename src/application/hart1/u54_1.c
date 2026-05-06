#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // For free()
#include <errno.h>
#include <felix.h>
#include "fs/rootfs/dev/flx_fs_dev.h"
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
#include "kernel/sched.h"
#include "kernel/process.h"

static ssize_t mmuart_vfs_read(FlxFile_t *file, void *buf, size_t count) {
    int minor = file->node->dev_id & 0xFF;
    FlxDev_t *dev = flx_dev_get(minor);
    if (!dev) return -ENODEV;

    ssize_t ret;
    while ((ret = serial_read(dev, (char *)buf, count)) == 0) {
        sched_sem_take(dev->rx_semaphore, SCHED_WAIT_FOREVER);
    }

    return ret;
}

static ssize_t mmuart_vfs_write(FlxFile_t *file, const void *buf, size_t count) {
    int minor = file->node->dev_id & 0xFF;
    FlxDev_t *dev = flx_dev_get(minor);
    if (!dev) return -ENODEV;

    /* Write directly to the serial core. (No need to tick the SD card here anymore) */
    return serial_write(dev, (const char *)buf, count);
}

/* Create the standard operations struct */
static FlxVfsOps_t mmuart_ops = {
    .read = mmuart_vfs_read,
    .write = mmuart_vfs_write
};

static void init_task(void *arg) {
    (void)arg;

    /* Allocate POSIX Identity (TLS is now active) */
    flx_proc_init_current(FLX_PROC_PID_SHELL);

    /* Boot the VFS and OS Subsystems */
    felix_init();

    /* Register Devices */
    felix_register_uart(1, &g_mss_uart1_lo, &g_mss_uart_ops);

    /* Register all other mmuart nodes */
    flx_fs_dev_register_chr("mmuart0", 4, 0, &mmuart_ops);
    flx_fs_dev_register_chr("mmuart1", 4, 1, &mmuart_ops);
    flx_fs_dev_register_chr("mmuart2", 4, 2, &mmuart_ops);
    flx_fs_dev_register_chr("mmuart3", 4, 3, &mmuart_ops);
    flx_fs_dev_register_chr("mmuart4", 4, 4, &mmuart_ops);
    flx_fs_dev_register_chr("tty01", 4, 1, &mmuart_ops);
    flx_fs_dev_register_chr("console", 4, 1, &mmuart_ops);

    /* Bind Standard I/O (This now works because the PCB exists) */
    felix_console_bind("/dev/console");

    /* C-Library Housekeeping */
    clearerr(stdin); clearerr(stdout); clearerr(stderr);
    setvbuf(stdin,  NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("\n[Kernel] FreeRTOS Engine is ticking. Entering Shell...\n");

    /* Launch User Space */
    flx_sh_main(0, NULL);

    /* Teardown if it exits */
    flx_proc_exit();
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
    clear_csr(mie, MIP_MEIP);
    set_csr(mie, MIP_MSIP);
    //set_csr(mie, MIP_MSIP | MIP_MEIP);
    __enable_irq();

    /* =========================================================
     * RTOS HANDOFF
     * ========================================================= */

    /* Spawn the background daemon to handle SD card hotplugging */
    void *blk_handle = NULL;
    sched_thread_create(&blk_handle, "BlkDaemon", 16384, blk_manager_daemon_task, NULL);

    /* Spawn the primary OS Init Task */
    void *shell_handle = NULL;
    sched_thread_create(&shell_handle, "InitShell", 32768, init_task, NULL);

    /* Forcefully disable Machine External Interrupts (Bit 11) in the 'mie' register */
    clear_csr(mie, MIP_MEIP);

    /* Start the FreeRTOS Engine! */
    if (active_scheduler && active_scheduler->start) {
        active_scheduler->start();
    }

#if 0

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
#if 0
    /* Launch the Shell! This function contains the linenoise REPL
     * and should not return under normal circumstances. */
    flx_sh_main(0, NULL);

#else
    /* We are creating our first FreeRTOS Task!
     * We give it a massive 32KB stack (4096 words * 8 bytes) just to be safe
     * while we test, and point it to our wrapper. */
    void *shell_handle = NULL;
    sched_thread_create(&shell_handle, "InitShell", 32768, init_task, NULL);

    /* THE BULLETPROOF MUZZLE:
     * Forcefully disable Machine External Interrupts (Bit 11) in the 'mie' register.
     * This guarantees the PLIC cannot ambush the FreeRTOS engine. */
    clear_csr(mie, MIP_MEIP);

    /* Spawn the background daemon to handle SD card hotplugging */
    void *blk_handle = NULL;
    sched_thread_create(&blk_handle, "BlkDaemon", 2048, blk_manager_daemon_task, NULL);

    /* Start the FreeRTOS Engine.
     * This configures the MTIME timer, hijacks the trap vector, and starts preemption.
     * THIS FUNCTION WILL NEVER RETURN. */
    if (active_scheduler && active_scheduler->start) {
        active_scheduler->start();
    }
#endif
#endif

    /* Fallback trap if the shell ever exits */
    while (1) {
        __asm("wfi");
    }
    /* never return */
}

