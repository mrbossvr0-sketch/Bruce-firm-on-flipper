#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/widget.h>
#include <stdlib.h>

// Bruce-inspired launcher. Hardware tools are intentionally marked as stubs.
typedef struct {
    Gui* gui;
    ViewDispatcher* dispatcher;
    Submenu* menu;
    Widget* info;
    uint32_t score;
} BruceApp;

enum { ViewMenu, ViewInfo };
enum { ItemInfrared, ItemSubghz, ItemNfc, ItemWifi, ItemGame, ItemAbout };

static uint32_t back_callback(void* context) {
    UNUSED(context);
    return ViewMenu;
}

static void menu_callback(void* context, uint32_t index) {
    BruceApp* app = context;
    widget_reset(app->info);
    const char* title = "Bruce Lite";
    const char* line1 = "";
    const char* line2 = "";
    switch(index) {
    case ItemInfrared:
        title = "Infrared";
        line1 = "IR tool placeholder";
        line2 = "No signal sent";
        break;
    case ItemSubghz:
        title = "Sub-GHz";
        line1 = "Radio tool placeholder";
        line2 = "No RF transmitted";
        break;
    case ItemNfc:
        title = "NFC";
        line1 = "NFC tool placeholder";
        line2 = "No card accessed";
        break;
    case ItemWifi:
        title = "Wi-Fi";
        line1 = "ESP32 board needed";
        line2 = "No Wi-Fi scan yet";
        break;
    case ItemGame:
        title = "Lucky Tap";
        app->score++;
        line1 = "You earned 1 point!";
        line2 = "Return to play again";
        break;
    default:
        title = "Bruce Lite";
        line1 = "Bruce-inspired demo";
        line2 = "Built for Flipper Zero";
        break;
    }
    widget_add_string_element(app->info, 64, 12, AlignCenter, AlignCenter, FontPrimary, title);
    widget_add_string_element(app->info, 64, 31, AlignCenter, AlignCenter, FontSecondary, line1);
    widget_add_string_element(app->info, 64, 45, AlignCenter, AlignCenter, FontSecondary, line2);
    view_dispatcher_switch_to_view(app->dispatcher, ViewInfo);
}

int32_t bruce_lite_app(void* p) {
    UNUSED(p);
    BruceApp* app = malloc(sizeof(BruceApp));
    if(!app) return -1;
    app->score = 0;
    app->gui = furi_record_open(RECORD_GUI);
    app->dispatcher = view_dispatcher_alloc();
    app->menu = submenu_alloc();
    app->info = widget_alloc();
    view_dispatcher_enable_queue(app->dispatcher);
    view_dispatcher_attach_to_gui(app->dispatcher, app->gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_add_view(app->dispatcher, ViewMenu, submenu_get_view(app->menu));
    view_dispatcher_add_view(app->dispatcher, ViewInfo, widget_get_view(app->info));
    view_set_previous_callback(widget_get_view(app->info), back_callback);
    submenu_add_item(app->menu, "Infrared", ItemInfrared, menu_callback, app);
    submenu_add_item(app->menu, "Sub-GHz", ItemSubghz, menu_callback, app);
    submenu_add_item(app->menu, "NFC", ItemNfc, menu_callback, app);
    submenu_add_item(app->menu, "Wi-Fi (ESP32)", ItemWifi, menu_callback, app);
    submenu_add_item(app->menu, "Lucky Tap Game", ItemGame, menu_callback, app);
    submenu_add_item(app->menu, "About", ItemAbout, menu_callback, app);
    view_dispatcher_switch_to_view(app->dispatcher, ViewMenu);
    view_dispatcher_run(app->dispatcher);
    view_dispatcher_remove_view(app->dispatcher, ViewInfo);
    view_dispatcher_remove_view(app->dispatcher, ViewMenu);
    widget_free(app->info);
    submenu_free(app->menu);
    view_dispatcher_free(app->dispatcher);
    furi_record_close(RECORD_GUI);
    free(app);
    return 0;
}
