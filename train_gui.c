#include "raylib.h"
#include <time.h>

/* 数据模型与链表操作已拆到独立模块，GUI 只包含头文件。 */
#include "train_model.h"

#define WINDOW_WIDTH 1180
#define WINDOW_HEIGHT 720
#define PANEL_X 300
#define FIELD_COUNT 7
#define CONTENT_WIDTH 650
#define CONTENT_X (PANEL_X + ((WINDOW_WIDTH - PANEL_X - CONTENT_WIDTH) / 2))
/* 存档路径由模型层统一提供，控制台版与 GUI 版共用同一份数据 */
#define DATA_FILE default_data_file()

static const Color CNR_RED = {54, 126, 190, 255};
static const Color CNR_DARK_RED = {35, 92, 151, 255};
static const Color CNR_BACKGROUND = {241, 247, 252, 255};
static const Color CNR_TEXT = {24, 39, 54, 255};
static const Color CNR_MUTED = {91, 112, 132, 255};
static const Color CNR_BORDER = {211, 225, 237, 255};
static const Color CNR_HEADER_TEXT = {224, 241, 255, 255};
static const Color CNR_STATUS = {34, 116, 160, 255};
static const Color CNR_ERROR = {190, 67, 74, 255};
static const Color COVER_TOP = {225, 242, 255, 255};
static const Color COVER_BOTTOM = {255, 255, 255, 255};
static const char *gui_stations[6] = {
    "Shanghai", "Suzhou", "Nanjing", "Jinan", "Tianjin", "Beijing"
};

static bool date_dropdown_open;
static int selected_date_option;
static bool train_dropdown_open;
static int selected_train_option;
static int station_dropdown_open = -1;
static bool class_dropdown_open;

static void get_date_option(int offset, char *output, size_t output_size) {
    time_t now = time(NULL);
    struct tm date = *localtime(&now);
    date.tm_mday += offset;
    mktime(&date);
    strftime(output, output_size, "%Y-%m-%d", &date);
}

static Font ui_font;
static Font english_font;
static Font chinese_font;
static bool english_font_loaded;
static bool chinese_font_loaded;
static int *chinese_codepoints;
static int chinese_codepoint_count;

typedef enum {
    LANGUAGE_ZH,
    LANGUAGE_EN
} Language;

static Language language = LANGUAGE_ZH;

static const char *tr(const char *chinese, const char *english) {
    return language == LANGUAGE_ZH ? chinese : english;
}

static void set_language(Language selected) {
    language = selected;
    if (language == LANGUAGE_ZH && chinese_font_loaded) {
        ui_font = chinese_font;
    } else if (english_font_loaded) {
        ui_font = english_font;
    } else {
        ui_font = GetFontDefault();
    }
}

static void load_ui_fonts(void) {
    int ascii_codepoints[95];
    for (int i = 0; i < 95; i++) {
        ascii_codepoints[i] = 32 + i;
    }
    english_font = LoadFontEx("C:/Windows/Fonts/timesbd.ttf", 32,
                              ascii_codepoints, 95);
    english_font_loaded = english_font.texture.id != 0;

    int cjk_start = 0x4e00;
    int cjk_end = 0x9fff;
    int punctuation_start = 0x3000;
    int punctuation_end = 0x303f;
    int fullwidth_start = 0xff00;
    int fullwidth_end = 0xffef;
    chinese_codepoint_count = 95 +
                              (cjk_end - cjk_start + 1) +
                              (punctuation_end - punctuation_start + 1) +
                              (fullwidth_end - fullwidth_start + 1);
    chinese_codepoints = MemAlloc((unsigned int)chinese_codepoint_count * sizeof(int));
    for (int i = 0; i < 95; i++) chinese_codepoints[i] = 32 + i;
    for (int i = 0; i <= cjk_end - cjk_start; i++) {
        chinese_codepoints[95 + i] = cjk_start + i;
    }
    int offset = 95 + (cjk_end - cjk_start + 1);
    for (int i = 0; i <= punctuation_end - punctuation_start; i++) {
        chinese_codepoints[offset + i] = punctuation_start + i;
    }
    offset += punctuation_end - punctuation_start + 1;
    for (int i = 0; i <= fullwidth_end - fullwidth_start; i++) {
        chinese_codepoints[offset + i] = fullwidth_start + i;
    }
    chinese_font = LoadFontEx("C:/Windows/Fonts/simhei.ttf", 32,
                              chinese_codepoints, chinese_codepoint_count);
    chinese_font_loaded = chinese_font.texture.id != 0;
    if (english_font_loaded) SetTextureFilter(english_font.texture, TEXTURE_FILTER_BILINEAR);
    if (chinese_font_loaded) SetTextureFilter(chinese_font.texture, TEXTURE_FILTER_BILINEAR);
    MemFree(chinese_codepoints);
    chinese_codepoints = NULL;
}

static void ui_text(const char *text, int x, int y, float size, Color color) {
    DrawTextEx(ui_font, text, (Vector2){(float)x, (float)y}, size, 0, color);
}

static void draw_passenger_name(const char *name, int x, int y, float size, Color color) {
    bool contains_non_ascii = false;
    for (const unsigned char *character = (const unsigned char *)name;
         *character != '\0'; character++) {
        if (*character >= 0x80) {
            contains_non_ascii = true;
            break;
        }
    }
    if (language == LANGUAGE_EN && contains_non_ascii && chinese_font_loaded) {
        DrawTextEx(chinese_font, name, (Vector2){(float)x, (float)y}, size, 0, color);
    } else {
        DrawTextEx(ui_font, name, (Vector2){(float)x, (float)y}, size, 0, color);
    }
}

static void draw_centered_text(const char *text, int center_x, int y, float size, Color color) {
    int width = (int)MeasureTextEx(ui_font, text, size, 0).x;
    DrawTextEx(ui_font, text, (Vector2){(float)(center_x - width / 2), (float)y}, size, 0, color);
}

#define DrawText(text, x, y, size, color) ui_text(text, x, y, (float)(size), color)

typedef enum {
    VIEW_COVER,
    VIEW_DASHBOARD,
    VIEW_SELL,
    VIEW_PASSENGERS,
    VIEW_STATS,
    VIEW_SCHEDULE
} View;

typedef struct {
    char text[32];
    int length;
    int max_bytes;
    bool active;
    bool invalid;
} TextField;

static bool is_integer_text(const char *text) {
    if (text[0] == '\0') return false;
    for (int i = 0; text[i] != '\0'; i++) {
        if (text[i] < '0' || text[i] > '9') return false;
    }
    return true;
}

static void validate_form_fields(TextField *fields) {
    fields[0].invalid = fields[0].length > 0 && !valid_id(fields[0].text);
    fields[1].invalid = false;
    fields[2].invalid = fields[2].length > 0 &&
                       (!is_integer_text(fields[2].text) || atoi(fields[2].text) < 0 || atoi(fields[2].text) > 5);
    fields[3].invalid = fields[3].length > 0 &&
                       (!is_integer_text(fields[3].text) || atoi(fields[3].text) < 0 || atoi(fields[3].text) > 5);
    if (!fields[2].invalid && !fields[3].invalid &&
        fields[2].length > 0 && fields[3].length > 0 &&
        atoi(fields[2].text) == atoi(fields[3].text)) {
        fields[2].invalid = true;
        fields[3].invalid = true;
    }
    fields[4].invalid = fields[4].length > 0 &&
                       (!is_integer_text(fields[4].text) || (atoi(fields[4].text) != 0 && atoi(fields[4].text) != 1));
    fields[5].invalid = fields[5].length > 0 && !valid_date(fields[5].text);
    fields[6].invalid = fields[6].length > 0 &&
                       (fields[6].text[0] != 'G' || !is_integer_text(fields[6].text + 1));
}

static void draw_text_field(TextField *field, Rectangle bounds, const char *label) {
    DrawText(label, (int)bounds.x, (int)bounds.y - 24, 17, field->invalid ? CNR_ERROR : CNR_TEXT);
    DrawRectangleRec(bounds, WHITE);
    DrawRectangleLinesEx(bounds, 2, field->invalid ? CNR_ERROR : (field->active ? CNR_RED : CNR_BORDER));
    DrawText(field->text, (int)bounds.x + 12, (int)bounds.y + 10, 20, CNR_TEXT);
    if (field->invalid) {
        DrawCircle((int)bounds.x + bounds.width + 22, (int)bounds.y + 22, 12, CNR_ERROR);
        DrawText("!", (int)bounds.x + bounds.width + 19, (int)bounds.y + 9, 20, WHITE);
    }
    if (field->active && ((int)(GetTime() * 2) % 2 == 0)) {
        int text_width = (int)MeasureTextEx(ui_font, field->text, 20, 0).x;
        DrawLine((int)bounds.x + 12 + text_width, (int)bounds.y + 8,
                 (int)bounds.x + 12 + text_width, (int)bounds.y + 36, CNR_RED);
    }
}

static void draw_field_error(const TextField *field, Rectangle bounds, const char *message) {
    if (field->invalid) {
        DrawText(message, (int)bounds.x, (int)bounds.y + (int)bounds.height + 7, 14, CNR_ERROR);
    }
}

static bool button(Rectangle bounds, const char *label, bool selected);
static int train_option_count(const TextField *fields);
static void set_train_field(TextField *field, const TextField *fields, int option);

static void draw_cover(void) {
    int center_x = WINDOW_WIDTH / 2;
    DrawRectangleGradientV(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, COVER_TOP, COVER_BOTTOM);
    draw_centered_text("The Railway Of ZJQ", center_x, 190, 48, CNR_TEXT);
    draw_centered_text(tr("列车旅客管理系统", "Train Passenger Management System"), center_x, 270, 22, CNR_TEXT);
    draw_centered_text(tr("欢迎使用", "Welcome"), center_x, 335, 24, CNR_RED);
    button((Rectangle){center_x - 125, 410, 250, 58}, tr("进入系统", "Enter system"), false);
    button((Rectangle){WINDOW_WIDTH - 190, 42, 150, 42}, tr("English", "Chinese"), false);
}

static bool button(Rectangle bounds, const char *label, bool selected) {
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, bounds);
    Color fill = selected ? CNR_DARK_RED : (hovered ? (Color){86, 151, 207, 255} : CNR_RED);
    DrawRectangleRounded(bounds, 0.08f, 6, fill);
    int label_width = (int)MeasureTextEx(ui_font, label, 19, 0).x;
    DrawText(label, (int)(bounds.x + (bounds.width - label_width) / 2), (int)bounds.y + 13, 19, WHITE);
    return hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}

static void draw_sidebar(View view) {
    DrawRectangle(0, 0, PANEL_X, WINDOW_HEIGHT, WHITE);
    DrawRectangle(0, 0, PANEL_X, 112, CNR_RED);
    DrawText(tr("中国铁路", "CHINA RAILWAY"), 34, 30, 27, WHITE);
    DrawText(tr("旅客管理系统", "PASSENGER DESK"), 36, 76, 15, CNR_HEADER_TEXT);
    DrawLine(34, 124, 260, 124, CNR_BORDER);

    if (button((Rectangle){32, 148, 230, 48}, tr("主界面", "Home"), view == VIEW_DASHBOARD)) {
        /* Navigation is handled by the caller. */
    }
    if (button((Rectangle){32, 210, 230, 48}, tr("售票", "Sell ticket"), view == VIEW_SELL)) {
    }
    if (button((Rectangle){32, 272, 230, 48}, tr("旅客列表", "Passengers"), view == VIEW_PASSENGERS)) {
    }
    if (button((Rectangle){32, 334, 230, 48}, tr("统计信息", "Statistics"), view == VIEW_STATS)) {
    }
    if (button((Rectangle){32, 396, 230, 48}, tr("车次列表", "Timetable"), view == VIEW_SCHEDULE)) {
    }
    button((Rectangle){32, 458, 230, 42}, tr("English", "Chinese"), false);
    DrawText(tr("列车管理系统", "TRAIN MANAGEMENT"), 36, WINDOW_HEIGHT - 42, 15, CNR_MUTED);
}

static void draw_dashboard(void) {
    char today[11];
    today_string(today);

    DrawText(tr("运行总览", "Dashboard"), CONTENT_X, 42, 34, CNR_TEXT);
    DrawText(tr("今日列车运行情况", "Today's train operation at a glance"), CONTENT_X + 2, 84, 18, CNR_MUTED);

    DrawRectangleRounded((Rectangle){CONTENT_X, 140, 290, 128}, 0.06f, 6, WHITE);
    DrawRectangleLinesEx((Rectangle){CONTENT_X, 140, 290, 128}, 1, CNR_BORDER);
    // 链表中含未来日期的票，故不写「当前在车」
    DrawText(tr("售票总数", "TICKETS SOLD"), CONTENT_X + 22, 164, 16, CNR_RED);
    DrawText(TextFormat("%d", passenger_count()), CONTENT_X + 22, 196, 42, CNR_TEXT);
    DrawText(tr("张", "tickets"), CONTENT_X + 24, 242, 16, CNR_MUTED);

    DrawRectangleRounded((Rectangle){CONTENT_X + 320, 140, 290, 128}, 0.06f, 6, WHITE);
    DrawRectangleLinesEx((Rectangle){CONTENT_X + 320, 140, 290, 128}, 1, CNR_BORDER);
    // 区段复用下「剩余座位」没有单一定义，改为按车次去重的已占用座位数
    DrawText(tr("今日已占用座位", "SEATS TAKEN TODAY"), CONTENT_X + 342, 164, 16, CNR_RED);
    DrawText(TextFormat("%d", occupied_seat_total(today)), CONTENT_X + 342, 196, 42, CNR_TEXT);
    DrawText(tr("个（按车次去重）", "seat-slots booked"), CONTENT_X + 344, 242, 16, CNR_MUTED);

    DrawText(tr("快捷操作", "Quick actions"), CONTENT_X, 334, 24, CNR_TEXT);
    DrawText(tr("管理车票、旅客和座位信息。", "Manage tickets, passengers, and seat availability."), CONTENT_X, 372, 17, CNR_MUTED);
    button((Rectangle){CONTENT_X + 205, 414, 240, 48}, tr("新建车票", "New ticket"), false);
}

static void draw_schedule_view(void) {
    DrawText(tr("车次列表", "Timetable"), CONTENT_X, 42, 34, CNR_TEXT);
    DrawText(tr("今日固定车次与发车时间", "Today's fixed trains and departure times"), CONTENT_X + 2, 84, 18, CNR_MUTED);
    DrawRectangleRounded((Rectangle){CONTENT_X, 140, 290, 500}, 0.04f, 6, WHITE);
    DrawRectangleLinesEx((Rectangle){CONTENT_X, 140, 290, 500}, 1, CNR_BORDER);
    DrawRectangleRounded((Rectangle){CONTENT_X + 320, 140, 290, 500}, 0.04f, 6, WHITE);
    DrawRectangleLinesEx((Rectangle){CONTENT_X + 320, 140, 290, 500}, 1, CNR_BORDER);

    draw_centered_text(tr("始发站 - 终点站", "Origin - Destination"), CONTENT_X + 145, 166, 18, CNR_RED);
    draw_centered_text(tr("北京       上海", "Beijing       Shanghai"), CONTENT_X + 145, 198, 20, CNR_TEXT);
    draw_centered_text(tr("车次                 发车时间", "Train              Departure"), CONTENT_X + 145, 234, 16, CNR_MUTED);
    for (int i = 0; i < 5; i++) {
        int y = 252 + i * 38;
        DrawText(TextFormat("G%d", i * 2 + 1), CONTENT_X + 54, y, 19, CNR_TEXT);
        DrawText(TextFormat("%02d:00", 6 + i), CONTENT_X + 178, y, 19, CNR_TEXT);
    }

    draw_centered_text(tr("始发站 - 终点站", "Origin - Destination"), CONTENT_X + 465, 166, 18, CNR_RED);
    draw_centered_text(tr("上海       北京", "Shanghai       Beijing"), CONTENT_X + 465, 198, 20, CNR_TEXT);
    draw_centered_text(tr("车次                 发车时间", "Train              Departure"), CONTENT_X + 465, 234, 16, CNR_MUTED);
    for (int i = 0; i < 5; i++) {
        int y = 252 + i * 38;
        DrawText(TextFormat("G%d", i * 2 + 2), CONTENT_X + 374, y, 19, CNR_TEXT);
        DrawText(TextFormat("%02d:30", 6 + i), CONTENT_X + 498, y, 19, CNR_TEXT);
    }
}

static void draw_sell_view(TextField *fields) {
    DrawText(tr("售票", "Sell ticket"), CONTENT_X, 42, 34, CNR_TEXT);
    DrawText(tr("请输入旅客信息", "Enter passenger details"), CONTENT_X + 2, 84, 18, CNR_MUTED);

    Rectangle id_bounds = {CONTENT_X + 150, 126, 350, 42};
    Rectangle name_bounds = {CONTENT_X + 150, 202, 350, 42};
    Rectangle board_bounds = {CONTENT_X + 80, 278, 200, 42};
    Rectangle alight_bounds = {CONTENT_X + 370, 278, 200, 42};
    Rectangle class_bounds = {CONTENT_X + 80, 354, 200, 42};
    Rectangle date_bounds = {CONTENT_X + 370, 354, 200, 42};
    Rectangle train_bounds = {CONTENT_X + 80, 430, 200, 42};
    draw_text_field(&fields[0], id_bounds, tr("身份证后4位", "ID suffix (4 characters)"));
    draw_text_field(&fields[1], name_bounds, tr("姓名", "Name"));
    draw_text_field(&fields[2], board_bounds, tr("上车站", "Board station"));
    draw_text_field(&fields[3], alight_bounds, tr("下车站", "Alight station"));
    draw_text_field(&fields[4], class_bounds, tr("座位等级", "Seat class"));
    draw_text_field(&fields[5], date_bounds, tr("购票日期", "Travel date"));
    draw_text_field(&fields[6], train_bounds, tr("车次 G1-G10", "Train G1-G10"));
    draw_field_error(&fields[0], id_bounds, tr("身份证格式不正确", "Invalid ID format"));
    draw_field_error(&fields[2], board_bounds,
                     fields[3].length > 0 && atoi(fields[2].text) == atoi(fields[3].text)
                         ? tr("上车站不能与下车站相同", "Board and alight stations must differ")
                         : tr("上车站必须是0至5", "Use a number from 0 to 5"));
    draw_field_error(&fields[3], alight_bounds,
                     fields[2].length > 0 && atoi(fields[2].text) == atoi(fields[3].text)
                         ? tr("下车站不能与上车站相同", "Board and alight stations must differ")
                         : tr("下车站必须是0至5", "Use a number from 0 to 5"));
    draw_field_error(&fields[4], class_bounds, tr("等级只能是0或1", "Class must be 0 or 1"));
    draw_field_error(&fields[5], date_bounds, tr("日期格式应为YYYY-MM-DD", "Use YYYY-MM-DD"));
    draw_field_error(&fields[6], train_bounds, tr("请填写有效车次", "Enter a valid train number"));

    DrawText("v", (int)board_bounds.x + (int)board_bounds.width - 28,
             (int)board_bounds.y + 11, 18, CNR_RED);
    DrawText("v", (int)alight_bounds.x + (int)alight_bounds.width - 28,
             (int)alight_bounds.y + 11, 18, CNR_RED);
    for (int station_field = 2; station_field <= 3; station_field++) {
        if (station_dropdown_open != station_field) continue;
        Rectangle bounds = station_field == 2 ? board_bounds : alight_bounds;
        DrawRectangle((int)bounds.x, (int)bounds.y + 44, (int)bounds.width, 186, WHITE);
        DrawRectangleLinesEx((Rectangle){bounds.x, bounds.y + 44, bounds.width, 186}, 1, CNR_BORDER);
        for (int station = 0; station < 6; station++) {
            bool selected = fields[station_field].length > 0 &&
                            atoi(fields[station_field].text) == station;
            Color row_color = selected ? (Color){226, 240, 250, 255} : WHITE;
            DrawRectangle((int)bounds.x + 2, (int)bounds.y + 46 + station * 30,
                          (int)bounds.width - 4, 27, row_color);
            DrawText(TextFormat("%d  %s", station, language == LANGUAGE_ZH
                                ? stations[station] : gui_stations[station]),
                     (int)bounds.x + 16, (int)bounds.y + 51 + station * 30, 16, CNR_TEXT);
        }
    }

    DrawText("v", (int)class_bounds.x + (int)class_bounds.width - 28,
             (int)class_bounds.y + 11, 18, CNR_RED);
    if (class_dropdown_open) {
        DrawRectangle((int)class_bounds.x, (int)class_bounds.y + 44,
                      (int)class_bounds.width, 66, WHITE);
        DrawRectangleLinesEx((Rectangle){class_bounds.x, class_bounds.y + 44,
                                         class_bounds.width, 66}, 1, CNR_BORDER);
        for (int seat_class = 0; seat_class <= 1; seat_class++) {
            bool selected = fields[4].length > 0 && atoi(fields[4].text) == seat_class;
            Color row_color = selected ? (Color){226, 240, 250, 255} : WHITE;
            DrawRectangle((int)class_bounds.x + 2, (int)class_bounds.y + 46 + seat_class * 30,
                          (int)class_bounds.width - 4, 27, row_color);
            DrawText(TextFormat("%d  %s", seat_class,
                                language == LANGUAGE_ZH
                                    ? (seat_class == 0 ? "二等座" : "一等座")
                                    : (seat_class == 0 ? "Second class" : "First class")),
                     (int)class_bounds.x + 16, (int)class_bounds.y + 51 + seat_class * 30,
                     16, CNR_TEXT);
        }
    }

    DrawText("v", (int)date_bounds.x + (int)date_bounds.width - 28, (int)date_bounds.y + 11, 18, CNR_RED);
    if (date_dropdown_open) {
        DrawRectangle((int)date_bounds.x, (int)date_bounds.y + 44, (int)date_bounds.width, 132, WHITE);
        DrawRectangleLinesEx((Rectangle){date_bounds.x, date_bounds.y + 44, date_bounds.width, 132}, 1, CNR_BORDER);
        for (int i = 0; i < 4; i++) {
            char date_text[11];
            get_date_option(i, date_text, sizeof(date_text));
            Color row_color = i == selected_date_option ? (Color){226, 240, 250, 255} : WHITE;
            DrawRectangle((int)date_bounds.x + 2, (int)date_bounds.y + 46 + i * 30,
                          (int)date_bounds.width - 4, 27, row_color);
            DrawText(date_text, (int)date_bounds.x + 18, (int)date_bounds.y + 51 + i * 30, 16, CNR_TEXT);
            if (i == selected_date_option) {
                DrawText("OK", (int)date_bounds.x + (int)date_bounds.width - 42,
                         (int)date_bounds.y + 51 + i * 30, 14, CNR_RED);
            }
        }
    }
    DrawText("v", (int)train_bounds.x + (int)train_bounds.width - 28, (int)train_bounds.y + 11, 18, CNR_RED);
    if (train_dropdown_open) {
        int option_count = train_option_count(fields);
        int dropdown_height = option_count > 0 ? option_count * 30 : 34;
        DrawRectangle((int)train_bounds.x, (int)train_bounds.y + 44,
                      (int)train_bounds.width, dropdown_height, WHITE);
        DrawRectangleLinesEx((Rectangle){train_bounds.x, train_bounds.y + 44,
                                         train_bounds.width, dropdown_height}, 1, CNR_BORDER);
        if (option_count == 0) {
            DrawText(tr("请先选择北京和上海", "Select Beijing and Shanghai first"),
                     (int)train_bounds.x + 12, (int)train_bounds.y + 52, 14, CNR_MUTED);
        } else {
            int board = atoi(fields[2].text);
            for (int i = 0; i < option_count; i++) {
                char train_text[32];
                int train_number = board > atoi(fields[3].text) ? i * 2 + 1 : i * 2 + 2;
                snprintf(train_text, sizeof(train_text), "G%d   %02d:%02d",
                         train_number, 6 + i, board > atoi(fields[3].text) ? 0 : 30);
                Color row_color = i == selected_train_option ? (Color){226, 240, 250, 255} : WHITE;
                DrawRectangle((int)train_bounds.x + 2, (int)train_bounds.y + 46 + i * 30,
                              (int)train_bounds.width - 4, 27, row_color);
                DrawText(train_text, (int)train_bounds.x + 16,
                         (int)train_bounds.y + 51 + i * 30, 16, CNR_TEXT);
            }
        }
    }
    button((Rectangle){CONTENT_X + 205, 570, 240, 48}, tr("确认购票", "Confirm ticket"), false);
}

static bool gui_sell_ticket(TextField *fields, char *status, size_t status_size) {
    int board = atoi(fields[2].text);
    int alight = atoi(fields[3].text);
    int firstclass = atoi(fields[4].text);
    char err[160];

    if (!valid_id(fields[0].text)) {
        snprintf(status, status_size, "%s", tr("身份证后4位格式不正确。", "Invalid ID suffix."));
        return false;
    }
    if (fields[1].length == 0) {
        snprintf(status, status_size, "%s", tr("请输入姓名。", "Name is required."));
        return false;
    }
    // atoi("") 会返回 0（恰好等于「上海」），必须先挡掉空输入
    if (!is_integer_text(fields[2].text) || !is_integer_text(fields[3].text)) {
        snprintf(status, status_size, "%s",
                 tr("请选择上车站和下车站。", "Select board and alight stations."));
        return false;
    }
    if (firstclass != 0 && firstclass != 1) {
        snprintf(status, status_size, "%s", tr("座位等级只能是0或1。", "Class must be 0 or 1."));
        return false;
    }
    // 车次格式、日期、车站范围、行程方向 全部交给模型层统一校验
    if (!validate_trip(fields[6].text, fields[5].text, board, alight, firstclass,
                       err, sizeof(err))) {
        snprintf(status, status_size, "%s", err);
        return false;
    }
    if (search_passenger(fields[0].text) != NULL) {
        snprintf(status, status_size, "%s", tr("该身份证已经购票。", "This ID already has a ticket."));
        return false;
    }

    int carriage = -1;
    int seat = -1;
    if (!assign_seat(fields[6].text, fields[5].text, firstclass, board, alight,
                     &carriage, &seat)) {
        snprintf(status, status_size, "%s",
                 tr("该区间该等级座位已售罄。", "No seats left for this class on this leg."));
        return false;
    }

    Passenger passenger = {0};
    strncpy(passenger.id, fields[0].text, sizeof(passenger.id) - 1);
    strncpy(passenger.name, fields[1].text, sizeof(passenger.name) - 1);
    strncpy(passenger.travel_date, fields[5].text, sizeof(passenger.travel_date) - 1);
    // 限宽拷贝：validate_trip 已保证车次形如 G1~G10（最长3字符），
    // 但仍限定长度，避免任何绕过校验的路径再次触发栈溢出
    strncpy(passenger.train_no, fields[6].text, sizeof(passenger.train_no) - 1);
    train_depart_time(train_number_of(passenger.train_no), passenger.depart_time);
    passenger.board = board;
    passenger.alight = alight;
    passenger.firstclass = firstclass;
    passenger.price = calc_price(board, alight, firstclass);
    passenger.carriage = carriage;
    passenger.seat = seat;

    InsertResult inserted = insert_passenger(passenger);
    if (inserted != INSERT_OK) {
        snprintf(status, status_size, "%s",
                 inserted == INSERT_DUPLICATE
                     ? tr("该身份证已经购票。", "This ID already has a ticket.")
                     : tr("无法分配旅客信息内存。", "Could not allocate passenger memory."));
        return false;
    }
    if (!save_passengers(DATA_FILE)) {
        snprintf(status, status_size, "%s", tr("购票成功，但数据保存失败。", "Ticket sold, but saving failed."));
        return false;
    }
    if (language == LANGUAGE_ZH) {
        snprintf(status, status_size, "购票成功：%d车%d号座位。", carriage, seat);
    } else {
        snprintf(status, status_size, "Ticket confirmed: carriage %d, seat %d.", carriage, seat);
    }
    return true;
}

static void draw_passengers_view(void) {
    DrawText(tr("旅客列表", "Passengers"), PANEL_X + 42, 42, 34, CNR_TEXT);
    DrawText(tr("当前在车旅客", "Current passengers on board"), PANEL_X + 44, 84, 18, CNR_MUTED);
    DrawText(tr("证件", "ID"), PANEL_X + 42, 138, 16, CNR_RED);
    DrawText(tr("姓名", "NAME"), PANEL_X + 130, 138, 16, CNR_RED);
    DrawText(tr("行程", "ROUTE"), PANEL_X + 300, 138, 16, CNR_RED);
    DrawText(tr("车次", "TRAIN"), PANEL_X + 500, 138, 16, CNR_RED);
    DrawText(tr("日期", "DATE"), PANEL_X + 575, 138, 16, CNR_RED);
    DrawText(tr("车厢/座位", "CARRIAGE/SEAT"), PANEL_X + 690, 138, 16, CNR_RED);

    int row = 0;
    for (Node *node = head; node != NULL && row < 12; node = node->next, row++) {
        int y = 174 + row * 36;
        DrawLine(PANEL_X + 42, y + 25, WINDOW_WIDTH - 44, y + 25, CNR_BORDER);
        DrawText(node->data.id, PANEL_X + 42, y, 17, CNR_TEXT);
        draw_passenger_name(node->data.name, PANEL_X + 130, y, 17, CNR_TEXT);
        if (language == LANGUAGE_ZH) {
            DrawText(TextFormat("%s -> %s", stations[node->data.board], stations[node->data.alight]), PANEL_X + 300, y, 17, CNR_TEXT);
        } else {
            DrawText(TextFormat("%s -> %s", gui_stations[node->data.board], gui_stations[node->data.alight]), PANEL_X + 300, y, 17, CNR_TEXT);
        }
        DrawText(node->data.train_no, PANEL_X + 500, y, 17, CNR_TEXT);
        DrawText(node->data.travel_date, PANEL_X + 575, y, 15, CNR_TEXT);
        DrawText(language == LANGUAGE_ZH
                     ? TextFormat("%d车 / %d号", node->data.carriage, node->data.seat)
                     : TextFormat("Car %d / Seat %d", node->data.carriage, node->data.seat),
                 PANEL_X + 690, y, 17, CNR_TEXT);
    }
    if (row == 0) DrawText(tr("暂无旅客。", "No passengers yet."), PANEL_X + 42, 184, 18, CNR_MUTED);
}

/* 统计视图：区段复用下「已售 N/12」会出现超过总座位数的自相矛盾数字，
   因此改为展示「全程空座 / 已占用」这一划分，以及各区段的实际载客数。 */
static int stats_train_number = 1;
static int stats_date_option = 0;

static Rectangle stats_date_button(int i) {
    return (Rectangle){PANEL_X + 106 + i * 122, 118, 116, 32};
}

static Rectangle stats_train_button(int i) {
    return (Rectangle){PANEL_X + 106 + i * 74, 162, 68, 32};
}

static void draw_stats_view(void) {
    char date[11];
    char train_no[6];
    date_offset_string(stats_date_option, date);
    snprintf(train_no, sizeof(train_no), "G%d", stats_train_number);

    int total_seats = 0;
    for (int c = 1; c <= CARRIAGE_COUNT; c++) total_seats += car_seats[c];

    DrawText(tr("统计信息", "Statistics"), PANEL_X + 42, 42, 34, CNR_TEXT);
    DrawText(tr("按车次与日期统计", "Per train and date"), PANEL_X + 44, 84, 18, CNR_MUTED);

    DrawText(tr("日期", "DATE"), PANEL_X + 42, 126, 16, CNR_RED);
    for (int i = 0; i < 4; i++) {
        char label[11];
        date_offset_string(i, label);
        button(stats_date_button(i), label, i == stats_date_option);
    }
    DrawText(tr("车次", "TRAIN"), PANEL_X + 42, 170, 16, CNR_RED);
    for (int i = 0; i < TRAIN_COUNT; i++) {
        char label[6];
        snprintf(label, sizeof(label), "G%d", i + 1);
        button(stats_train_button(i), label, i + 1 == stats_train_number);
    }

    int y = 218;
    DrawText(tr("各车厢座位", "Seats by carriage"), PANEL_X + 42, y, 20, CNR_TEXT);
    y += 32;
    for (int c = 1; c <= CARRIAGE_COUNT; c++) {
        int used = carriage_occupied_seats(train_no, date, c);
        int fully_free = carriage_fully_free_seats(train_no, date, c);
        char label[28];
        char detail[56];
        if (language == LANGUAGE_ZH) {
            snprintf(label, sizeof(label), "%d号车厢", c);
            snprintf(detail, sizeof(detail), "共%d座  空%d  占%d",
                     car_seats[c], fully_free, used);
        } else {
            snprintf(label, sizeof(label), "Carriage %d", c);
            snprintf(detail, sizeof(detail), "%d seats  %d free  %d used",
                     car_seats[c], fully_free, used);
        }
        DrawText(label, PANEL_X + 42, y, 17, CNR_TEXT);
        DrawRectangle(PANEL_X + 170, y + 3, 330, 20, CNR_BORDER);
        if (used > 0) {
            DrawRectangle(PANEL_X + 170, y + 3,
                          (int)(330.0f * used / car_seats[c]), 20, CNR_RED);
        }
        DrawText(detail, PANEL_X + 512, y + 1, 15, CNR_MUTED);
        y += 30;
    }

    y += 14;
    DrawText(tr("各区段载客", "Passengers per segment"), PANEL_X + 42, y, 20, CNR_TEXT);
    y += 30;
    for (int i = 0; i < SEGMENT_COUNT; i++) {
        char label[40];
        char detail[32];
        int load = segment_load(train_no, date, i);
        if (language == LANGUAGE_ZH) {
            snprintf(label, sizeof(label), "%s-%s", stations[i], stations[i + 1]);
        } else {
            snprintf(label, sizeof(label), "%s-%s", gui_stations[i], gui_stations[i + 1]);
        }
        snprintf(detail, sizeof(detail), "%d / %d", load, total_seats);
        DrawText(label, PANEL_X + 42, y, 16, CNR_TEXT);
        DrawRectangle(PANEL_X + 250, y + 2, 250, 18, CNR_BORDER);
        if (load > 0) {
            DrawRectangle(PANEL_X + 250, y + 2,
                          (int)(250.0f * load / total_seats), 18, CNR_RED);
        }
        DrawText(detail, PANEL_X + 512, y, 15, CNR_MUTED);
        y += 28;
    }
}

static void handle_text_input(TextField *field) {
    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key != 127) {
            char utf8[5];
            int bytes = 0;
            if (key <= 0x7f) {
                utf8[bytes++] = (char)key;
            } else if (key <= 0x7ff) {
                utf8[bytes++] = (char)(0xc0 | (key >> 6));
                utf8[bytes++] = (char)(0x80 | (key & 0x3f));
            } else if (key <= 0xffff) {
                utf8[bytes++] = (char)(0xe0 | (key >> 12));
                utf8[bytes++] = (char)(0x80 | ((key >> 6) & 0x3f));
                utf8[bytes++] = (char)(0x80 | (key & 0x3f));
            } else if (key <= 0x10ffff) {
                utf8[bytes++] = (char)(0xf0 | (key >> 18));
                utf8[bytes++] = (char)(0x80 | ((key >> 12) & 0x3f));
                utf8[bytes++] = (char)(0x80 | ((key >> 6) & 0x3f));
                utf8[bytes++] = (char)(0x80 | (key & 0x3f));
            }
            if (field->length + bytes <= field->max_bytes &&
                field->length + bytes < (int)sizeof(field->text)) {
                memcpy(field->text + field->length, utf8, (size_t)bytes);
                field->length += bytes;
                field->text[field->length] = '\0';
            }
        }
        key = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) && field->length > 0) {
        field->length--;
        while (field->length > 0 && ((unsigned char)field->text[field->length] & 0xc0) == 0x80) {
            field->length--;
        }
        field->text[field->length] = '\0';
    }
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_V)) {
        const char *clipboard = GetClipboardText();
        if (clipboard != NULL) {
            size_t available = (size_t)field->max_bytes - (size_t)field->length;
            size_t pasted_length = strlen(clipboard);
            if (pasted_length > available) pasted_length = available;
            while (pasted_length > 0 &&
                   ((unsigned char)clipboard[pasted_length] & 0xc0) == 0x80) {
                pasted_length--;
            }
            memcpy(field->text + field->length, clipboard, pasted_length);
            field->length += (int)pasted_length;
            field->text[field->length] = '\0';
        }
    }
}

static Rectangle field_bounds(int index) {
    if (index == 0) return (Rectangle){CONTENT_X + 150, 126, 350, 42};
    if (index == 1) return (Rectangle){CONTENT_X + 150, 202, 350, 42};
    if (index == 2) return (Rectangle){CONTENT_X + 80, 278, 200, 42};
    if (index == 3) return (Rectangle){CONTENT_X + 370, 278, 200, 42};
    if (index == 4) return (Rectangle){CONTENT_X + 80, 354, 200, 42};
    if (index == 5) return (Rectangle){CONTENT_X + 370, 354, 200, 42};
    return (Rectangle){CONTENT_X + 80, 430, 200, 42};
}

static void set_date_field(TextField *field, int option) {
    get_date_option(option, field->text, sizeof(field->text));
    field->length = (int)strlen(field->text);
    field->invalid = false;
    selected_date_option = option;
}

static int train_option_count(const TextField *fields) {
    if (fields[2].length == 0 || fields[3].length == 0 ||
        !is_integer_text(fields[2].text) || !is_integer_text(fields[3].text)) return 0;
    int board = atoi(fields[2].text);
    int alight = atoi(fields[3].text);
    if (board == alight || board < 0 || board > 5 || alight < 0 || alight > 5) return 0;
    return 5;
}

static void set_train_field(TextField *field, const TextField *fields, int option) {
    int board = atoi(fields[2].text);
    int alight = atoi(fields[3].text);
    int train_number = board > alight ? option * 2 + 1 : option * 2 + 2;
    snprintf(field->text, sizeof(field->text), "G%d", train_number);
    field->length = (int)strlen(field->text);
    field->invalid = false;
    selected_train_option = option;
}

int main(void) {
    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "Train Passenger Desk");
    SetTargetFPS(60);

    load_ui_fonts();
    set_language(LANGUAGE_ZH);

    View view = VIEW_COVER;
    TextField fields[FIELD_COUNT] = {0};
    int active_field = 0;
    fields[active_field].active = true;
    for (int i = 0; i < FIELD_COUNT; i++) fields[i].max_bytes = 31;
    fields[1].max_bytes = 19;
    set_date_field(&fields[5], 0);
    char status[128] = "请从左侧菜单选择功能。";
    // 加载被拒收时坏文件已备份为 .bad-<时间戳>，此时不应在退出时把空表写回去
    bool data_rejected = false;
    LoadResult loaded = load_passengers(DATA_FILE);
    if (loaded == LOAD_OK) {
        snprintf(status, sizeof(status), "%s", tr("已读取上次保存的数据。", "Saved data loaded."));
    } else if (loaded == LOAD_REJECTED) {
        data_rejected = true;
        snprintf(status, sizeof(status), "%s",
                 tr("存档未通过校验，已备份为 .bad-<时间戳>，以空列表启动。",
                    "Save file failed validation; backed up as .bad-<timestamp>."));
    } else if (loaded == LOAD_NO_MEMORY) {
        data_rejected = true;
        snprintf(status, sizeof(status), "%s",
                 tr("内存不足，无法读取存档。", "Not enough memory to load the save file."));
    }
    char today[11];
    today_string(today);
    int expired_count = remove_expired_passengers(today);
    if (expired_count > 0) {
        save_passengers(DATA_FILE);
        snprintf(status, sizeof(status), tr("已自动清理%d名过期旅客。", "%d expired passengers removed."),
                 expired_count);
    }

    while (!WindowShouldClose()) {
        time_t loop_now = time(NULL);
        struct tm loop_date = *localtime(&loop_now);
        char loop_today[11];
        strftime(loop_today, sizeof(loop_today), "%Y-%m-%d", &loop_date);
        if (strcmp(loop_today, today) != 0) {
            strcpy(today, loop_today);
            int expired_count = remove_expired_passengers(today);
            if (expired_count > 0) {
                save_passengers(DATA_FILE);
                snprintf(status, sizeof(status), tr("已自动清理%d名过期旅客。", "%d expired passengers removed."),
                         expired_count);
            }
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            Vector2 mouse = GetMousePosition();
            if (view == VIEW_COVER && CheckCollisionPointRec(mouse, (Rectangle){CONTENT_X + 150, 410, 250, 58})) {
                view = VIEW_DASHBOARD;
            }
            if (view == VIEW_COVER && CheckCollisionPointRec(mouse, (Rectangle){WINDOW_WIDTH - 190, 42, 150, 42})) {
                set_language(language == LANGUAGE_ZH ? LANGUAGE_EN : LANGUAGE_ZH);
                if (language == LANGUAGE_ZH) {
                    snprintf(status, sizeof(status), "请从左侧菜单选择功能。");
                } else {
                    snprintf(status, sizeof(status), "Choose a section from the left menu.");
                }
            }
            if (view != VIEW_COVER && CheckCollisionPointRec(mouse, (Rectangle){32, 148, 230, 48})) view = VIEW_DASHBOARD;
            if (view != VIEW_COVER && CheckCollisionPointRec(mouse, (Rectangle){32, 210, 230, 48})) view = VIEW_SELL;
            if (view != VIEW_COVER && CheckCollisionPointRec(mouse, (Rectangle){32, 272, 230, 48})) view = VIEW_PASSENGERS;
            if (view != VIEW_COVER && CheckCollisionPointRec(mouse, (Rectangle){32, 334, 230, 48})) view = VIEW_STATS;
            if (view != VIEW_COVER && CheckCollisionPointRec(mouse, (Rectangle){32, 396, 230, 48})) view = VIEW_SCHEDULE;
            if (view != VIEW_COVER && CheckCollisionPointRec(mouse, (Rectangle){32, 458, 230, 42})) {
                set_language(language == LANGUAGE_ZH ? LANGUAGE_EN : LANGUAGE_ZH);
                snprintf(status, sizeof(status), "%s", tr("请从左侧菜单选择功能。", "Choose a section from the left menu."));
            }
            if (view == VIEW_DASHBOARD && CheckCollisionPointRec(mouse, (Rectangle){CONTENT_X + 205, 414, 240, 48})) {
                view = VIEW_SELL;
            }
            if (view == VIEW_STATS) {
                for (int i = 0; i < 4; i++) {
                    if (CheckCollisionPointRec(mouse, stats_date_button(i))) stats_date_option = i;
                }
                for (int i = 0; i < TRAIN_COUNT; i++) {
                    if (CheckCollisionPointRec(mouse, stats_train_button(i))) stats_train_number = i + 1;
                }
            }
            if (view == VIEW_SELL) {
                bool station_dropdown_handled = false;
                for (int station_field = 2; station_field <= 3; station_field++) {
                    Rectangle station_bounds = field_bounds(station_field);
                    if (CheckCollisionPointRec(mouse, station_bounds)) {
                        station_dropdown_open = station_dropdown_open == station_field ? -1 : station_field;
                        date_dropdown_open = false;
                        train_dropdown_open = false;
                        fields[active_field].active = false;
                        active_field = station_field;
                        fields[active_field].active = true;
                        station_dropdown_handled = true;
                    } else if (station_dropdown_open == station_field &&
                               CheckCollisionPointRec(mouse,
                                   (Rectangle){station_bounds.x, station_bounds.y + 44,
                                               station_bounds.width, 186})) {
                        int option = (int)((mouse.y - station_bounds.y - 44) / 30);
                        if (option >= 0 && option < 6) {
                            snprintf(fields[station_field].text,
                                     sizeof(fields[station_field].text), "%d", option);
                            fields[station_field].length = (int)strlen(fields[station_field].text);
                            fields[station_field].invalid = false;
                            fields[6].text[0] = '\0';
                            fields[6].length = 0;
                            selected_train_option = 0;
                            station_dropdown_open = -1;
                            station_dropdown_handled = true;
                        }
                    }
                }
                if (!station_dropdown_handled && station_dropdown_open != -1) {
                    station_dropdown_open = -1;
                }
                Rectangle class_bounds = field_bounds(4);
                if (!station_dropdown_handled && CheckCollisionPointRec(mouse, class_bounds)) {
                    class_dropdown_open = !class_dropdown_open;
                    station_dropdown_open = -1;
                    date_dropdown_open = false;
                    train_dropdown_open = false;
                    fields[active_field].active = false;
                    active_field = 4;
                    fields[active_field].active = true;
                } else if (!station_dropdown_handled && class_dropdown_open &&
                           CheckCollisionPointRec(mouse,
                               (Rectangle){class_bounds.x, class_bounds.y + 44,
                                           class_bounds.width, 66})) {
                    int option = (int)((mouse.y - class_bounds.y - 44) / 30);
                    if (option >= 0 && option <= 1) {
                        snprintf(fields[4].text, sizeof(fields[4].text), "%d", option);
                        fields[4].length = (int)strlen(fields[4].text);
                        fields[4].invalid = false;
                        class_dropdown_open = false;
                    }
                } else {
                    class_dropdown_open = false;
                }
                Rectangle date_bounds = field_bounds(5);
                Rectangle train_bounds = field_bounds(6);
                if (!station_dropdown_handled && CheckCollisionPointRec(mouse, date_bounds)) {
                    date_dropdown_open = !date_dropdown_open;
                    fields[active_field].active = false;
                    active_field = 5;
                    fields[active_field].active = true;
                } else if (!station_dropdown_handled && date_dropdown_open &&
                           CheckCollisionPointRec(mouse, (Rectangle){date_bounds.x, date_bounds.y + 44, date_bounds.width, 132})) {
                    int option = (int)((mouse.y - date_bounds.y - 44) / 30);
                    if (option >= 0 && option < 4) {
                        set_date_field(&fields[5], option);
                        date_dropdown_open = false;
                    }
                } else {
                    date_dropdown_open = false;
                }
                if (!station_dropdown_handled && CheckCollisionPointRec(mouse, train_bounds)) {
                    train_dropdown_open = !train_dropdown_open;
                    fields[active_field].active = false;
                    active_field = 6;
                    fields[active_field].active = true;
                } else if (!station_dropdown_handled && train_dropdown_open &&
                           CheckCollisionPointRec(mouse, (Rectangle){train_bounds.x, train_bounds.y + 44,
                                                                    train_bounds.width,
                                                                    train_option_count(fields) > 0 ? train_option_count(fields) * 30 : 34})) {
                    int option = (int)((mouse.y - train_bounds.y - 44) / 30);
                    int option_count = train_option_count(fields);
                    if (option >= 0 && option < option_count) {
                        set_train_field(&fields[6], fields, option);
                        train_dropdown_open = false;
                    }
                } else {
                    train_dropdown_open = false;
                }
                if (!station_dropdown_handled) for (int i = 0; i < FIELD_COUNT; i++) {
                    Rectangle bounds = field_bounds(i);
                    if (CheckCollisionPointRec(mouse, bounds)) {
                        fields[active_field].active = false;
                        active_field = i;
                        fields[active_field].active = true;
                        if (i == 2 || i == 3) {
                            station_dropdown_open = i;
                            fields[6].text[0] = '\0';
                            fields[6].length = 0;
                            selected_train_option = 0;
                        }
                    }
                }
                if (CheckCollisionPointRec(mouse, (Rectangle){CONTENT_X + 205, 570, 240, 48})) {
                    gui_sell_ticket(fields, status, sizeof(status));
                }
            }
        }

        if (view == VIEW_SELL) {
            if (active_field != 5 && active_field != 6) handle_text_input(&fields[active_field]);
            validate_form_fields(fields);
        }

        if (view == VIEW_SELL && IsKeyPressed(KEY_TAB)) {
            fields[active_field].active = false;
            active_field = (active_field + 1) % FIELD_COUNT;
            fields[active_field].active = true;
        }
        if (view == VIEW_SELL && IsKeyPressed(KEY_ENTER)) {
            if (active_field < FIELD_COUNT - 1) {
                fields[active_field].active = false;
                active_field++;
                fields[active_field].active = true;
            } else {
                gui_sell_ticket(fields, status, sizeof(status));
            }
        }

        BeginDrawing();
        ClearBackground(CNR_BACKGROUND);
        if (view == VIEW_COVER) {
            draw_cover();
        } else {
            draw_sidebar(view);
            if (view == VIEW_DASHBOARD) draw_dashboard();
            if (view == VIEW_SELL) draw_sell_view(fields);
            if (view == VIEW_PASSENGERS) draw_passengers_view();
            if (view == VIEW_STATS) draw_stats_view();
            if (view == VIEW_SCHEDULE) draw_schedule_view();
            DrawText(status, CONTENT_X, WINDOW_HEIGHT - 42, 16, CNR_STATUS);
        }
        EndDrawing();
    }

    // 只有成功加载过（或首次运行）才写回，避免用空表覆盖掉刚被拒收的存档
    if (!data_rejected) save_passengers(DATA_FILE);
    free_all_passengers();
    if (english_font_loaded) UnloadFont(english_font);
    if (chinese_font_loaded) UnloadFont(chinese_font);
    CloseWindow();
    return 0;
}
