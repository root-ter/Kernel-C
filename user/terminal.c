#include <stdint.h>
#include <stddef.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define INTERNAL_SPACE 0x01

#define SYSCALL_PRINT_CHAR 0
#define SYSCALL_PRINT_STRING 1
#define SYSCALL_GETCHAR 30 /* получить символ из клавиатурного буфера; -1 если пусто */
#define SYSCALL_SETPOSCURSOR 31

uint8_t x;
uint8_t y;

uint32_t input_len; // количество символов во входной строке

enum vga_color {
    BLACK,
    BLUE,
    GREEN,
    CYAN,
    RED,
    MAGENTA,
    BROWN,
    GREY,
    DARK_GREY,
    BRIGHT_BLUE,
    BRIGHT_GREEN,
    BRIGHT_CYAN,
    BRIGHT_RED,
    BRIGHT_MAGENTA,
    YELLOW,
    WHITE
};

// Внутренний буфер экрана
static char screen_chars[VGA_HEIGHT][VGA_WIDTH];
static uint8_t screen_attr[VGA_HEIGHT][VGA_WIDTH];

static inline void sys_print_str(const char *s, uint32_t x, uint32_t y, uint8_t fg, uint8_t bg)
{
    asm volatile(
        "int $0x80"
        :
        : "a"(SYSCALL_PRINT_STRING), "b"(s), "c"(x), "d"(y), "S"(fg), "D"(bg)
        : "memory");
}

static inline void sys_print_char(char ch, uint32_t x, uint32_t y, uint8_t fg, uint8_t bg)
{
    asm volatile(
        "int $0x80"
        :
        : "a"(SYSCALL_PRINT_CHAR), "b"(ch), "c"(x), "d"(y), "S"(fg), "D"(bg)
        : "memory");
}

static inline const char sys_getchar(void)
{
    asm volatile(
        "int $0x80"
        :
        : "a"(SYSCALL_GETCHAR)
        : "memory");
    // Возвращает символ или 0, если нет символов
    // В реальном режиме нужно туда вставить обработку возврата
}

static inline void sys_setposcursor(uint32_t x, uint32_t y)
{
    asm volatile(
        "int $0x80"
        :
        : "a"(SYSCALL_SETPOSCURSOR), "b"(x), "c"(y)
        : "memory");
}

// Вспомогательные функции
void new_line(void)
{
    x = 0;
    y += 1;
    sys_setposcursor(x, y);
}

// Скролл с использованием буфера
static void scroll_screen_syscall(void)
{
    for (int row = 1; row < VGA_HEIGHT; row++)
    {
        for (int col = 0; col < VGA_WIDTH; col++)
        {
            screen_chars[row - 1][col] = screen_chars[row][col];
            screen_attr[row - 1][col] = screen_attr[row][col];
        }
    }

    for (int col = 0; col < VGA_WIDTH; col++)
    {
        screen_chars[VGA_HEIGHT - 1][col] = ' ';
        screen_attr[VGA_HEIGHT - 1][col] = (BLACK << 4) | GREY;
    }

    // Перерисовка всего экрана из буфера
    for (int row = 0; row < VGA_HEIGHT; row++)
    {
        for (int col = 0; col < VGA_WIDTH; col++)
        {
            uint8_t attr = screen_attr[row][col];
            sys_print_char(screen_chars[row][col], col, row, attr & 0x0F, attr >> 4);
        }
    }
}

// Печать символа с обновлением внутреннего буфера
void print_char_to_buffer(char c, int col, int row, uint8_t fg, uint8_t bg)
{
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    screen_chars[row][col] = c;
    screen_attr[row][col] = (bg << 4) | (fg & 0x0F);
    sys_print_char(c, col, row, fg, bg);
}

// Печать строки в буфер и на экран
void print_str_to_buffer(const char *str, int col, int row, uint8_t fg, uint8_t bg)
{
    int c = col;
    int r = row;

    for (const char *p = str; *p; ++p)
    {
        print_char_to_buffer(*p, c, r, fg, bg);
        c++;
    }
}

// Внутренний буфер команд
char input_buffer[256];

void backspace(void)
{
    if (input_len == 0)
        return;

    if (x == 0)
    {
        if (y > 0)
        {
            y--;
            x = VGA_WIDTH - 1;
        }
        else
            return; // в верхней левой ячейке
    }
    else
        x--;

    print_char_to_buffer(' ', x, y, WHITE, BLACK);
    if (input_len > 0)
        input_len--;
    sys_setposcursor(x, y);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *b && *a == *b)
    {
        a++;
        b++;
    }
    return *a - *b;
}

void process_command(char *cmd)
{
    if (strcmp(cmd, "help") == 0)
    {
        print_str_to_buffer("Available commands:", 0, y++, WHITE, BLACK);
        print_str_to_buffer("help   - show help", 0, y++, WHITE, BLACK);
        print_str_to_buffer("clear  - clear screen", 0, y++, WHITE, BLACK);
    }
    else if (strcmp(cmd, "clear") == 0)
    {
        for (int row = 0; row < VGA_HEIGHT; row++)
            for (int col = 0; col < VGA_WIDTH; col++)
            {
                screen_chars[row][col] = ' ';
                screen_attr[row][col] = (BLACK << 4) | WHITE;
            }
        sys_setposcursor(0, 0);
        y = 0;
    }
    else
    {
        print_str_to_buffer("Unknown command", 0, y++, WHITE, BLACK);
    }
}



void main(void)
{
    x = 0; y = 0; input_len = 0;

    // Вывод приветствия
    for (int col = 0; col < VGA_WIDTH; col++)
    {
        screen_chars[0][col] = ' ';
        screen_attr[0][col] = (BLACK << 4) | WHITE;
    }
    print_str_to_buffer("SimpleTerm v0.2", x, y, WHITE, BLACK);
    y++;
    // Показ prompt
    print_str_to_buffer("$: ", x, y, WHITE, BLACK);
    sys_setposcursor(x, y);

    while (1)
    {
        char ch = sys_getchar();
        if (ch == '\0' || ch == ' ')
        {
            asm volatile("hlt");
            continue;
        }
        if (ch == '\n')
        {
            // Обработка команды
            input_buffer[input_len] = '\0';
            process_command(input_buffer);
            input_len = 0;

            if (y >= VGA_HEIGHT)
            {
                scroll_screen_syscall();
                y = VGA_HEIGHT - 1;
            }
            // новый промпт
            print_str_to_buffer("$: ", 0, y, WHITE, BLACK);
            x = 3;
            sys_setposcursor(x, y);
        }
        else if (ch == '\b' || ch == 127)
        {
            if (input_len > 0)
                backspace();
        }
        else
        {
            if (ch == INTERNAL_SPACE)
                ch = ' ';
            print_char_to_buffer(ch, x, y, WHITE, BLACK);
            x++;
            input_len++;
            if (x >= VGA_WIDTH)
            {
                x = 0;
                y++;
                if (y >= VGA_HEIGHT)
                {
                    scroll_screen_syscall();
                    y = VGA_HEIGHT - 1;
                }
            }
            sys_setposcursor(x, y);
        }
    }
}
