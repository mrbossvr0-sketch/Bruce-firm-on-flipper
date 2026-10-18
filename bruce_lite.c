#include <furi.h>
#include <furi_hal.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <gui/gui.h>
#include <gui/view_port.h>
#include <gui/canvas.h>
#include <input/input.h>
#include <dialogs/dialogs.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <infrared.h>
#include <infrared_worker.h>
#include <nfc/nfc.h>
#include <nfc/nfc_scanner.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// Bruce Zero — original Bruce-inspired 128x64 UI. Does NOT replace Flipper firmware.
// IR: plays first signal in a user-chosen .ir file.
// RF: hardware RSSI receiver, receive-only.
// NFC: real supported-protocol card presence detection.
// Wi-Fi: ESP-AT UART scanner requiring an external ESP32 running ESP-AT firmware.

typedef enum {ScreenSplash, ScreenHome, ScreenInfrared, ScreenRadio, ScreenNfc,
              ScreenWifi, ScreenGame, ScreenAbout} Screen;

typedef struct {
    Gui* gui;
    ViewPort* viewport;
    FuriMessageQueue* input_queue;
    Screen screen;
    uint8_t selected;
    uint8_t frequency_index;
    bool radio_active;
    InfraredWorker* ir_worker;
    bool ir_learning;
    volatile bool ir_captured;
    bool ir_last_valid;
    InfraredMessage ir_last;
    int16_t rssi;
    Nfc* nfc;
    NfcScanner* nfc_scanner;
    volatile bool nfc_found;
    volatile uint8_t nfc_protocol_count;
    FuriHalSerialHandle* serial;
    volatile uint16_t wifi_len;
    char wifi_buf[320];
    bool wifi_busy;
    bool wifi_ready;
    uint32_t wifi_started;
    char wifi_name[32];
    char notice[38];
    bool running;
    uint32_t score;
    uint8_t shark_x;
    uint8_t target_x;
} BruceZero;

static const char* const labels[] = {
    "Infrared replay", "Sub-GHz meter", "NFC detector", "Wi-Fi scan", "Shark game", "About"
};
static const uint32_t frequencies[] = {315000000, 433920000, 868350000};
static const char* const frequencies_short[] = {"315.00", "433.92", "868.35"};

static void shark(Canvas* c, int x, int y) {
    // Original tiny polygon shark (not a trademark/logo copy).
    canvas_draw_line(c,x,y+12,x+11,y+6);
    canvas_draw_line(c,x+11,y+6,x+27,y+6);
    canvas_draw_line(c,x+27,y+6,x+34,y);
    canvas_draw_line(c,x+34,y,x+37,y+7);
    canvas_draw_line(c,x+37,y+7,x+55,y+10);
    canvas_draw_line(c,x+55,y+10,x+65,y+5);
    canvas_draw_line(c,x+65,y+5,x+62,y+14);
    canvas_draw_line(c,x+62,y+14,x+65,y+23);
    canvas_draw_line(c,x+65,y+23,x+54,y+18);
    canvas_draw_line(c,x+54,y+18,x+37,y+22);
    canvas_draw_line(c,x+37,y+22,x+31,y+27);
    canvas_draw_line(c,x+31,y+27,x+27,y+21);
    canvas_draw_line(c,x+27,y+21,x+10,y+19);
    canvas_draw_line(c,x+10,y+19,x,y+12);
    canvas_draw_disc(c,x+13,y+11,1);
    canvas_draw_line(c,x+20,y+13,x+20,y+17);
    canvas_draw_line(c,x+23,y+13,x+23,y+17);
    canvas_draw_line(c,x+26,y+13,x+26,y+17);
}

static void header(Canvas* c, const char* title) {
    canvas_set_color(c,ColorBlack);
    canvas_draw_box(c,0,0,128,13);
    canvas_set_color(c,ColorWhite);
    canvas_set_font(c,FontSecondary);
    canvas_draw_str(c,4,10,"BRUCE/ZERO");
    canvas_draw_str_aligned(c,124,10,AlignRight,AlignBottom,title);
    canvas_set_color(c,ColorBlack);
}

static void footer(Canvas* c, const char* instruction) {
    canvas_draw_line(c,0,53,127,53);
    canvas_set_font(c,FontSecondary);
    canvas_draw_str(c,3,62,instruction);
}

static void draw_cb(Canvas* c, void* context) {
    BruceZero* a=context;
    canvas_clear(c);
    canvas_set_color(c,ColorBlack);
    if(a->screen==ScreenSplash) {
        canvas_draw_frame(c,1,1,126,62);
        shark(c,31,3);
        canvas_set_font(c,FontPrimary);
        canvas_draw_str_aligned(c,64,42,AlignCenter,AlignCenter,"BRUCE ZERO");
        canvas_set_font(c,FontSecondary);
        canvas_draw_str_aligned(c,64,58,AlignCenter,AlignCenter,"PRESS OK TO START");
        return;
    }
    if(a->screen==ScreenHome) {
        header(c,"MENU");
        // Three entries in fixed 13px rows, separate from 53px footer.
        for(int row=0;row<3;row++) {
            int idx=(int)a->selected+row-1;
            if(idx<0||idx>=6) continue;
            int y=14+row*13;
            if(idx==a->selected) {
                canvas_draw_box(c,3,y,122,13);
                canvas_set_color(c,ColorWhite);
            }
            canvas_set_font(c,FontSecondary);
            canvas_draw_str(c,8,y+10,idx==a->selected?">":" ");
            canvas_draw_str(c,21,y+10,labels[idx]);
            if(idx==a->selected)canvas_set_color(c,ColorBlack);
        }
        footer(c,"UP/DOWN    OK:ENTER");
        return;
    }
    if(a->screen==ScreenInfrared) {
        header(c,"IR");
        canvas_set_font(c,FontPrimary);
        canvas_draw_str(c,6,27,"Saved IR replay");
        canvas_set_font(c,FontSecondary);
        canvas_draw_str(c,6,40,a->ir_learning?"Aim remote at receiver":
            (a->ir_last_valid?"Learned IR ready to send":"Learn or use a saved .ir"));
        footer(c,"L:LEARN OK:FILE R:TX");
    } else if(a->screen==ScreenRadio) {
        header(c,"RF RX");
        canvas_set_font(c,FontSecondary);
        canvas_draw_str(c,5,23,"Frequency (MHz):");
        canvas_set_font(c,FontPrimary);
        canvas_draw_str(c,5,38,frequencies_short[a->frequency_index]);
        char buf[24];
        if(a->radio_active) snprintf(buf,sizeof(buf),"%d dBm",(int)a->rssi);
        else snprintf(buf,sizeof(buf),"PRESS OK");
        canvas_draw_str_aligned(c,123,38,AlignRight,AlignBottom,buf);
        footer(c,"L/R:FREQ   OK:RX");
    } else if(a->screen==ScreenNfc) {
        header(c,"NFC");
        canvas_set_font(c,FontPrimary);
        canvas_draw_str_aligned(c,64,28,AlignCenter,AlignCenter,
            a->nfc_found?"CARD DETECTED!":"Scanning for card");
        canvas_set_font(c,FontSecondary);
        if(a->nfc_found) {
            char buf[24];
            snprintf(buf,sizeof(buf),"Protocols: %u",(unsigned)a->nfc_protocol_count);
            canvas_draw_str_aligned(c,64,43,AlignCenter,AlignCenter,buf);
        } else canvas_draw_str_aligned(c,64,43,AlignCenter,AlignCenter,"Hold tag near Flipper");
        footer(c,"BACK:STOP SCAN");
    } else if(a->screen==ScreenWifi) {
        header(c,"ESP-AT");
        canvas_set_font(c,FontPrimary);
        canvas_draw_str_aligned(c,64,26,AlignCenter,AlignCenter,"Wi-Fi scanner");
        canvas_set_font(c,FontSecondary);
        canvas_draw_str_aligned(c,64,41,AlignCenter,AlignCenter,
            a->wifi_busy?"Scanning...":(a->wifi_name[0]?a->wifi_name:(a->wifi_ready?"ESP32 responding":"ESP32 AT required")));
        footer(c,"OK:SCAN  BACK:MENU");
    } else if(a->screen==ScreenGame) {
        header(c,"GAME");
        char buf[25];
        snprintf(buf,sizeof(buf),"SCORE %lu",(unsigned long)a->score);
        canvas_set_font(c,FontSecondary);
        canvas_draw_str_aligned(c,64,22,AlignCenter,AlignCenter,buf);
        canvas_draw_box(c,a->target_x,37,6,6);
        shark(c,a->shark_x,23);
        footer(c,"LEFT/RIGHT   OK:CATCH");
    } else {
        header(c,"INFO");
        canvas_set_font(c,FontPrimary);
        canvas_draw_str_aligned(c,64,28,AlignCenter,AlignCenter,"BRUCE ZERO 2");
        canvas_set_font(c,FontSecondary);
        canvas_draw_str_aligned(c,64,41,AlignCenter,AlignCenter,"Flipper-native toolkit");
        footer(c,"BACK:MENU");
    }
    // Short action feedback only on screens where it won't obscure a live measurement.
    if(a->notice[0] && a->screen==ScreenInfrared) {
        canvas_draw_box(c,0,14,128,39);
        canvas_set_color(c,ColorWhite);
        canvas_set_font(c,FontSecondary);
        canvas_draw_str_aligned(c,64,36,AlignCenter,AlignCenter,a->notice);
        canvas_set_color(c,ColorBlack);
    }
}

static void input_cb(InputEvent* e,void* context) {
    BruceZero* a=context;
    if(e->type==InputTypeShort||e->type==InputTypeRepeat) {
        furi_message_queue_put(a->input_queue,e,0);
    }
}

static void ir_rx_callback(void* context, InfraredWorkerSignal* signal) {
    BruceZero* a=context;
    if(!a->ir_captured && infrared_worker_signal_is_decoded(signal)) {
        const InfraredMessage* received=infrared_worker_get_decoded_signal(signal);
        if(received) {
            a->ir_last=*received;
            a->ir_last_valid=true;
            a->ir_captured=true;
        }
    }
}
static void stop_ir(BruceZero* a) {
    if(a->ir_worker) {
        infrared_worker_rx_stop(a->ir_worker);
        infrared_worker_free(a->ir_worker);
        a->ir_worker=NULL;
    }
    a->ir_learning=false;
}
static void start_ir(BruceZero* a) {
    stop_ir(a);
    a->ir_captured=false;
    a->notice[0]=0;
    a->ir_worker=infrared_worker_alloc();
    if(!a->ir_worker) {
        snprintf(a->notice,sizeof(a->notice),"IR busy");
        return;
    }
    infrared_worker_rx_enable_signal_decoding(a->ir_worker,true);
    infrared_worker_rx_set_received_signal_callback(a->ir_worker,ir_rx_callback,a);
    infrared_worker_rx_start(a->ir_worker);
    a->ir_learning=true;
}
static void stop_radio(BruceZero* a) {
    if(!a->radio_active)return;
    furi_hal_subghz_idle();
    furi_hal_subghz_set_path(FuriHalSubGhzPathIsolate);
    furi_hal_subghz_sleep();
    a->radio_active=false;
}

static void start_radio(BruceZero* a) {
    // CC1101 configuration for receive-only OOK RSSI monitoring.
    // Register sequence + terminator + PA table (not used for TX).
    static const uint8_t preset[]={
        0x02,0x0D,0x03,0x07,0x08,0x32,0x0B,0x06,
        0x14,0x00,0x13,0x00,0x12,0x30,0x11,0x32,
        0x10,0x17,0x18,0x18,0x19,0x18,0x1D,0x91,
        0x1C,0x00,0x1B,0x07,0x20,0xFB,0x22,0x11,
        0x21,0xB6,0x00,0x00,0x00,0xC0,0x00,0x00,0x00,0x00,0x00,0x00
    };
    if(!furi_hal_subghz_is_frequency_valid(frequencies[a->frequency_index])) return;
    furi_hal_subghz_reset();
    furi_hal_subghz_load_custom_preset(preset);
    furi_hal_subghz_set_frequency_and_path(frequencies[a->frequency_index]);
    furi_hal_subghz_rx();
    a->radio_active=true;
}

static void nfc_callback(NfcScannerEvent e,void* context) {
    BruceZero* a=context;
    if(e.type==NfcScannerEventTypeDetected) {
        a->nfc_found=true;
        a->nfc_protocol_count=(uint8_t)(e.data.protocol_num>255?255:e.data.protocol_num);
    }
}

static void stop_nfc(BruceZero* a) {
    if(a->nfc_scanner) {
        nfc_scanner_stop(a->nfc_scanner);
        nfc_scanner_free(a->nfc_scanner);
        a->nfc_scanner=NULL;
    }
    if(a->nfc) {nfc_free(a->nfc);a->nfc=NULL;}
}
static void start_nfc(BruceZero* a) {
    a->nfc_found=false;
    a->nfc_protocol_count=0;
    a->nfc=nfc_alloc();
    if(!a->nfc)return;
    a->nfc_scanner=nfc_scanner_alloc(a->nfc);
    if(a->nfc_scanner)nfc_scanner_start(a->nfc_scanner,nfc_callback,a);
}

// UART RX interrupt collects the ESP-AT responses. Never update GUI inside ISR.
static void serial_rx(FuriHalSerialHandle* handle,FuriHalSerialRxEvent event,void* context) {
    BruceZero* a=context;
    if(event & FuriHalSerialRxEventData) {
        while(furi_hal_serial_async_rx_available(handle)) {
            uint8_t ch=furi_hal_serial_async_rx(handle);
            uint16_t pos=a->wifi_len;
            if(pos < sizeof(a->wifi_buf)-1) {
                a->wifi_buf[pos]=(char)ch;
                a->wifi_len=pos+1;
                a->wifi_buf[pos+1]=0;
            }
        }
    }
}
static bool wifi_open(BruceZero* a) {
    if(a->serial)return true;
    a->serial=furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!a->serial)return false;
    furi_hal_serial_init(a->serial,115200);
    furi_hal_serial_async_rx_start(a->serial,serial_rx,a,false);
    return true;
}
static void wifi_close(BruceZero* a) {
    if(!a->serial)return;
    furi_hal_serial_async_rx_stop(a->serial);
    furi_hal_serial_deinit(a->serial);
    furi_hal_serial_control_release(a->serial);
    a->serial=NULL;
    a->wifi_busy=false;
}
static void wifi_scan(BruceZero* a) {
    a->wifi_name[0]=0;
    a->wifi_len=0;
    a->wifi_buf[0]=0;
    a->wifi_ready=false;
    if(!wifi_open(a)) {
        snprintf(a->wifi_name,sizeof(a->wifi_name),"UART busy");
        return;
    }
    // Compatible with boards flashed with Espressif ESP-AT, not Marauder/Bruce.
    static const char mode_cmd[]="AT+CWMODE=1\r\n";
    static const char scan_cmd[]="AT+CWLAP\r\n";
    furi_hal_serial_tx(a->serial,(const uint8_t*)mode_cmd,sizeof(mode_cmd)-1);
    furi_delay_ms(150);
    furi_hal_serial_tx(a->serial,(const uint8_t*)scan_cmd,sizeof(scan_cmd)-1);
    a->wifi_started=furi_get_tick();
    a->wifi_busy=true;
}
static void wifi_update(BruceZero* a) {
    if(!a->wifi_busy)return;
    // Never parse the ISR-owned buffer until the command has completed.
    if((furi_get_tick()-a->wifi_started)<furi_ms_to_ticks(5500))return;
    wifi_close(a);
    if(strstr(a->wifi_buf,"OK"))a->wifi_ready=true;
    // +CWLAP:(ecn,"SSID",rssi,...); only show first network name.
    const char* p=strstr(a->wifi_buf,"+CWLAP:");
    if(p) {
        const char* q=strchr(p,'"');
        if(q) {
            q++;
            size_t len=0;
            while(q[len]&&q[len]!='"'&&len<sizeof(a->wifi_name)-1)len++;
            memcpy(a->wifi_name,q,len);
            a->wifi_name[len]=0;
        }
    }
    if(!a->wifi_name[0])snprintf(a->wifi_name,sizeof(a->wifi_name),
        a->wifi_ready?"No networks returned":"No ESP-AT response");
}

static void infrared_replay(BruceZero* a) {
    a->notice[0]=0;
    DialogsApp* d=furi_record_open(RECORD_DIALOGS);
    Storage* s=furi_record_open(RECORD_STORAGE);
    FuriString* path=furi_string_alloc_set_str("/ext/infrared");
    FuriString* selected=furi_string_alloc();
    DialogsFileBrowserOptions opts;
    dialog_file_browser_set_basic_options(&opts,".ir",NULL);
    opts.base_path="/ext/infrared";
    bool chosen=dialog_file_browser_show(d,selected,path,&opts);
    if(chosen) {
        FlipperFormat* ff=flipper_format_file_alloc(s);
        FuriString* name=furi_string_alloc();
        FuriString* type=furi_string_alloc();
        FuriString* protocol=furi_string_alloc();
        if(flipper_format_file_open_existing(ff,furi_string_get_cstr(selected))) {
            FuriString* header_type=furi_string_alloc();
            uint32_t version=0;
            bool ok=flipper_format_read_header(ff,header_type,&version) && version==1;
            if(ok)ok=flipper_format_read_string(ff,"name",name);
            if(ok)ok=flipper_format_read_string(ff,"type",type);
            if(ok && !strcmp(furi_string_get_cstr(type),"parsed")) {
                ok=flipper_format_read_string(ff,"protocol",protocol);
                uint8_t address[4]={0}, command[4]={0};
                if(ok)ok=flipper_format_read_hex(ff,"address",address,4);
                if(ok)ok=flipper_format_read_hex(ff,"command",command,4);
                if(ok) {
                    InfraredProtocol pr=infrared_get_protocol_by_name(furi_string_get_cstr(protocol));
                    if(infrared_is_protocol_valid(pr)) {
                        InfraredMessage msg={.protocol=pr,.address=0,.command=0,.repeat=false};
                        for(size_t i=0;i<4;i++) {
                            msg.address|=((uint32_t)address[i])<<(8*i);
                            msg.command|=((uint32_t)command[i])<<(8*i);
                        }
                        infrared_send(&msg,1);
                    } else ok=false;
                }
            } else if(ok && !strcmp(furi_string_get_cstr(type),"raw")) {
                uint32_t frequency=0;
                float duty_cycle=0;
                uint32_t count=0;
                ok=flipper_format_read_uint32(ff,"frequency",&frequency,1);
                if(ok)ok=flipper_format_read_float(ff,"duty_cycle",&duty_cycle,1);
                if(ok)ok=flipper_format_get_value_count(ff,"data",&count);
                if(ok && count>=2 && count<=1024 && frequency>=10000 && frequency<=1000000 &&
                    duty_cycle>0.0f && duty_cycle<1.0f) {
                    uint32_t* timings=malloc(count*sizeof(uint32_t));
                    if(timings) {
                        ok=flipper_format_read_uint32(ff,"data",timings,(uint16_t)count);
                        if(ok)infrared_send_raw_ext(timings,count,true,frequency,duty_cycle);
                        free(timings);
                    } else ok=false;
                } else ok=false;
            } else ok=false;
            if(ok)snprintf(a->notice,sizeof(a->notice),"IR sent: %.22s",furi_string_get_cstr(name));
            else snprintf(a->notice,sizeof(a->notice),"Bad IR file / button");
            furi_string_free(header_type);
        } else snprintf(a->notice,sizeof(a->notice),"Can't open IR file");
        flipper_format_free(ff);
        furi_string_free(protocol);
        furi_string_free(type);
        furi_string_free(name);
    }
    furi_string_free(path);
    furi_string_free(selected);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
}

static void enter_screen(BruceZero* a,Screen screen) {
    a->notice[0]=0;
    a->screen=screen;
    if(screen==ScreenNfc)start_nfc(a);
}
static void home(BruceZero* a) {
    if(a->screen==ScreenInfrared)stop_ir(a);
    if(a->screen==ScreenRadio)stop_radio(a);
    if(a->screen==ScreenNfc)stop_nfc(a);
    if(a->screen==ScreenWifi)wifi_close(a);
    a->screen=ScreenHome;
    a->notice[0]=0;
}

int32_t bruce_lite_app(void* p) {
    UNUSED(p);
    BruceZero* a=malloc(sizeof(BruceZero));
    if(!a)return -1;
    memset(a,0,sizeof(BruceZero));
    a->rssi=-100;
    a->running=true;
    a->screen=ScreenSplash;
    a->shark_x=0;
    a->target_x=85;
    a->input_queue=furi_message_queue_alloc(8,sizeof(InputEvent));
    a->viewport=view_port_alloc();
    if(!a->input_queue || !a->viewport) {
        if(a->viewport)view_port_free(a->viewport);
        if(a->input_queue)furi_message_queue_free(a->input_queue);
        free(a);
        return -1;
    }
    view_port_draw_callback_set(a->viewport,draw_cb,a);
    view_port_input_callback_set(a->viewport,input_cb,a);
    a->gui=furi_record_open(RECORD_GUI);
    gui_add_view_port(a->gui,a->viewport,GuiLayerFullscreen);
    view_port_update(a->viewport);
    while(a->running) {
        InputEvent event;
        FuriStatus status=furi_message_queue_get(a->input_queue,&event,furi_ms_to_ticks(170));
        if(status==FuriStatusOk) {
            if(a->screen==ScreenSplash) {
                if(event.key==InputKeyOk) a->screen=ScreenHome;
                else if(event.key==InputKeyBack)a->running=false;
            } else if(a->screen==ScreenHome) {
                if(event.key==InputKeyUp&&a->selected>0)a->selected--;
                else if(event.key==InputKeyDown&&a->selected<5)a->selected++;
                else if(event.key==InputKeyBack)a->running=false;
                else if(event.key==InputKeyOk) {
                    static const Screen screens[]={ScreenInfrared,ScreenRadio,ScreenNfc,
                        ScreenWifi,ScreenGame,ScreenAbout};
                    enter_screen(a,screens[a->selected]);
                }
            } else if(event.key==InputKeyBack) home(a);
            else if(a->screen==ScreenInfrared) {
                if(event.key==InputKeyLeft)start_ir(a);
                else if(event.key==InputKeyRight) {
                    stop_ir(a);
                    if(a->ir_last_valid) {
                        infrared_send(&a->ir_last,1);
                        snprintf(a->notice,sizeof(a->notice),"Learned IR transmitted");
                    } else snprintf(a->notice,sizeof(a->notice),"Learn a code first");
                } else if(event.key==InputKeyOk) {
                    stop_ir(a);
                    infrared_replay(a);
                }
            }
            else if(a->screen==ScreenRadio) {
                if(event.key==InputKeyLeft || event.key==InputKeyRight) {
                    stop_radio(a);
                    if(event.key==InputKeyLeft)
                        a->frequency_index=(a->frequency_index+2)%3;
                    else a->frequency_index=(a->frequency_index+1)%3;
                } else if(event.key==InputKeyOk) {
                    if(a->radio_active)stop_radio(a);else start_radio(a);
                }
            } else if(a->screen==ScreenWifi && event.key==InputKeyOk && !a->wifi_busy) wifi_scan(a);
            else if(a->screen==ScreenGame) {
                if(event.key==InputKeyLeft&&a->shark_x>=4)a->shark_x-=4;
                else if(event.key==InputKeyRight&&a->shark_x<57)a->shark_x+=4;
                else if(event.key==InputKeyOk) {
                    // The shark's nose at x and target within the drawn body.
                    int delta=(int)a->shark_x+35-(int)a->target_x;
                    if(delta<0)delta=-delta;
                    if(delta<13)a->score++;
                    a->target_x=(uint8_t)(30+((a->score*31+a->target_x*7+17)%62));
                }
            }
        }
        if(a->ir_captured && a->ir_learning) {
            stop_ir(a);
            snprintf(a->notice,sizeof(a->notice),"IR captured! R to TX");
        }
        if(a->radio_active) a->rssi=(int16_t)furi_hal_subghz_get_rssi();
        if(a->screen==ScreenWifi)wifi_update(a);
        view_port_update(a->viewport);
    }
    stop_ir(a);
    stop_radio(a);
    stop_nfc(a);
    wifi_close(a);
    gui_remove_view_port(a->gui,a->viewport);
    furi_record_close(RECORD_GUI);
    view_port_free(a->viewport);
    furi_message_queue_free(a->input_queue);
    free(a);
    return 0;
}
