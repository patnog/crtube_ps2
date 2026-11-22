#include <kernel.h>
#include <delaythread.h>
#include <libpad.h>
#include <stdio.h>

#include "network.h"
#include "app.h"
#include "screen.h"
#include "ui.h"
#include "localization.h"
#include "debug_log.h"
#if defined(CRTUBE_ENABLE_YOUTUBE_PROBE)
#include "images.h"
#include "youtube_search.h"
#endif

#if defined(CRTUBE_ENABLE_YOUTUBE_PROBE)
#define CRTUBE_UI_HAS_YOUTUBE_TEST 1
#else
#define CRTUBE_UI_HAS_YOUTUBE_TEST 0
#endif

typedef enum crtube_ui_screen {
    CRTUBE_UI_HOME = 0,
    CRTUBE_UI_SEARCH,
    CRTUBE_UI_NETWORK,
    CRTUBE_UI_ABOUT
} crtube_ui_screen_t;

static const crtube_text_id_t g_menu_items[] = {
    CRTUBE_TEXT_MENU_SEARCH,
    CRTUBE_TEXT_MENU_NETWORK,
    CRTUBE_TEXT_MENU_ABOUT
};

#define CRTUBE_MENU_ITEMS ((int)(sizeof(g_menu_items) / sizeof(g_menu_items[0])))

static void crtube_ui_render_home(int selection)
{
    char menu_line[160];
    int used;

    crtube_screen_clear();
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_APP_TITLE));
    scr_printf("========================================\n");
    scr_printf("YouTube recommendations\n");

#if CRTUBE_UI_HAS_YOUTUBE_TEST
    crtube_images_draw_home_grid();
#else
    crtube_screen_draw_text(48, 110, 0x00ffffffu,
                            "Build with YOUTUBE_TEST=1 to load videos.");
#endif

    used = snprintf(menu_line, sizeof(menu_line),
                    "%c %s    %c %s    %c %s",
                    selection == 0 ? '>' : ' ',
                    crtube_text(g_menu_items[0]),
                    selection == 1 ? '>' : ' ',
                    crtube_text(g_menu_items[1]),
                    selection == 2 ? '>' : ' ',
                    crtube_text(g_menu_items[2]));
    if (used > 0)
        crtube_screen_draw_text(16, 414, 0x00ffffffu, menu_line);
    crtube_screen_draw_text(16, 432, 0x00808080u,
                            crtube_text(CRTUBE_TEXT_NAVIGATION_HINT));
}

static void crtube_ui_render_search(void)
{
    crtube_screen_clear();
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_SEARCH_TITLE));
    scr_printf("========================================\n\n");
    scr_printf("%s\n\n", crtube_text(CRTUBE_TEXT_SEARCH_UNAVAILABLE));
    scr_printf("%s\n\n", crtube_text(CRTUBE_TEXT_SEARCH_TLS_REQUIRED));
    scr_printf("%s\n\n", crtube_text(CRTUBE_TEXT_SEARCH_NEXT_STEP));
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_BACK_HINT));
}

static void crtube_ui_render_network(int network_result,
                                     const char *ip_address,
                                     int tests_completed)
{
    (void)tests_completed;
    crtube_screen_clear();
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_NETWORK_TITLE));
    scr_printf("========================================\n\n");

    if (network_result == CRTUBE_NETWORK_OK)
        scr_printf("%s\n", crtube_text(CRTUBE_TEXT_NETWORK_READY));
    else {
        scr_printf(crtube_text(CRTUBE_TEXT_NETWORK_FAILED), network_result);
        scr_printf("\n");
    }

    if (ip_address != NULL && ip_address[0] != '\0') {
        scr_printf(crtube_text(CRTUBE_TEXT_IP_LABEL), ip_address);
        scr_printf("\n");
    } else {
        scr_printf("%s\n", crtube_text(CRTUBE_TEXT_IP_UNAVAILABLE));
    }

    scr_printf("\n");
#if CRTUBE_UI_HAS_YOUTUBE_TEST
    if (tests_completed == 1)
        scr_printf("%s\n", crtube_text(CRTUBE_TEXT_NETWORK_TESTS_DONE));
    else if (tests_completed == 2)
        scr_printf("%s\n", crtube_text(CRTUBE_TEXT_NETWORK_TESTS_DONE_NO_LOG));
    else
        scr_printf("%s\n", crtube_text(CRTUBE_TEXT_NETWORK_DIAGNOSTICS_HINT));
#else
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_NETWORK_BUILD_HINT));
#endif
    scr_printf("\n%s\n", crtube_text(CRTUBE_TEXT_BACK_HINT));
}

static void crtube_ui_render_about(void)
{
    crtube_screen_clear();
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_ABOUT_TITLE));
    scr_printf("========================================\n\n");
    scr_printf("%s\n\n", crtube_text(CRTUBE_TEXT_ABOUT_LINE_1));
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_ABOUT_LINE_2));
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_ABOUT_LINE_3));
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_ABOUT_LINE_4));
    scr_printf("%s\n\n", crtube_text(CRTUBE_TEXT_ABOUT_LINE_5));
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_BACK_HINT));
}

static int g_ui_network_result;
static const char *g_ui_network_ip;

static void crtube_ui_show_network_progress(crtube_text_id_t text_id)
{
    crtube_screen_clear();
    scr_printf("%s\n", crtube_text(CRTUBE_TEXT_NETWORK_TITLE));
    scr_printf("========================================\n\n");

    if (g_ui_network_result == CRTUBE_NETWORK_OK)
        scr_printf("%s\n", crtube_text(CRTUBE_TEXT_NETWORK_READY));
    else
        scr_printf(crtube_text(CRTUBE_TEXT_NETWORK_FAILED),
                   g_ui_network_result);

    if (g_ui_network_ip != NULL && g_ui_network_ip[0] != '\0')
        scr_printf(crtube_text(CRTUBE_TEXT_IP_LABEL), g_ui_network_ip);
    else
        scr_printf("%s\n", crtube_text(CRTUBE_TEXT_IP_UNAVAILABLE));

    scr_printf("\n%s\n", crtube_text(text_id));
}

static int crtube_ui_run_network_tests(int network_result,
                                       const char *ip_address)
{
#if CRTUBE_UI_HAS_YOUTUBE_TEST
    static int diagnostic_log_ready;
    int smb_result;

    g_ui_network_result = network_result;
    g_ui_network_ip = ip_address;
    crtube_network_set_progress_callback(crtube_ui_show_network_progress);
    crtube_ui_show_network_progress(
        CRTUBE_TEXT_NETWORK_PROGRESS_PREPARING_LOG);

    smb_result = crtube_smb_init();
    if (!diagnostic_log_ready && smb_result == 0 &&
        crtube_log_init(NULL) == 0) {
        diagnostic_log_ready = 1;
        crtube_log("CRTube startup\n");
        crtube_log("SMB share mounted\n");
    }

    if (diagnostic_log_ready)
        crtube_log("User requested network diagnostics from the UI.\n");
    crtube_network_log_diagnostics();
    crtube_network_run_diagnostics();
    crtube_network_log_diagnostics();
    (void)crtube_images_load_home_results();
    crtube_network_set_progress_callback(NULL);
    return diagnostic_log_ready ? 1 : 2;
#else
    (void)network_result;
    (void)ip_address;
    return 0;
#endif
}

void crtube_ui_run(int network_result, const char *ip_address)
{
    crtube_ui_screen_t screen = CRTUBE_UI_HOME;
    int selection = 0;
    int network_tests_completed = 0;

    if (crtube_screen_init() != 0)
        scr_printf("Graphics initialization failed; text-only mode.\n");
    crtube_screen_clear();
    crtube_ui_render_home(selection);
    crtube_screen_set_visible(1);

    for (;;) {
        unsigned short pressed;

        DelayThread(4000);
        if (crtube_controller_take_exit_request())
            crtube_app_return_to_osdsys();
        pressed = crtube_controller_take_pressed();

        if (screen == CRTUBE_UI_HOME) {
            if ((pressed & PAD_UP) != 0) {
                selection--;
                if (selection < 0)
                    selection = CRTUBE_MENU_ITEMS - 1;
                crtube_ui_render_home(selection);
            } else if ((pressed & PAD_DOWN) != 0) {
                selection++;
                if (selection >= CRTUBE_MENU_ITEMS)
                    selection = 0;
                crtube_ui_render_home(selection);
            } else if ((pressed & PAD_CROSS) != 0) {
                if (selection == 0) {
                    screen = CRTUBE_UI_SEARCH;
                    crtube_ui_render_search();
                } else if (selection == 1) {
                    screen = CRTUBE_UI_NETWORK;
                    crtube_ui_render_network(network_result, ip_address,
                                             network_tests_completed);
                } else {
                    screen = CRTUBE_UI_ABOUT;
                    crtube_ui_render_about();
                }
            }
        } else if (screen == CRTUBE_UI_NETWORK &&
                   (pressed & PAD_TRIANGLE) != 0) {
#if CRTUBE_UI_HAS_YOUTUBE_TEST
            network_tests_completed = crtube_ui_run_network_tests(
                network_result, ip_address);
            crtube_ui_render_network(network_result, ip_address,
                                     network_tests_completed);
#endif
        } else if ((pressed & PAD_CIRCLE) != 0) {
            screen = CRTUBE_UI_HOME;
            crtube_ui_render_home(selection);
        }
    }
}
