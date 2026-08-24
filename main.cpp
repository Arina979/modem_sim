#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cerrno>
#include <cstdlib>

bool g_echo_enabled = true;

// Структура для хранения одного правила из CSV
struct Rule {
    std::string pattern;
    std::string response;
};

std::vector<Rule> g_rules;

/**
 * Функция сопоставления шаблона с поддержкой:
 *   '.' - один любой символ
 *   '*' - ноль или более любых символов
 */
bool match_pattern(const std::string& pattern, const std::string& str) {
    size_t p = 0, s = 0;
    size_t last_s = std::string::npos;
    size_t star_p = std::string::npos;

    while (s < str.length()) {
        if (p < pattern.length() && (pattern[p] == '.' || pattern[p] == str[s])) {
            p++;
            s++;
        } else if (p < pattern.length() && pattern[p] == '*') {
            star_p = p;
            p++;
            last_s = s;
        } else if (star_p != std::string::npos) {
            p = star_p + 1;
            last_s++;
            s = last_s;
        } else {
            return false;
        }
    }

    while (p < pattern.length() && pattern[p] == '*') {
        p++;
    }

    return p == pattern.length();
}

/**
 * Заменяет спецсимвол '|' из CSV-файла на стандартную последовательность CRLF ("\r\n")
 */
std::string replace_pipe_with_crlf(const std::string& input) {
    std::string result;
    for (char c : input) {
        if (c == '|') {
            result += "\r\n";
        } else {
            result += c;
        }
    }
    return result;
}

/**
 * Считывание правил из CSV-файла вида "ожидание=ответ"
 */
bool load_rules_from_csv(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Ошибка: не удалось открыть CSV файл " << filename << std::endl;
        return false;
    }

    g_rules.clear();
    std::string line;
    while (std::getline(file, line)) {
        // Удаляем '\r' при чтении файлов с переводами строк (CRLF - \r\n)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        // Игнорируем пустые строки и комментарии
        if (line.empty() || line[0] == '#') continue;

        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string pattern = line.substr(0, pos);
            std::string raw_response = line.substr(pos + 1);

            Rule rule;
            rule.pattern = pattern;
            rule.response = replace_pipe_with_crlf(raw_response);
            g_rules.push_back(rule);
        }
    }
    file.close();
    std::cout << "Загружено правил из CSV: " << g_rules.size() << std::endl;
    return true;
}

void handle_at_command(int fd, const std::string& cmd) {
    if (cmd.empty()) return;

    // Управление программным эхом при получении команд ATE0 / ATE1
    if (cmd == "ATE0") {
        g_echo_enabled = false;
    } else if (cmd == "ATE1") {
        g_echo_enabled = true;
    }

    std::string response = "";
    bool matched = false;

    // Поиск совпадения по загруженным из CSV шаблонам
    for (const auto& rule : g_rules) {
        if (match_pattern(rule.pattern, cmd)) {
            response = rule.response;
            matched = true;
            break;
        }
    }

    if (!matched) {
        response = "ERROR\r\n";
    }

    write(fd, response.c_str(), response.length());
    
    std::cout << "\n[Входная команда]: " << cmd << std::endl;
    std::cout << "[Отправлен ответ]:\n" << response << "------------------------" << std::endl;
}

int main() {
    // Загрузка словаря ожиданий
    if (!load_rules_from_csv("at_commands.csv")) {
        std::cerr << "Предупреждение: не удалось загрузить словарь из CSV! Будет возвращаться ERROR." << std::endl;
    }

    // 1. Создаем псевдотерминал TTY Master напрямую в Linux
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master_fd < 0) {
        std::cerr << "Ошибка posix_openpt: " << std::strerror(errno) << std::endl;
        return 1;
    }

    if (grantpt(master_fd) != 0 || unlockpt(master_fd) != 0) {
        std::cerr << "Ошибка настройки TTY" << std::endl;
        close(master_fd);
        return 1;
    }

    char* pts_name = ptsname(master_fd);
    if (!pts_name) {
        std::cerr << "Не удалось получить имя псевдотерминала TTY" << std::endl;
        close(master_fd);
        return 1;
    }

    // 2. Создаем симлинк ./virtual-tty на реальное TTY-устройство (/dev/pts/X)
    const char* symlink_path = "./virtual-tty";
    unlink(symlink_path);
    if (symlink(pts_name, symlink_path) != 0) {
        std::cerr << "Ошибка создания симлинка: " << std::strerror(errno) << std::endl;
    }

    std::cout << "Виртуальный порт создан: " << pts_name << std::endl;
    std::cout << "Симлинк: " << symlink_path << std::endl;
    std::cout << "Модем запущен. Ожидание AT-команд...\n" << std::endl;

    char buffer[1024];
    std::string command_accumulator = ""; 

    while (true) {
        int num_bytes = read(master_fd, buffer, sizeof(buffer) - 1);

        if (num_bytes > 0) {
            for (int i = 0; i < num_bytes; ++i) {
                char ch = buffer[i];

                if (ch == 0) continue; // Игнорируем null-байты инициализации

                // 1. Программное эхо для отображения в терминале
                if (g_echo_enabled) {
                    if (ch == '\r' || ch == '\n') {
                        write(master_fd, "\r\n", 2);
                    } else if (ch == '\b' || ch == 127) {
                        // Эхо стирания символа (Забой - Пробел - Забой)
                        write(master_fd, "\b \b", 3);
                    } else if (ch >= 32) {
                        write(master_fd, &ch, 1);
                    }
                }

                // 2. Обработка Backspace
                if (ch == '\b' || ch == 127) {
                    if (!command_accumulator.empty()) {
                        command_accumulator.pop_back();
                    }
                    continue;
                }

                // 3. Сборка команды по нажатию Enter
                if (ch == '\r' || ch == '\n') {
                    while (!command_accumulator.empty() && 
                          (command_accumulator.back() == '\r' || command_accumulator.back() == '\n')) {
                        command_accumulator.pop_back();
                    }

                    if (!command_accumulator.empty()) {
                        handle_at_command(master_fd, command_accumulator);
                        command_accumulator.clear(); 
                    }
                } else {
                    if (ch >= 32) {
                        command_accumulator += ch;
                    }
                }
            }
        } else if (num_bytes < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "\nОшибка чтения: " << std::strerror(errno) << std::endl;
                break;
            }
        }

        usleep(10000); 
    }

    unlink(symlink_path);
    close(master_fd);
    return 0;
}