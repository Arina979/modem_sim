#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cerrno>
#include <string>
#include <cstdlib>

bool g_echo_enabled = true;

void handle_at_command(int fd, const std::string& cmd) {
    if (cmd.empty()) return;

    std::string response = "";

    if (cmd == "AT") {
        response = "OK\r\n";
    } 
    else if (cmd == "ATE0") {
        g_echo_enabled = false;
        response = "OK\r\n";
    } 
    else if (cmd == "ATE1") {
        g_echo_enabled = true;
        response = "OK\r\n";
    } 
    else if (cmd == "ATI") {
        response = "Arduino-Sim-Modem v1.0\r\nOK\r\n";
    } 
    else if (cmd == "AT+COPS?") { 
        response = "+COPS: 0,0,\"Virtual-Network\"\r\nOK\r\n";
    } 
    else if (cmd == "AT+CPIN?") { 
        response = "+CPIN: READY\r\nOK\r\n";
    } 
    else {
        response = "ERROR\r\n";
    }

    write(fd, response.c_str(), response.length());
    
    std::cout << "\n[Входная команда]: " << cmd << std::endl;
    std::cout << "[Отправлен ответ]:\n" << response << "------------------------" << std::endl;
}

int main() {
    // 1. Создаем PTY Master напрямую в Linux (без использования socat)
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (master_fd < 0) {
        std::cerr << "Ошибка posix_openpt: " << std::strerror(errno) << std::endl;
        return 1;
    }

    if (grantpt(master_fd) != 0 || unlockpt(master_fd) != 0) {
        std::cerr << "Ошибка настройки PTY" << std::endl;
        close(master_fd);
        return 1;
    }

    char* pts_name = ptsname(master_fd);
    if (!pts_name) {
        std::cerr << "Не удалось получить имя PTY slave" << std::endl;
        close(master_fd);
        return 1;
    }

    // 2. Создаем симлинк ./virtual-tty на реальное PTY-устройство (/dev/pts/X)
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