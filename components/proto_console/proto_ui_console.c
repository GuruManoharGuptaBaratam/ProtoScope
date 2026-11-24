#include "proto_ui_console.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_console.h"
#include "proto_display.h"
#include "proto_encoder.h"
#include "proto_ui.h"

void proto_ui_console_print_status(void)
{
    const proto_display_info_t d = proto_display_info();
    const proto_ui_runtime_stats_t u = proto_ui_runtime_stats();
    const proto_encoder_stats_t e = proto_encoder_stats();
    printf("display_active=%s type=%s i2c=0x%02X width=%u height=%u flipped=%s flushes=%" PRIu32
           " flush_errors=%" PRIu32 " starts=%" PRIu32 "\n", d.active ? "yes" : "no", d.name ? d.name : "none",
           (unsigned)d.i2c_address, (unsigned)d.width, (unsigned)d.height, d.flipped ? "yes" : "no",
           d.flushes, d.flush_errors, d.starts);
    printf("ui_started=%s screen=%s inputs=%" PRIu32 " frames=%" PRIu32 " last_frame_ms=%" PRIu32
           " max_frame_ms=%" PRIu32 "\n", u.started ? "yes" : "no", proto_ui_screen_name(u.screen),
           u.inputs, u.frames, u.last_frame_ms, u.max_frame_ms);
    printf("encoder_started=%s detents=%" PRIu32 " presses=%" PRIu32 " long_presses=%" PRIu32 " invert=%s"
           " steps_per_detent=%u\n", e.started ? "yes" : "no", e.detents, e.presses, e.long_presses,
           e.invert ? "on" : "off", (unsigned)e.steps_per_detent);
    puts("ui_status_end");
}

static int display_command(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        proto_ui_console_print_status();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "start") == 0) {
        const esp_err_t err = proto_ui_runtime_display_start();
        if (err != ESP_OK) {
            printf("display_error=%s (is the OLED wired: SDA GPIO14, SCL GPIO10, VCC 3V3, GND?)\n", esp_err_to_name(err));
            return 1;
        }
        const proto_display_info_t d = proto_display_info();
        printf("display_start=ok type=%s i2c=0x%02X width=%u height=%u\n", d.name, (unsigned)d.i2c_address,
               (unsigned)d.width, (unsigned)d.height);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "test") == 0) {
        proto_ui_runtime_test_pattern();
        puts("display_test=shown");
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "flip") == 0) {
        const esp_err_t err = proto_display_flip(strcmp(argv[2], "on") == 0);
        if (err != ESP_OK) { printf("display_error=%s\n", esp_err_to_name(err)); return 1; }
        puts("display_flip=ok");
        return 0;
    }
    puts("Usage: display start | display status | display test | display flip on|off");
    puts("OLED 128x64 SSD1306 on I2C: SDA GPIO14, SCL GPIO10. 'flip' rotates the picture 180 degrees.");
    return 1;
}

static int ui_command(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "status") == 0) {
        proto_ui_console_print_status();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "screen") == 0) {
        char rows[10][32];
        const size_t n = proto_ui_runtime_read_screen(rows, 10);
        for (size_t i = 0; i < n; ++i) { printf("ui_row%u=|%s|\n", (unsigned)i, rows[i]); }
        puts("ui_screen_end");
        return 0;
    }
    if (argc >= 3 && argc <= 4 && strcmp(argv[1], "press") == 0) {
        proto_ui_input_t in;
        if (strcmp(argv[2], "cw") == 0) { in = PROTO_UI_IN_CW; }
        else if (strcmp(argv[2], "ccw") == 0) { in = PROTO_UI_IN_CCW; }
        else if (strcmp(argv[2], "short") == 0) { in = PROTO_UI_IN_SHORT; }
        else if (strcmp(argv[2], "long") == 0) { in = PROTO_UI_IN_LONG; }
        else { puts("ui_error=bad_input"); return 1; }
        const int count = argc == 4 ? atoi(argv[3]) : 1;
        if (count < 1 || count > 15) { puts("ui_error=bad_count"); return 1; }
        for (int i = 0; i < count; ++i) {
            if (!proto_ui_runtime_post_input(in)) { puts("ui_error=queue_full"); return 1; }
        }
        printf("ui_press=ok input=%s count=%d\n", argv[2], count);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "encoder") == 0) {
        int a = -1, b = -1, sw = -1;
        proto_encoder_read_levels(&a, &b, &sw);
        const proto_encoder_stats_t e = proto_encoder_stats();
        printf("encoder_raw a(GPIO1,CLK)=%d b(GPIO2,DT)=%d sw(GPIO6)=%d   (rest = 1 1 1, pressed switch = 0)\n", a, b, sw);
        printf("encoder_counts edges=%" PRIu32 " valid_transitions=%" PRIu32 " missed_edges=%" PRIu32
               " detents=%" PRIu32 " steps_per_detent=%u\n", e.isr_edges, e.valid_transitions, e.missed_edges,
               e.detents, (unsigned)e.steps_per_detent);
        puts("Turn the knob slowly and run 'ui encoder' again: edges must rise. edges=0 means no signal reaches GPIO1/GPIO2.");
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "encoder") == 0 && strcmp(argv[2], "steps") == 0) {
        const int n = atoi(argv[3]);
        if (n < 1 || n > 4) { puts("ui_error=steps_must_be_1_to_4"); return 1; }
        proto_encoder_set_steps((uint8_t)n);
        printf("ui_encoder_steps=%d\n", n);
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "invert") == 0 && (strcmp(argv[2], "on") == 0 || strcmp(argv[2], "off") == 0)) {
        proto_encoder_set_invert(strcmp(argv[2], "on") == 0);
        printf("ui_invert=%s\n", argv[2]);
        return 0;
    }
    puts("Usage: ui status | ui screen | ui press cw|ccw|short|long [count] | ui invert on|off");
    puts("       ui encoder            (live GPIO1/GPIO2/GPIO6 levels and edge counters)");
    puts("       ui encoder steps 1-4  (signal changes per click: 4 for most knobs, 2 for 30-click knobs)");
    puts("'ui press' injects an encoder input; 'ui screen' prints the text of the current screen.");
    return 1;
}

esp_err_t proto_ui_console_register(void)
{
    esp_err_t err = proto_ui_runtime_start();
    if (err != ESP_OK) { return err; }
    const esp_console_cmd_t display = {
        .command = "display",
        .help = "Phase 9 OLED display: start, status, test, flip on|off",
        .func = display_command,
    };
    err = esp_console_cmd_register(&display);
    if (err != ESP_OK) { return err; }
    const esp_console_cmd_t ui = {
        .command = "ui",
        .help = "Phase 9 UI: status, screen, press cw|ccw|short|long, invert on|off",
        .func = ui_command,
    };
    return esp_console_cmd_register(&ui);
}
