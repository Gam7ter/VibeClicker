#include <windows.h>
#include <mmsystem.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <string.h>

#define MAX_ACTIONS 256
#define MAX_PROFILES 10
#define MAX_NAME_LEN 32
#define PROFILES_DIR "profiles"
#define INDEX_FILE "profiles/profiles.dat"

typedef enum {
    ACTION_KEYBOARD,
    ACTION_MOUSE,
    ACTION_DELAY
} ActionType;

typedef struct {
    ActionType type;
    WORD vKey;
    bool isKeyDown;
    int mouseButton;
    DWORD delayMs;
} Action;

typedef struct {
    char name[MAX_NAME_LEN];
    WORD key_start;
    WORD key_stop;
    int script_size;
    Action script[MAX_ACTIONS];
} Profile;

// Глобальные переменные профилей
Profile current_profile;
int current_profile_index = 0;
char profile_names[MAX_PROFILES][MAX_NAME_LEN];

volatile BOOL is_thread_active = TRUE;
bool is_running = false;
bool in_main_menu = false;
HANDLE hThread = NULL;

// Прототипы функций
void SaveCurrentProfile();
void LoadProfile(int index);
void SaveProfileIndex();
void LoadProfileIndex();
void ManageProfiles();
void DisplayScript();
void DisplayAsciiKeyboard();
int ConfigureAction(Action* out_actions);
void AddAction();
void DeleteAction();
void ModifyAction();
void ChangeHotkeys();
void GetKeyNameStr(WORD vk, char* dest, int maxLen);
void PreciseSleep(double ms);
void ReadInputString(char* dest, int maxLen);
int ReadIntInput();
unsigned int ReadHexInput();
DWORD WINAPI MacroThreadProc(LPVOID lpParam);

// Надежно считывает строку напрямую через API Windows Unicode
void ReadInputString(char* dest, int maxLen) {
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    FlushConsoleInputBuffer(hInput);
    
    WCHAR wbuf[512] = {0};
    DWORD charsRead = 0;

    DWORD mode;
    GetConsoleMode(hInput, &mode);
    SetConsoleMode(hInput, mode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);

    if (ReadConsoleW(hInput, wbuf, 511, &charsRead, NULL)) {
        for (DWORD i = 0; i < charsRead; i++) {
            if (wbuf[i] == L'\r' || wbuf[i] == L'\n') {
                wbuf[i] = L'\0';
                break;
            }
        }
        WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, dest, maxLen, NULL, NULL);
    } else {
        dest[0] = '\0';
    }
}

// Безопасное чтение целых чисел
int ReadIntInput() {
    char buf[64] = {0};
    ReadInputString(buf, sizeof(buf));
    return atoi(buf);
}

// Безопасное чтение HEX-значений
unsigned int ReadHexInput() {
    char buf[64] = {0};
    ReadInputString(buf, sizeof(buf));
    return (unsigned int)strtoul(buf, NULL, 16);
}

void PreciseSleep(double ms) {
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    double target_ticks = (ms / 1000.0) * freq.QuadPart;

    while (is_running && is_thread_active) {
        if (GetAsyncKeyState(current_profile.key_stop) & 0x8000) {
            is_running = false;
            break;
        }

        QueryPerformanceCounter(&now);
        double elapsed_ticks = (double)(now.QuadPart - start.QuadPart);
        double remaining_ticks = target_ticks - elapsed_ticks;

        if (remaining_ticks <= 0) break;

        double remaining_ms = (remaining_ticks / freq.QuadPart) * 1000.0;

        if (remaining_ms > 1.5) {
            Sleep(1); 
        } else {
            Sleep(0); 
        }
    }
}

void GetKeyNameStr(WORD vk, char* dest, int maxLen) {
    if (vk >= 0x30 && vk <= 0x39) sprintf_s(dest, maxLen, "%c", (char)vk);
    else if (vk >= 0x41 && vk <= 0x5A) sprintf_s(dest, maxLen, "%c", (char)vk);
    else if (vk >= VK_F1 && vk <= VK_F12) sprintf_s(dest, maxLen, "F%d", vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) sprintf_s(dest, maxLen, "Num%d", vk - VK_NUMPAD0);
    else {
        switch (vk) {
            case VK_ESCAPE: sprintf_s(dest, maxLen, "Esc"); break;
            case VK_SPACE: sprintf_s(dest, maxLen, "Space"); break;
            case VK_RETURN: sprintf_s(dest, maxLen, "Enter"); break;
            case VK_BACK: sprintf_s(dest, maxLen, "Backspace"); break;
            case VK_TAB: sprintf_s(dest, maxLen, "Tab"); break;
            case VK_SHIFT: sprintf_s(dest, maxLen, "Shift"); break;
            case VK_CONTROL: sprintf_s(dest, maxLen, "Ctrl"); break;
            case VK_MENU: sprintf_s(dest, maxLen, "Alt"); break;
            case VK_MULTIPLY: sprintf_s(dest, maxLen, "Num*"); break;
            case VK_ADD: sprintf_s(dest, maxLen, "Num+"); break;
            case VK_SUBTRACT: sprintf_s(dest, maxLen, "Num-"); break;
            case VK_DECIMAL: sprintf_s(dest, maxLen, "Num."); break;
            case VK_DIVIDE: sprintf_s(dest, maxLen, "Num/"); break;
            default: sprintf_s(dest, maxLen, "VK:0x%X", vk); break;
        }
    }
}

DWORD WINAPI MacroThreadProc(LPVOID lpParam) {
    while (is_thread_active) {
        if (in_main_menu) {
            if ((GetAsyncKeyState(current_profile.key_start) & 0x8000) && !is_running) {
                is_running = true;
            }
            if (GetAsyncKeyState(current_profile.key_stop) & 0x8000) {
                is_running = false;
            }
        }

        if (is_running && current_profile.script_size > 0) {
            for (int i = 0; i < current_profile.script_size && is_running; i++) {
                if (!in_main_menu) {
                    is_running = false; 
                    break; 
                }
                
                Action act = current_profile.script[i];
                if (act.type == ACTION_KEYBOARD) {
                    INPUT input = {0};
                    input.type = INPUT_KEYBOARD;
                    input.ki.wVk = act.vKey;
                    if (!act.isKeyDown) input.ki.dwFlags = KEYEVENTF_KEYUP;
                    SendInput(1, &input, sizeof(INPUT));
                } 
                else if (act.type == ACTION_MOUSE) {
                    INPUT input = {0};
                    input.type = INPUT_MOUSE;
                    if (act.mouseButton == 1) {
                        input.mi.dwFlags = act.isKeyDown ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
                    } else {
                        input.mi.dwFlags = act.isKeyDown ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
                    }
                    SendInput(1, &input, sizeof(INPUT));
                } 
                else if (act.type == ACTION_DELAY) {
                    PreciseSleep(act.delayMs);
                }
                Sleep(0);
            }
        } else {
            Sleep(10);
        }
    }
    return 0;
}

// ----------------- РАБОТА С ПРОФИЛЯМИ И ФАЙЛАМИ -----------------

void GetProfileFilename(int index, char* filename, size_t size) {
    sprintf_s(filename, size, "profiles/profile_%d.dat", index);
}

void SaveProfileIndex() {
    FILE* f = NULL;
    if (fopen_s(&f, INDEX_FILE, "wb") == 0) {
        fwrite(&current_profile_index, sizeof(int), 1, f);
        fwrite(profile_names, sizeof(profile_names), 1, f);
        fclose(f);
    }
}

void LoadProfileIndex() {
    FILE* f = NULL;
    if (fopen_s(&f, INDEX_FILE, "rb") == 0) {
        fread(&current_profile_index, sizeof(int), 1, f);
        fread(profile_names, sizeof(profile_names), 1, f);
        fclose(f);
    } else {
        current_profile_index = 0;
        for (int i = 0; i < MAX_PROFILES; i++) {
            sprintf_s(profile_names[i], MAX_NAME_LEN, "Профиль %d", i + 1);
        }
        SaveProfileIndex();
    }
}

void SaveCurrentProfile() {
    char filename[64];
    GetProfileFilename(current_profile_index, filename, sizeof(filename));
    
    FILE* f = NULL;
    if (fopen_s(&f, filename, "wb") == 0) {
        fwrite(&current_profile, sizeof(Profile), 1, f);
        fclose(f);
    }
    SaveProfileIndex();
}

void LoadProfile(int index) {
    if (index < 0 || index >= MAX_PROFILES) return;
    
    current_profile_index = index;
    char filename[64];
    GetProfileFilename(index, filename, sizeof(filename));
    
    FILE* f = NULL;
    if (fopen_s(&f, filename, "rb") == 0) {
        fread(&current_profile, sizeof(Profile), 1, f);
        fclose(f);
    } else {
        memset(&current_profile, 0, sizeof(Profile));
        strcpy_s(current_profile.name, MAX_NAME_LEN, profile_names[index]);
        current_profile.key_start = VK_F6;
        current_profile.key_stop = VK_F7;
        current_profile.script_size = 0;
        SaveCurrentProfile();
    }
}

void ManageProfiles() {
    while (true) {
        system("cls");
        printf("==================================================\n");
        printf(" МЕНЕДЖЕР ПРОФИЛЕЙ\n");
        printf("==================================================\n");
        for (int i = 0; i < MAX_PROFILES; i++) {
            printf(" %2d. %s %s\n", i + 1, profile_names[i], 
                   (i == current_profile_index) ? "[АКТИВЕН]" : "");
        }
        printf("--------------------------------------------------\n");
        printf(" [1-9, 0] - Сменить активный профиль (1..10)\n");
        printf(" [R / К]  - Переименовать профиль\n");
        printf(" [ESC]    - Назад в главное меню\n");
        printf("==================================================\n");
        printf("Выберите действие: ");

        int c = _getch();

        // Выход по Esc или 'b'/'q'
        if (c == 27 || c == 'b' || c == 'B' || c == 'q' || c == 'Q') {
            break;
        }

        // Проверка клавиши R/К на любой раскладке
        bool is_rename = (c == 'r' || c == 'R' || 
                          (unsigned char)c == 0xAA || (unsigned char)c == 0xEA || 
                          (unsigned char)c == 0x8A || (unsigned char)c == 0xCA ||
                          (unsigned char)c == 0xD0 || (unsigned char)c == 0xD1 ||
                          (unsigned char)c == 0xBA || (unsigned char)c == 0xDA);

        if (!is_rename && (GetAsyncKeyState('R') & 0x8000)) {
            is_rename = true;
        }

        if (is_rename) {
            printf("\n\nВведите номер профиля для переименования (1-%d): ", MAX_PROFILES);
            
            int idx = ReadIntInput();

            if (idx >= 1 && idx <= MAX_PROFILES) {
                printf("Введите новое название (нажмите Enter, чтобы оставить \"%s\"): ", profile_names[idx - 1]);
                
                char temp_name[MAX_NAME_LEN] = {0};
                ReadInputString(temp_name, sizeof(temp_name));

                bool is_empty = true;
                for (int k = 0; temp_name[k] != '\0'; k++) {
                    if ((unsigned char)temp_name[k] > 32) {
                        is_empty = false;
                        break;
                    }
                }

                if (!is_empty) {
                    strcpy_s(profile_names[idx - 1], MAX_NAME_LEN, temp_name);
                    if (idx - 1 == current_profile_index) {
                        strcpy_s(current_profile.name, MAX_NAME_LEN, temp_name);
                        SaveCurrentProfile();
                    } else {
                        SaveProfileIndex();
                    }
                    printf("Название профиля обновлено!\n");
                } else {
                    printf("Название оставлено без изменений.\n");
                }
            } else {
                printf("Неверный номер профиля!\n");
            }
            Sleep(1000);
        }
        else if (c >= '1' && c <= '9') {
            int idx = c - '1';
            LoadProfile(idx);
            printf("\n\nАктивирован профиль: %s\n", current_profile.name);
            Sleep(700);
        }
        else if (c == '0') {
            int idx = 9; // Профиль 10
            LoadProfile(idx);
            printf("\n\nАктивирован профиль: %s\n", current_profile.name);
            Sleep(700);
        }
    }
}

// ----------------------------------------------------------------

void DisplayScript() {
    char keyStartName[32], keyStopName[32];
    GetKeyNameStr(current_profile.key_start, keyStartName, 32);
    GetKeyNameStr(current_profile.key_stop, keyStopName, 32);

    printf("==================================================\n");
    printf(" ПРОФИЛЬ: %s\n", current_profile.name);
    printf(" (Старт: %s | Стоп: %s)\n", keyStartName, keyStopName);
    printf("==================================================\n");
    
    if (current_profile.script_size == 0) {
        printf(" [Скрипт пуст]\n");
    } else {
        for (int i = 0; i < current_profile.script_size; i++) {
            printf(" %d. ", i + 1);
            char keyName[32];
            switch (current_profile.script[i].type) {
                case ACTION_KEYBOARD:
                    GetKeyNameStr(current_profile.script[i].vKey, keyName, 32);
                    printf("Клавиатура: %s [%s]\n", keyName, current_profile.script[i].isKeyDown ? "НАЖАТЬ" : "ОТПУСТИТЬ");
                    break;
                case ACTION_MOUSE:
                    printf("Мышь: %s %s\n", 
                           current_profile.script[i].mouseButton == 1 ? "ЛЕВАЯ" : "ПРАВАЯ", 
                           current_profile.script[i].isKeyDown ? "НАЖАТЬ" : "ОТПУСТИТЬ");
                    break;
                case ACTION_DELAY:
                    printf("Задержка: %lu мс\n", current_profile.script[i].delayMs);
                    break;
            }
        }
    }
    printf("==================================================\n");
}

void DisplayAsciiKeyboard() {
    printf("\n=================================== ASCII КАРТА КЛАВИАТУРЫ ===================================\n");
    printf(" Введите HEX-код нужной клавиши (например: 41 для 'A', 20 для Space)\n");
    printf("==============================================================================================\n\n");
    
    printf("[1B:Esc]    [70:F1] [71:F2] [72:F3] [73:F4]  [74:F5] [75:F6] [76:F7] [77:F8]  [78:F9] [79:F10] [7A:F11] [7B:F12]\n\n");
    printf("     [31:1] [32:2] [33:3]  [34:4]  [35:5] [36:6] [37:7] [38:8] [39:9] [30:0] [08:Backspace] [67:7] [68:8] [69:9] [6F:/]\n");
    printf("[09:Tab] [51:Q] [57:W] [45:E] [52:R] [54:T] [59:Y] [55:U] [49:I] [4F:O] [50:P]              [64:4] [65:5] [66:6] [6A:*]\n");
    printf("[14:Caps] [41:A] [53:S] [44:D] [46:F] [47:G] [48:H] [4A:J] [4B:K] [4C:L] [0D:Ent]           [61:1] [62:2] [63:3] [6D:-]\n");
    printf("[10:Shft]    [5A:Z] [58:X] [43:C] [56:V] [42:B] [4E:N] [4D:M]                                    [60:0] [6E:.] [6B:+]\n");
    printf("[11:Ctrl] [12:Alt]       [20:               Space             ]                               \n\n");

    printf("==============================================================================================\n");
}

int ConfigureAction(Action* out_actions) {
    printf("\nВыберите тип действия:\n1. Клавиатура\n2. Мышь\n3. Задержка\n0. Назад в меню\nВыбор: ");
    int typeChoice = ReadIntInput();

    if (typeChoice == 0) return 0; 

    if (typeChoice == 1) {
        DisplayAsciiKeyboard();
        printf("\nВведите HEX-код нужной клавиши из таблицы выше: ");
        unsigned int hexKey = ReadHexInput();

        printf("Выберите событие:\n1. Нажатие (KeyDown)\n2. Отпускание (KeyUp)\n3. Нажатие и отпускание (Клик)\n0. Назад в меню\nВыбор: ");
        int stateChoice = ReadIntInput();
        if (stateChoice == 0) return 0; 
        
        if (stateChoice == 1 || stateChoice == 2) {
            out_actions[0].type = ACTION_KEYBOARD;
            out_actions[0].vKey = (WORD)hexKey;
            out_actions[0].isKeyDown = (stateChoice == 1);
            return 1;
        } else if (stateChoice == 3) {
            out_actions[0].type = ACTION_KEYBOARD; out_actions[0].vKey = (WORD)hexKey; out_actions[0].isKeyDown = true;
            out_actions[1].type = ACTION_KEYBOARD; out_actions[1].vKey = (WORD)hexKey; out_actions[1].isKeyDown = false;
            return 2;
        }
    } 
    else if (typeChoice == 2) {
        printf("Выберите кнопку мыши:\n1. Левая\n2. Правая\n0. Назад в меню\nВыбор: ");
        int mouseChoice = ReadIntInput();
        if (mouseChoice == 0) return 0; 
        
        printf("Выберите событие:\n1. Нажатие (MouseDown)\n2. Отпускание (MouseUp)\n3. Нажатие и отпускание (Клик)\n0. Назад в меню\nВыбор: ");
        int stateChoice = ReadIntInput();
        if (stateChoice == 0) return 0; 
        
        if (stateChoice == 1 || stateChoice == 2) {
            out_actions[0].type = ACTION_MOUSE;
            out_actions[0].mouseButton = mouseChoice;
            out_actions[0].isKeyDown = (stateChoice == 1);
            return 1;
        } else if (stateChoice == 3) {
            out_actions[0].type = ACTION_MOUSE; out_actions[0].mouseButton = mouseChoice; out_actions[0].isKeyDown = true;
            out_actions[1].type = ACTION_MOUSE; out_actions[1].mouseButton = mouseChoice; out_actions[1].isKeyDown = false;
            return 2;
        }
    } 
    else if (typeChoice == 3) {
        out_actions[0].type = ACTION_DELAY;
        printf("Введите время задержки в миллисекундах (мс): ");
        out_actions[0].delayMs = (DWORD)ReadIntInput();
        return 1;
    }
    return 0;
}

void AddAction() {
    if (current_profile.script_size >= MAX_ACTIONS) {
        printf("Достигнут предел количества действий!\n");
        _getch();
        return;
    }
    
    Action buffer[2];
    int count = ConfigureAction(buffer);
    
    if (count == 0) {
        printf("Добавление отменено, возврат в меню...\n");
        Sleep(1000);
        return;
    }
    
    if (current_profile.script_size + count > MAX_ACTIONS) {
        printf("Ошибка: недостаточно места в скрипте!\n");
        _getch();
        return;
    }

    for (int i = 0; i < count; i++) {
        current_profile.script[current_profile.script_size] = buffer[i];
        current_profile.script_size++;
    }
    
    SaveCurrentProfile();
    printf("Действие(я) успешно добавлено(ы)!\n");
    Sleep(1000);
}

void DeleteAction() {
    if (current_profile.script_size == 0) {
        printf("Скрипт пуст, нечего удалять.\n");
        _getch();
        return;
    }
    printf("Введите номер действия для удаления (от 1 до %d) или 0 для отмены: ", current_profile.script_size);
    int index = ReadIntInput();

    if (index == 0) return;

    if (index < 1 || index > current_profile.script_size) {
        printf("Неверный номер действия!\n");
        _getch();
        return;
    }

    for (int i = index - 1; i < current_profile.script_size - 1; i++) {
        current_profile.script[i] = current_profile.script[i + 1];
    }
    current_profile.script_size--;
    SaveCurrentProfile();
    printf("Действие удалено!\n");
    Sleep(1000);
}

void ModifyAction() {
    if (current_profile.script_size == 0) {
        printf("Скрипт пуст, нечего изменять.\n");
        _getch();
        return;
    }
    printf("Введите номер действия для изменения (от 1 до %d) или 0 для отмены: ", current_profile.script_size);
    int index = ReadIntInput();

    if (index == 0) return; 

    if (index < 1 || index > current_profile.script_size) {
        printf("Неверный номер действия!\n");
        _getch();
        return;
    }

    printf("Перенастройка действия №%d:\n", index);
    Action buffer[2];
    int count = ConfigureAction(buffer);
    
    if (count == 0) {
        printf("Изменение отменено, возврат в меню...\n");
        Sleep(1000);
        return;
    }

    if (count == 1) {
        current_profile.script[index - 1] = buffer[0];
    } 
    else if (count == 2) {
        if (current_profile.script_size >= MAX_ACTIONS) {
            printf("Ошибка: невозможно расширить скрипт, лимит!\n");
            _getch();
            return;
        }
        for (int i = current_profile.script_size; i > index; i--) {
            current_profile.script[i] = current_profile.script[i - 1];
        }
        current_profile.script[index - 1] = buffer[0];
        current_profile.script[index] = buffer[1];
        current_profile.script_size++;
    }

    SaveCurrentProfile();
    printf("Действие успешно обновлено!\n");
    Sleep(1000);
}

void ChangeHotkeys() {
    DisplayAsciiKeyboard();
    
    printf("\nВведите HEX-код для новой клавиши СТАРТА: ");
    unsigned int hexKey = ReadHexInput();
    current_profile.key_start = (WORD)hexKey;

    printf("Введите HEX-код для новой клавиши СТОПА: ");
    hexKey = ReadHexInput();
    current_profile.key_stop = (WORD)hexKey;

    SaveCurrentProfile();
    printf("Горячие клавиши обновлены!\n");
    Sleep(1000);
}

int main() {
    timeBeginPeriod(1);

    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    CreateDirectoryA(PROFILES_DIR, NULL);

    LoadProfileIndex();
    LoadProfile(current_profile_index);

    hThread = CreateThread(NULL, 0, MacroThreadProc, NULL, 0, NULL);

    while (true) {
        system("cls");
        DisplayScript();
        
        if (is_running) {
            printf(" >>> КЛИКЕР СЕЙЧАС АКТИВЕН <<<\n");
        }

        printf("ГЛАВНОЕ МЕНЮ:\n");
        printf("1. Добавить действие\n");
        printf("2. Удалить действие\n");
        printf("3. Изменить действие\n");
        printf("4. Изменить клавиши активации/деактивации\n");
        printf("5. Управление профилями\n");
        printf("[ESC] Выйти из программы\n");
        printf("\nВыберите пункт меню: ");

        in_main_menu = true; 

        char choice = 0;
        while (!_kbhit()) {
            Sleep(50);
            static bool last_state = false;
            if (is_running != last_state) {
                last_state = is_running;
                break; 
            }
        }
        
        if (_kbhit()) {
            choice = _getch();
        } else {
            continue; 
        }

        in_main_menu = false; 

        if (choice == '1') {
            system("cls");
            AddAction();
        } else if (choice == '2') {
            system("cls");
            DeleteAction();
        } else if (choice == '3') {
            system("cls");
            ModifyAction();
        } else if (choice == '4') {
            system("cls");
            ChangeHotkeys();
        } else if (choice == '5') {
            system("cls");
            ManageProfiles();
        } else if (choice == 27 || choice == '6' || choice == 'q' || choice == 'Q') {
            break;
        }
    }

    if (hThread) {
        is_thread_active = FALSE;
        WaitForSingleObject(hThread, 1000); 
        CloseHandle(hThread); 
    }
    timeEndPeriod(1);
    return 0;
}