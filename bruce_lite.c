#include <furi.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <input/input.h>
#include <stdlib.h>
#include <stdio.h>

// Bruce-inspired monochrome shell for the 128x64 Flipper Zero display.
// Hardware sections are clearly marked as demos, not implemented tools.
typedef enum {ScreenSplash, ScreenMenu, ScreenDetail, ScreenGame} Screen;
typedef struct {
    Gui* gui;
    ViewPort* viewport;
    FuriMessageQueue* queue;
    Screen screen;
    uint8_t selected;
    uint32_t score;
    bool running;
} BruceApp;

static const char* const items[] = {
    "Infrared", "Sub-GHz", "NFC", "Wi-Fi / ESP32", "Shark Tap", "About"
};
#define ITEM_COUNT 6

static void draw_shark(Canvas* c, int x, int y) {
    // Original line-art shark with fin, eye, gills and forked tail.
    canvas_draw_line(c, x+0, y+15, x+11, y+8);
    canvas_draw_line(c, x+11, y+8, x+27, y+8);
    canvas_draw_line(c, x+27, y+8, x+34, y+0);
    canvas_draw_line(c, x+34, y+0, x+38, y+9);
    canvas_draw_line(c, x+38, y+9, x+58, y+12);
    canvas_draw_line(c, x+58, y+12, x+66, y+8);
    canvas_draw_line(c, x+66, y+8, x+64, y+18);
    canvas_draw_line(c, x+64, y+18, x+66, y+26);
    canvas_draw_line(c, x+66, y+26, x+55, y+22);
    canvas_draw_line(c, x+55, y+22, x+39, y+25);
    canvas_draw_line(c, x+39, y+25, x+33, y+30);
    canvas_draw_line(c, x+33, y+30, x+28, y+24);
    canvas_draw_line(c, x+28, y+24, x+12, y+23);
    canvas_draw_line(c, x+12, y+23, x+0, y+15);
    canvas_draw_disc(c, x+14, y+13, 2);
    canvas_draw_line(c, x+21, y+16, x+21, y+21);
    canvas_draw_line(c, x+24, y+16, x+24, y+21);
    canvas_draw_line(c, x+27, y+16, x+27, y+21);
    canvas_draw_line(c, x+3, y+18, x+15, y+19);
}

static void draw_cb(Canvas* c, void* context) {
    BruceApp* a = context;
    canvas_clear(c);
    canvas_set_color(c, ColorBlack);
    if(a->screen == ScreenSplash) {
        canvas_draw_frame(c, 0, 0, 128, 64);
        draw_shark(c, 30, 3);
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 44, AlignCenter, AlignCenter, "BRUCE LITE");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 57, AlignCenter, AlignCenter, "OK to dive in");
        return;
    }
    canvas_draw_box(c, 0, 0, 128, 13);
    canvas_set_color(c, ColorWhite);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 4, 10, "BRUCE / FLIPPER");
    canvas_draw_str(c, 108, 10, "v1");
    canvas_set_color(c, ColorBlack);

    if(a->screen == ScreenMenu) {
        canvas_set_font(c, FontSecondary);
        for(int row = 0; row < 3; row++) {
            int index = (int)a->selected + row - 1;
            if(index < 0 || index >= ITEM_COUNT) continue;
            int y = 17 + row*15;
            if(index == a->selected) {
                canvas_draw_box(c, 2, y, 124, 14);
                canvas_set_color(c, ColorWhite);
                canvas_draw_str(c, 7, y+11, ">");
                canvas_draw_str(c, 20, y+11, items[index]);
                canvas_set_color(c, ColorBlack);
            } else {
                canvas_draw_str(c, 20, y+11, items[index]);
            }
        }
        canvas_draw_str(c, 3, 63, "UP/DOWN   OK select");
    } else if(a->screen == ScreenGame) {
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "SCORE: %lu", (unsigned long)a->score);
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 30, AlignCenter, AlignCenter, "SHARK TAP");
        canvas_set_font(c, FontSecondary);
        canvas_draw_str_aligned(c, 64, 44, AlignCenter, AlignCenter, buffer);
        canvas_draw_str(c, 2, 62, "OK +1   BACK menu");
    } else {
        canvas_set_font(c, FontPrimary);
        canvas_draw_str_aligned(c, 64, 25, AlignCenter, AlignCenter, items[a->selected]);
        canvas_set_font(c, FontSecondary);
        const char* msg1 = "Demo screen only";
        const char* msg2 = "No hardware action";
        if(a->selected == 3) {
            msg1 = "ESP32 board needed";
            msg2 = "Wi-Fi not connected";
        } else if(a->selected == 5) {
            msg1 = "Bruce-inspired UI";
            msg2 = "Flipper Zero edition";
        }
        canvas_draw_str_aligned(c, 64, 41, AlignCenter, AlignCenter, msg1);
        canvas_draw_str_aligned(c, 64, 52, AlignCenter, AlignCenter, msg2);
        canvas_draw_str(c, 2, 63, "BACK to menu");
    }
}

static void input_cb(InputEvent* event, void* context) {
    BruceApp* a = context;
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        furi_message_queue_put(a->queue, event, 0);
    }
}

int32_t bruce_lite_app(void* p) {
    UNUSED(p);
    BruceApp* a = malloc(sizeof(BruceApp));
    if(!a) return -1;
    a->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    a->viewport = view_port_alloc();
    if(!a->queue || !a->viewport) {
        if(a->viewport) view_port_free(a->viewport);
        if(a->queue) furi_message_queue_free(a->queue);
        free(a);
        return -1;
    }
    a->selected = 0;
    a->score = 0;
    a->screen = ScreenSplash;
    a->running = true;
    view_port_draw_callback_set(a->viewport, draw_cb, a);
    view_port_input_callback_set(a->viewport, input_cb, a);
    a->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(a->gui, a->viewport, GuiLayerFullscreen);
    view_port_update(a->viewport);
    InputEvent event;
    while(a->running) {
        if(furi_message_queue_get(a->queue, &event, FuriWaitForever) != FuriStatusOk) continue;
        if(a->screen == ScreenSplash) {
            if(event.key == InputKeyOk || event.key == InputKeyBack) a->screen = ScreenMenu;
        } else if(a->screen == ScreenMenu) {
            if(event.key == InputKeyUp && a->selected > 0) a->selected--;
            else if(event.key == InputKeyDown && a->selected < ITEM_COUNT-1) a->selected++;
            else if(event.key == InputKeyOk)
                a->screen = (a->selected == 4) ? ScreenGame : ScreenDetail;
            else if(event.key == InputKeyBack) a->running = false;
        } else {
            if(event.key == InputKeyBack) a->screen = ScreenMenu;
            else if(a->screen == ScreenGame && event.key == InputKeyOk) a->score++;
        }
        view_port_update(a->viewport);
    }
    gui_remove_view_port(a->gui, a->viewport);
    furi_record_close(RECORD_GUI);
    view_port_free(a->viewport);
    furi_message_queue_free(a->queue);
    free(a);
    return 0;
}
