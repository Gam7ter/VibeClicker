#include <windows.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>

#define MAX_ACTIONS 256
#define CONFIG_FILE "clicker_config.dat"

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

Action script[MAX_ACTIONS];
int script_size = 0;
WORD key_start = VK_F6;
WORD key_stop = VK_F7;

bool is_running = false;
bool in_main_menu = false;
HANDLE hThread = NULL;

void SaveConfig();
void LoadConfig();
void DisplayScript();
void DisplayMenu();
void DisplayAsciiKeyboard();
int ConfigureAction(Action* out_actions);
void AddAction();
void DeleteAction();
void ModifyAction();
void ChangeHotkeys();
void GetKeyNameStr(WORD vk, char* dest, int maxLen);
void PreciseSleep(double ms);
DWORD WINAPI MacroThreadProc(LPVOID lpParam);

void PreciseSleep(double ms) {
    LARGE_INTEGER freq, start, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    double target_ticks = (ms / 1000.0) * freq.QuadPart;

    while (is_running) {
        if (GetAsyncKeyState(key_stop) & 0x8000) {
            is_running = false;
            break;
        }
        QueryPerformanceCounter(&now);
        if ((now.QuadPart - start.QuadPart) >= target_ticks) break;
        Sleep(0);
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
    while (true) {
        if (in_main_menu) {
            if ((GetAsyncKeyState(key_start) & 0x8000) && !is_running) {
                is_running = true;
                printf("\n[Скрипт ЗАПУЩЕН]\n");
            }
            if (GetAsyncKeyState(key_stop) & 0x8000) {
                is_running = false;
            }
        }

        if (is_running && script_size > 0) {
            for (int i = 0; i < script_size && is_running; i++) {
                if (!in_main_menu) {
                    is_running = false; 
                    break; 
                }
                
                Action act = script[i];
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

void SaveConfig() {
    FILE* f = NULL;
    if (fopen_s(&f, CONFIG_FILE, "wb") == 0) {
        fwrite(&key_start, sizeof(WORD), 1, f);
        fwrite(&key_stop, sizeof(WORD), 1, f);
        fwrite(&script_size, sizeof(int), 1, f);
        if (script_size > 0) {
            fwrite(script, sizeof(Action), script_size, f);
        }
        fclose(f);
    }
}

void LoadConfig() {
    FILE* f = NULL;
    if (fopen_s(&f, CONFIG_FILE, "rb") == 0) {
        fread(&key_start, sizeof(WORD), 1, f);
        fread(&key_stop, sizeof(WORD), 1, f);
        fread(&script_size, sizeof(int), 1, f);
        if (script_size > MAX_ACTIONS) script_size = MAX_ACTIONS;
        if (script_size > 0) {
            fread(script, sizeof(Action), script_size, f);
        }
        fclose(f);
    }
}

void DisplayScript() {
    char keyStartName[32], keyStopName[32];
    GetKeyNameStr(key_start, keyStartName, 32);
    GetKeyNameStr(key_stop, keyStopName, 32);

    printf("==================================================\n");
    printf(" ТЕКУЩИЙ СКРИПТ (Старт: %s | Стоп: %s)\n", keyStartName, keyStopName);
    printf("==================================================\n");
    
    if (script_size == 0) {
        printf(" [Скрипт пуст]\n");
    } else {
        for (int i = 0; i < script_size; i++) {
            printf(" %d. ", i + 1);
            char keyName[32];
            switch (script[i].type) {
                case ACTION_KEYBOARD:
                    GetKeyNameStr(script[i].vKey, keyName, 32);
                    printf("Клавиатура: %s [%s]\n", keyName, script[i].isKeyDown ? "НАЖАТЬ" : "ОТПУСТИТЬ");
                    break;
                case ACTION_MOUSE:
                    printf("Мышь: %s %s\n", 
                           script[i].mouseButton == 1 ? "ЛЕВАЯ" : "ПРАВАЯ", 
                           script[i].isKeyDown ? "НАЖАТЬ" : "ОТПУСТИТЬ");
                    break;
                case ACTION_DELAY:
                    printf("Задержка: %lu мс\n", script[i].delayMs);
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
    printf("[10:Shft]    [5A:Z] [58:X] [43:C] [56:V] [42:B] [4E:N] [4D:M]                                      [60:0] [6E:.] [6B:+]\n");
    printf("[11:Ctrl] [12:Alt]       [20:             Space             ]                           \n\n");

    printf("==============================================================================================\n");
}

int ConfigureAction(Action* out_actions) {
    printf("\nВыберите тип действия:\n1. Клавиатура\n2. Мышь\n3. Задержка\n0. Назад в меню\nВыбор: ");
    int typeChoice;
    scanf_s("%d", &typeChoice);

    if (typeChoice == 0) return 0; 

    if (typeChoice == 1) {
        DisplayAsciiKeyboard();
        printf("\nВведите HEX-код нужной клавиши из таблицы выше: ");
        unsigned int hexKey;
        scanf_s("%x", &hexKey);

        printf("Выберите событие:\n1. Нажатие (KeyDown)\n2. Отпускание (KeyUp)\n3. Нажатие и отпускание (Клик)\n0. Назад в меню\nВыбор: ");
        int stateChoice;
        scanf_s("%d", &stateChoice);
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
        int mouseChoice;
        scanf_s("%d", &mouseChoice);
        if (mouseChoice == 0) return 0; 
        
        printf("Выберите событие:\n1. Нажатие (MouseDown)\n2. Отпускание (MouseUp)\n3. Нажатие и отпускание (Клик)\n0. Назад в меню\nВыбор: ");
        int stateChoice;
        scanf_s("%d", &stateChoice);
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
        scanf_s("%lu", &(out_actions[0].delayMs));
        return 1;
    }
    return 0;
}

void AddAction() {
    if (script_size >= MAX_ACTIONS) {
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
    
    if (script_size + count > MAX_ACTIONS) {
        printf("Ошибка: недостаточно места в скрипте!\n");
        _getch();
        return;
    }

    for (int i = 0; i < count; i++) {
        script[script_size] = buffer[i];
        script_size++;
    }
    
    SaveConfig();
    printf("Действие(я) успешно добавлено(ы)!\n");
    Sleep(1000);
}

void DeleteAction() {
    if (script_size == 0) {
        printf("Скрипт пуст, нечего удалять.\n");
        _getch();
        return;
    }
    printf("Введите номер действия для удаления (от 1 до %d) или 0 для отмены: ", script_size);
    int index;
    scanf_s("%d", &index);

    if (index == 0) return;

    if (index < 1 || index > script_size) {
        printf("Неверный номер действия!\n");
        _getch();
        return;
    }

    for (int i = index - 1; i < script_size - 1; i++) {
        script[i] = script[i + 1];
    }
    script_size--;
    SaveConfig();
    printf("Действие удалено!\n");
    Sleep(1000);
}

void ModifyAction() {
    if (script_size == 0) {
        printf("Скрипт пуст, нечего изменять.\n");
        _getch();
        return;
    }
    printf("Введите номер действия для изменения (от 1 до %d) или 0 для отмены: ", script_size);
    int index;
    scanf_s("%d", &index);

    if (index == 0) return; 

    if (index < 1 || index > script_size) {
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
        script[index - 1] = buffer[0];
    } 
    else if (count == 2) {
        if (script_size >= MAX_ACTIONS) {
            printf("Ошибка: невозможно расширить скрипт, лимит!\n");
            _getch();
            return;
        }
        for (int i = script_size; i > index; i--) {
            script[i] = script[i - 1];
        }
        script[index - 1] = buffer[0];
        script[index] = buffer[1];
        script_size++;
    }

    SaveConfig();
    printf("Действие успешно обновлено!\n");
    Sleep(1000);
}

void ChangeHotkeys() {
    DisplayAsciiKeyboard();
    unsigned int hexKey;
    
    printf("\nВведите HEX-код для новой клавиши СТАРТА: ");
    scanf_s("%x", &hexKey);
    key_start = (WORD)hexKey;

    printf("Введите HEX-код для новой клавиши СТОПА: ");
    scanf_s("%x", &hexKey);
    key_stop = (WORD)hexKey;

    SaveConfig();
    printf("Горячие клавиши обновлены!\n");
    Sleep(1000);
}

int main() {

    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);

    LoadConfig();

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
        printf("5. Выйти из программы\n");
        printf("\nВыберите пункт меню: ");

        in_main_menu = true; 

        char choice = '0';
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
            break;
        }
    }

    if (hThread) TerminateThread(hThread, 0);
    return 0;
}