#include <debug.h>
#include <kernel.h>
#include <delaythread.h>
#include <iopcontrol.h>
#include <libpad.h>
#include <string.h>
#include <fileXio_rpc.h>
#include <iopheap.h>
#include <sbv_patches.h>
#include <loadfile.h>
#include <sifrpc.h>

#include "app.h"
#include "network.h"
#include "screen.h"
#include "ui.h"
#ifdef CRTUBE_H264_EDGE264_RUNTIME_SMOKE
#include "debug_log.h"
#ifdef CRTUBE_R5900_MMI_TEST_ONLY
#include "edge264_r5900_mmi.h"
#ifdef CRTUBE_R5900_DECODER_TEST_ONLY
#include "h264_edge264_smb_playback.h"
#endif
#include <stdint.h>
#ifndef CRTUBE_R5900_DECODER_TEST_ONLY
/* The decoder-independent suite compiles Edge264 function units without
 * linking the full decoder/profile subsystem. Keep profiling inert here. */
uint64_t edge264_profile_begin(void)
{
    return 0;
}

void edge264_profile_accumulate(unsigned id, uint64_t start_ticks)
{
    (void)id;
    (void)start_ticks;
}
#endif

int crtube_r5900_test_deblock_functions(void);
int crtube_r5900_test_idct_functions(void);
int crtube_r5900_test_dc_functions(void);
int crtube_r5900_test_intra_kernels(void);
int crtube_r5900_test_inter_kernels(void);
int crtube_r5900_test_mvpred_kernels(void);
int crtube_r5900_test_residual_kernels(void);
int crtube_r5900_test_deblock_kernels(void);
int crtube_vu_parallel_selftest(void);
#else
#include "h264_edge264.h"
#include "h264_edge264_smb_playback.h"
#endif
#endif

extern unsigned char IOMANX_irx[];
extern unsigned int size_IOMANX_irx;
extern unsigned char FILEXIO_irx[];
extern unsigned int size_FILEXIO_irx;

static unsigned char pad_buffer[256] __attribute__((aligned(64)));
static volatile unsigned short controller_buttons = 0xffff;
static volatile unsigned short controller_pressed = 0;
static volatile int controller_exit_requested = 0;
static volatile int controller_monitor_stop = 0;
static volatile int emergency_alarm_id = -1;
static volatile int app_exit_started = 0;
static int controller_pad_open = 0;
static unsigned short previous_controller_buttons = 0xffff;
static unsigned char emergency_stack[4096] __attribute__((aligned(16)));
static ee_thread_t emergency_thread;
static int emergency_thread_id = -1;
extern void *_gp;

static int crtube_filexio_load_modules(void)
{
    int result;

    scr_printf("Stage: loading IOMANX.IRX...\n");
    result = SifExecModuleBuffer(IOMANX_irx, size_IOMANX_irx, 0, NULL, NULL);
    if (result < 0) {
        scr_printf("IOMANX load failed: %d\n", result);
        return -1;
    }

    scr_printf("Stage: loading FILEXIO.IRX...\n");
    result = SifExecModuleBuffer(FILEXIO_irx, size_FILEXIO_irx, 0, NULL, NULL);
    if (result < 0) {
        scr_printf("FILEXIO load failed: %d\n", result);
        return -2;
    }
    return 0;
}

static int crtube_filexio_init(void)
{
    int result;

    scr_printf("Stage: fileXioInit...\n");
    result = fileXioInit();
    scr_printf("fileXioInit returned: %d\n", result);
    return result;
}

static int crtube_controller_init(void)
{
    scr_printf("Loading SIO2MAN...\n");
    if (SifLoadModule("rom0:SIO2MAN", 0, NULL) < 0)
        return -1;
    scr_printf("Loading MCMAN...\n");
    if (SifLoadModule("rom0:MCMAN", 0, NULL) < 0)
        return -1;
    scr_printf("Loading MCSERV...\n");
    if (SifLoadModule("rom0:MCSERV", 0, NULL) < 0)
        return -1;

    scr_printf("Loading PADMAN...\n");
    if (SifLoadModule("rom0:PADMAN", 0, NULL) < 0)
        return -1;

    scr_printf("Initializing PAD...\n");
    padInit(0);
    scr_printf("Opening controller port...\n");
    if (padPortOpen(0, 0, pad_buffer) == 0)
        return -1;
    controller_pad_open = 1;

    scr_printf("Controller ready.\n");
    return 0;
}

static void crtube_poll_wakeup(s32 alarm_id, u16 time, void *common)
{
    (void)alarm_id;
    (void)time;
    emergency_alarm_id = -1;
    iWakeupThread(*(int *)common);
}

static void crtube_controller_poll(void)
{
    struct padButtonStatus buttons;
    int state;

    state = padGetState(0, 0);
    if (state != PAD_STATE_STABLE && state != PAD_STATE_FINDCTP1) {
        controller_buttons = 0xffff;
        previous_controller_buttons = 0xffff;
        return;
    }

    if (padRead(0, 0, &buttons) == 0) {
        controller_buttons = 0xffff;
        previous_controller_buttons = 0xffff;
        return;
    }

    controller_buttons = buttons.btns;
    controller_pressed |= (unsigned short)(previous_controller_buttons &
                                            (unsigned short)~buttons.btns);
    previous_controller_buttons = buttons.btns;

    /* Pad button bits are active-low. Ask the UI/main thread to return to
       OSDSYS; LoadExecPS2 must not run from the emergency polling thread. */
    if ((buttons.btns & PAD_SELECT) == 0 &&
        (buttons.btns & PAD_START) == 0)
        controller_exit_requested = 1;
}

unsigned short crtube_controller_buttons(void)
{
    return controller_buttons;
}

unsigned short crtube_controller_take_pressed(void)
{
    int interrupt_state;
    unsigned short pressed;

    interrupt_state = DIntr();
    pressed = controller_pressed;
    controller_pressed = 0;
    if (interrupt_state != 0)
        EIntr();
    return pressed;
}

int crtube_controller_take_exit_request(void)
{
    int interrupt_state;
    int requested;

    interrupt_state = DIntr();
    requested = controller_exit_requested;
    controller_exit_requested = 0;
    if (interrupt_state != 0)
        EIntr();
    return requested;
}

void crtube_app_return_to_osdsys(void)
{
    int alarm_id;

    if (app_exit_started)
        return;
    app_exit_started = 1;

    scr_printf("\nShutting down CRTube...\n");

    /* Stop the EE-side PAD polling thread before tearing down IOP services.
       The thread checks the stop flag after arming its alarm, so it also
       handles a shutdown racing with SetAlarm(). */
    controller_monitor_stop = 1;
    if (emergency_thread_id >= 0) {
        WakeupThread(emergency_thread_id);
        DelayThread(2000);
        alarm_id = emergency_alarm_id;
        if (alarm_id >= 0)
            ReleaseAlarm(alarm_id);
        emergency_alarm_id = -1;
        TerminateThread(emergency_thread_id);
        DeleteThread(emergency_thread_id);
        emergency_thread_id = -1;
    }

    /* Release PAD ownership and network services before the IOP reset.
       Resetting the IOP also unloads SMBMAN, FILEXIO and the network IRXs,
       preventing stale IOP state from being inherited by the OPL session. */
    if (controller_pad_open) {
        padPortClose(0, 0);
        padEnd();
        controller_pad_open = 0;
    }
    crtube_network_deinit();

    scr_printf("Returning to PS2 system menu...\n");
    DelayThread(100000);

    SifExitRpc();
    while (!SifIopReset(NULL, 0)) {
    }
    while (!SifIopSync()) {
    }

    LoadExecPS2("rom0:OSDSYS", 0, NULL);

    /* LoadExecPS2 should not return. If it does, keep the app from continuing
       with partially shut-down services. */
    for (;;)
        SleepThread();
}

static void crtube_emergency_monitor(void *arg)
{
    int thread_id = GetThreadId();

    (void)arg;
    while (!controller_monitor_stop) {
        int alarm_id;

        crtube_controller_poll();
        if (controller_monitor_stop)
            break;

        alarm_id = SetAlarm(64, crtube_poll_wakeup, &thread_id);
        emergency_alarm_id = alarm_id;

        /* Close the race where shutdown begins between the stop check and
           SleepThread(): cancel the newly-created alarm and leave the loop. */
        if (controller_monitor_stop) {
            if (alarm_id >= 0)
                ReleaseAlarm(alarm_id);
            emergency_alarm_id = -1;
            break;
        }
        SleepThread();
    }
}

static int crtube_start_emergency_monitor(void)
{
    memset(&emergency_thread, 0, sizeof(emergency_thread));
    emergency_thread.func = crtube_emergency_monitor;
    emergency_thread.stack = emergency_stack;
    emergency_thread.stack_size = sizeof(emergency_stack);
    emergency_thread.gp_reg = &_gp;
    emergency_thread.initial_priority = 0x40;
    emergency_thread.attr = 0;
    emergency_thread.option = 0;

    emergency_thread_id = CreateThread(&emergency_thread);
    if (emergency_thread_id < 0)
        return -1;

    if (StartThread(emergency_thread_id, NULL) < 0) {
        DeleteThread(emergency_thread_id);
        emergency_thread_id = -1;
        return -2;
    }
    return 0;
}

void crtube_app_run(void)
{
    char ip_address[16];
    int net_result;

    ip_address[0] = '\0';

    init_scr();
    crtube_screen_clear();
    scr_printf("CRTube for PlayStation 2\n\n");
    scr_printf("Starting...\n");
    crtube_screen_set_visible(0);
    crtube_screen_clear();

    scr_printf("Initializing system services...\n");

    scr_printf("Stage: initializing SIF RPC...\n");
    sceSifInitRpc(0);

    /* Reset the IOP so the FileXio RPC server starts from a known state. */
    scr_printf("Stage: resetting IOP...\n");
    while (!SifIopReset(NULL, 0)) {
    }

    scr_printf("Stage: waiting for IOP sync...\n");
    while (!SifIopSync()) {
    }

    scr_printf("Stage: reinitializing SIF RPC...\n");
    sceSifInitRpc(0);
    scr_printf("Stage: initializing IOP heap...\n");
    SifInitIopHeap();
    scr_printf("Stage: initializing loadfile RPC...\n");
    SifLoadFileInit();
    scr_printf("Stage: enabling module load patch...\n");
    sbv_patch_enable_lmb();

    scr_printf("Stage: loading FileXio IOP modules...\n");
    if (crtube_filexio_load_modules() != 0) {
        scr_printf("FileXio IOP modules failed to load\n");
        net_result = CRTUBE_NETWORK_ERR_CONFIG;
    } else if (crtube_controller_init() != 0) {
        scr_printf("Controller initialization failed\n");
        net_result = CRTUBE_NETWORK_ERR_CONFIG;
    } else {
        scr_printf("Stage: starting controller monitor...\n");
        if (crtube_start_emergency_monitor() != 0)
            scr_printf("WARNING: controller monitor could not start\n");

        scr_printf("Stage: initializing FileXio...\n");
        if (crtube_filexio_init() != 0) {
            scr_printf("FileXio initialization failed\n");
            net_result = CRTUBE_NETWORK_ERR_CONFIG;
        } else {
            scr_printf("Stage: initializing network...\n");
            /* YouTube requests are deliberately deferred until the user runs
               Network Status diagnostics. HTTPS/TLS and JSON parsing must not
               delay the first interactive screen during startup. */
            net_result = crtube_network_init();
        }
    }

    if (net_result == CRTUBE_NETWORK_OK) {
        if (crtube_network_get_ip(ip_address, sizeof(ip_address)) == 0)
            scr_printf("Network ready: %s\n", ip_address);
        else
            scr_printf("Network ready; IP address unavailable\n");
    } else {
        scr_printf("Network initialization failed: %d\n", net_result);
    }

    /* SMB mounting and all network probes are deferred until requested from
       the Network Status screen. This keeps startup out of blocking tests. */

    /* Discard controller presses made during boot before accepting UI input. */
    (void)crtube_controller_take_pressed();

#ifdef CRTUBE_H264_EDGE264_RUNTIME_SMOKE
#ifdef CRTUBE_R5900_MMI_TEST_ONLY
    {
        char smb_ip[16];
        unsigned short smb_port = 0;
        int smb_result;
        int log_result = -1;
        int failures;
        int function_failures = 0;
        unsigned op;
        static const char * const operation_names[24] = {
            "PADDUB", "PADDSH", "PMADDH", "PMINUB", "PMAXUB",
            "PSUBUB", "PMINH", "PMAXH", "PCGTH", "PCEQH",
            "PAND", "POR", "PXOR", "PADDW", "PSUBW",
            "PSRAW", "PADDH", "PSUBH", "PSRAH.1", "PSRAH.2",
            "PSRAH.6", "PHMADH", "PHMADH wrapper", "PACKABD/PPACB"
        };

        if (crtube_screen_init() != 0)
            scr_printf("Graphics initialization failed; text-only mode.\n");
        crtube_screen_set_visible(1);
        crtube_screen_clear();
        scr_printf("\nR5900 MMI + decoder function test suite\n");
        scr_printf("MP4 input: DISABLED (no file will be opened)\n");
        scr_printf("Mounting SMB only for diagnostic logging...\n");

        smb_ip[0] = '\0';
        smb_result = crtube_smb_init();
        if (smb_result == 0) {
            log_result = crtube_log_init(NULL);
            if (log_result == 0) {
                (void)crtube_network_get_smb_endpoint(
                    smb_ip, sizeof(smb_ip), &smb_port);
                crtube_log("\n==================================================\n");
                crtube_log("CRTube R5900 MMI + decoder function equivalence suite\n");
                crtube_log("SMB share mounted: server=%s port=%u\n",
                           smb_ip[0] ? smb_ip : "unknown", (unsigned)smb_port);
                crtube_log("Log path: smb0:/APPS/CRTUBE_PS2/dbg_crtube.txt\n");
#ifdef CRTUBE_R5900_DECODER_TEST_ONLY
                crtube_log("MP4 input: embedded tests/data/mc_coverage.mp4\n");
                crtube_log("H.264 decoder: WILL INITIALIZE AFTER FUNCTION SUITE\n");
#else
                crtube_log("MP4 input: NOT OPENED\n");
                crtube_log("H.264 decoder: NOT INITIALIZED\n");
#endif
                crtube_log("Network startup result=%d local_ip=%s\n",
                           net_result, ip_address[0] ? ip_address : "unknown");
                scr_printf("SMB log: READY\n");
            }
        }
        if (smb_result != 0 || log_result != 0) {
            scr_printf("SMB log unavailable (smb=%d log=%d)\n",
                       smb_result, log_result);
        }

        if (smb_result != 0 || log_result != 0) {
            scr_printf("TESTS ABORTED: SMB log is mandatory.\n");
            scr_printf("Fix SMB mount/log path, then rerun this ELF.\n");
            for (;;) {
                if (crtube_controller_take_exit_request())
                    crtube_app_return_to_osdsys();
                DelayThread(100000);
            }
        }

        scr_printf("Executing raw MMI instructions...\n");
        scr_printf("MMI self-test running; please wait...\n");
        crtube_log("MMI test: begin; 20032 vectors per direct operation\n");
        crtube_log("MMI semantics: PMADDH=8 signed halfword products accumulated "
                   "in 8 HI/LO word lanes; PHMADH=4 horizontal adjacent-product sums; "
                   "PACKABD=abs(signed halfword difference) packed to bytes\n");
        failures = edge264_r5900_mmi_selftest();

        for (op = 0; op < 24; ++op) {
            crtube_log("MMI op=%s bit=%u result=%s\n",
                       operation_names[op], op,
                       (failures & (1 << op)) ? "FAIL" : "PASS");
        }
        crtube_log("MMI complete: %s failure_mask=0x%08x\n",
                   failures ? "FAIL" : "PASS", (unsigned)failures);
#if defined(CRTUBE_H264_MMI_MATRIX_TEST)
        if (failures == 0) {
            edge264_r5900_mmi_set_mode(
                EDGE264_R5900_MMI_OP_CHROMA |
                EDGE264_R5900_MMI_OP_WEIGHTED |
                EDGE264_R5900_MMI_OP_MINMAX |
                EDGE264_R5900_MMI_OP_RESIDUAL |
                EDGE264_R5900_MMI_OP_DEBLOCK_ADD |
                EDGE264_R5900_MMI_OP_DEBLOCK_PACKABD);
            crtube_log("Decoder function suite mode=ALL_MMI\n");
        } else {
            edge264_r5900_mmi_set_mode(0u);
            crtube_log("Decoder function suite mode=SCALAR (MMI primitive failure)\n");
        }
#endif

#define RUN_R5900_FUNCTION_TEST(fn, label) do { \
            int test_result; \
            scr_printf("Running: %s...\n", label); \
            test_result = fn(); \
            crtube_log("FUNCTION test=%s result=%s code=%d\n", label, \
                       test_result == 0 ? "PASS" : "FAIL", test_result); \
            scr_printf("%-20s %s (%d)\n", label, \
                       test_result == 0 ? "PASS" : "FAIL", test_result); \
            if (test_result != 0) ++function_failures; \
        } while (0)

#ifdef CRTUBE_R5900_DECODER_TEST_ONLY
        {
            int vu_result;
            scr_printf("\nVU0/VU1 parallel compute test...\n");
            crtube_log("VU0/VU1 parallel compute test: begin\n");
            vu_result = crtube_vu_parallel_selftest();
            scr_printf("VU0/VU1 compute: %s (%d)\n",
                       vu_result == 0 ? "PASS" : "FAIL", vu_result);
            crtube_log("VU0/VU1 parallel compute test: %s code=%d\n",
                       vu_result == 0 ? "PASS" : "FAIL", vu_result);
            if (vu_result != 0) ++function_failures;
        }
#endif

        scr_printf("\nDecoder function equivalence tests:\n");
        crtube_log("Decoder function equivalence tests: begin\n");
        RUN_R5900_FUNCTION_TEST(crtube_r5900_test_deblock_functions, "deblock functions");
        RUN_R5900_FUNCTION_TEST(crtube_r5900_test_idct_functions, "IDCT functions");
        RUN_R5900_FUNCTION_TEST(crtube_r5900_test_dc_functions, "DC reconstruction");
        RUN_R5900_FUNCTION_TEST(crtube_r5900_test_intra_kernels, "intra kernels");
        RUN_R5900_FUNCTION_TEST(crtube_r5900_test_inter_kernels, "inter kernels");
        RUN_R5900_FUNCTION_TEST(crtube_r5900_test_mvpred_kernels, "motion-vector kernels");
        RUN_R5900_FUNCTION_TEST(crtube_r5900_test_residual_kernels, "residual kernels");
        RUN_R5900_FUNCTION_TEST(crtube_r5900_test_deblock_kernels, "deblock kernels");
        crtube_log("Decoder function equivalence tests: %s failures=%d\n",
                   function_failures ? "FAIL" : "PASS", function_failures);
#undef RUN_R5900_FUNCTION_TEST



#ifdef CRTUBE_R5900_DECODER_TEST_ONLY
        {
            int decode_result;
#if defined(CRTUBE_H264_FOUR_PASS_TEST)
            crtube_log("H.264 embedded MP4 EE/VU differential test: begin\\n");
            scr_printf("MP4 EE/VU test (6 x 180 frames): running...\\n");
            decode_result = crtube_h264_edge264_embedded_four_pass();
            crtube_screen_set_visible(1);
            crtube_log("H.264 EE/VU differential test: %s result=%d\\n",
                       decode_result == 0 ? "PASS" : "FAIL", decode_result);
            scr_printf("MP4 EE/VU test: %s (%d)\\n",
                       decode_result == 0 ? "PASS" : "FAIL", decode_result);
#else
            crtube_log("H.264 embedded MP4 decode: begin\\n");
            scr_printf("Embedded MP4 decode (180 frames): running...\\n");
            decode_result = crtube_h264_edge264_embedded_playback();
            crtube_screen_set_visible(1);
            crtube_log("H.264 embedded MP4 decode: %s result=%d\\n",
                       decode_result == 0 ? "PASS" : "FAIL", decode_result);
            scr_printf("Embedded MP4 decode: %s (%d)\\n",
                       decode_result == 0 ? "PASS" : "FAIL", decode_result);
#endif
            if (decode_result != 0)
                ++function_failures;
        }
#endif

        scr_printf("PADDUB: %s\n",
                   (failures & (1 << 0)) ? "FAIL" : "PASS");
        scr_printf("PMADDH: %s\n",
                   (failures & (1 << 2)) ? "FAIL" : "PASS");
        scr_printf("PHMADH: %s\n",
                   (failures & (1 << 21)) ? "FAIL" : "PASS");
        scr_printf("PSUBUB: %s\n",
                   (failures & (1 << 5)) ? "FAIL" : "PASS");
        scr_printf("All MMI primitives: %s (mask=0x%08x)\n",
                   failures ? "FAIL" : "PASS", (unsigned)failures);
        scr_printf("Decoder function tests: %s (%d failures)\n",
                   function_failures ? "FAIL" : "PASS", function_failures);
        scr_printf("Full report: smb0:/APPS/CRTUBE_PS2/dbg_crtube.txt\n");
        crtube_log("R5900 suite final: %s mmi_mask=0x%08x function_failures=%d\n",
                   (failures || function_failures) ? "FAIL" : "PASS",
                   (unsigned)failures, function_failures);
        scr_printf("Test complete. Press SELECT+START to exit.\n");

        crtube_log_close();

        /* Keep the result visible; the normal menu and all MP4 playback
           paths are deliberately bypassed in this build. */
        for (;;) {
            if (crtube_controller_take_exit_request())
                crtube_app_return_to_osdsys();
            DelayThread(100000);
        }
    }
#else
    {
        unsigned decoded_frames = 0;
        size_t decoder_peak = 0;
        char smb_ip[16];
        unsigned short smb_port = 0;
        int smb_result;
        int log_result = -1;
        int smoke_result;

        scr_printf("\nPreparing SMB instrumentation log...\n");
        smb_ip[0] = '\0';
        smb_result = crtube_smb_init();
        if (smb_result == 0) {
            log_result = crtube_log_init(NULL);
            if (log_result == 0) {
                (void)crtube_network_get_smb_endpoint(
                    smb_ip, sizeof(smb_ip), &smb_port);
                crtube_log("\n==================================================\n");
                crtube_log("CRTube experimental H.264 runtime session\n");
                crtube_log("SMB share mounted: server=%s port=%u\n",
                           smb_ip[0] ? smb_ip : "unknown", (unsigned)smb_port);
                crtube_log("Log path: smb0:/APPS/CRTUBE_PS2/dbg_crtube.txt\n");
                crtube_log("Network startup result=%d local_ip=%s\n",
                           net_result, ip_address[0] ? ip_address : "unknown");
                scr_printf("SMB instrumentation log: READY\n");
            }
        }
        if (smb_result != 0 || log_result != 0) {
            scr_printf("SMB instrumentation log unavailable (smb=%d log=%d)\n",
                       smb_result, log_result);
        }

        crtube_log("H.264 runtime smoke: begin\n");
        smoke_result = crtube_h264_edge264_runtime_smoke(
            &decoded_frames, &decoder_peak);
        crtube_ui_set_h264_test_status(smoke_result);
        if (smoke_result == 0) {
            crtube_log("H.264 runtime smoke: PASS frames=%u peak_bytes=%lu\n",
                       decoded_frames, (unsigned long)decoder_peak);
            scr_printf("H.264 EE decode: PASS (%u frames, peak %lu bytes)\n",
                       decoded_frames, (unsigned long)decoder_peak);
        } else {
            crtube_log("H.264 runtime smoke: FAIL result=%d frames=%u peak_bytes=%lu\n",
                       smoke_result, decoded_frames,
                       (unsigned long)decoder_peak);
            scr_printf("H.264 EE decode: FAIL (%d)\n", smoke_result);
        }
        crtube_log("H.264 runtime smoke: end\n");
        {
            int playback_result;
            crtube_log("H.264 SMB playback: test source=APPS/CRTUBE_PS2/240p-baseline.mp4\n");
            playback_result = crtube_h264_edge264_smb_playback(
                "APPS/CRTUBE_PS2/240p-baseline.mp4");
            if (playback_result == 0)
                scr_printf("H.264 SMB test: PASS\n");
            else
                scr_printf("H.264 SMB test: FAIL (%d)\n",
                           playback_result);
#if defined(CRTUBE_H264_DOUBLE_PASS)
            scr_printf("H.264 SMB test: quiet comparison...\n");
            playback_result = crtube_h264_edge264_smb_playback_quiet(
                "APPS/CRTUBE_PS2/240p-baseline.mp4");
            if (playback_result == 0)
                scr_printf("H.264 quiet comparison: PASS\n");
            else
                scr_printf("H.264 quiet comparison: FAIL (%d)\n",
                           playback_result);
#endif
        }

        if (log_result == 0)
            crtube_log_close();
    }
#endif
#endif

    /* Boot diagnostics are buffered, not painted. The first visible frame
       is the main menu. */
    crtube_ui_run(net_result, ip_address);
}
