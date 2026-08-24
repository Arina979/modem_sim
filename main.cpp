#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cerrno>
#include <string>

//ошибка: теряется первый вводимый символ

// Флаг программного эха (управляемый ATE0/ATE1)
bool echo_enabled = true;

void handle_at_command(int fd, const std::string& cmd) {
    if (cmd.empty()) return;

    std::string response = "";

    if (cmd == "AT") {
        response = "OK\r\n";
    } 
    else if (cmd == "ATE0") {
        echo_enabled = false; // Меняем флаг без вызова tcsetattr
        response = "OK\r\n";
    } 
    else if (cmd == "ATE1") {
        echo_enabled = true;
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
    const char* port_path = "./arduino-sim"; 

    int serial_port = open(port_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_port < 0) {
        std::cerr << "Ошибка открытия порта: " << std::strerror(errno) << std::endl;
        return 1;
    }
    std::cout << "Потр открыт!" << std::endl;

    struct termios tty;
    if (tcgetattr(serial_port, &tty) != 0) {
        std::cerr << "Ошибка tcgetattr: " << std::strerror(errno) << std::endl;
        close(serial_port);
        return 1;
    }

    // Переводим порт в чистый RAW-режим (отключаем ECHO, ICANON, ICRNL, OPOST ядра)
    cfmakeraw(&tty);

    tty.c_cc[VMIN] = 0;  
    tty.c_cc[VTIME] = 0; 

    if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
        std::cerr << "Ошибка tcsetattr: " << std::strerror(errno) << std::endl;
        close(serial_port);
        return 1;
    }

    // Очищаем стартовый мусор после открытия порта socat/screen
    tcflush(serial_port, TCIOFLUSH);

    std::cout << "Модем запущен (Программное эхо). Ожидание AT-команд..." << std::endl;

    char buffer[1024];
    std::string command_accumulator = ""; 

    while (true) {
        int num_bytes = read(serial_port, buffer, sizeof(buffer) - 1);

        if (num_bytes > 0) {
            for (int i = 0; i < num_bytes; ++i) {
                char ch = buffer[i];

                // 1. Программное эхо символов в screen
                if (echo_enabled) {
                    if (ch == '\r' || ch == '\n') {
                        write(serial_port, "\r\n", 2);
                    } else if (ch >= 32 || ch == '\b' || ch == 127) {
                        write(serial_port, &ch, 1);
                    }
                }

                // 2. Обработка Backspace
                if (ch == '\b' || ch == 127) {
                    if (!command_accumulator.empty()) {
                        command_accumulator.pop_back();
                    }
                    continue;
                }

                // 3. Накопление и обработка команды
                if (ch == '\r' || ch == '\n') {
                    while (!command_accumulator.empty() && 
                          (command_accumulator.back() == '\r' || command_accumulator.back() == '\n')) {
                        command_accumulator.pop_back();
                    }

                    if (!command_accumulator.empty()) {
                        handle_at_command(serial_port, command_accumulator);
                        command_accumulator.clear(); 
                    }
                } else {
                    if (ch != ' ' && ch >= 32) {
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

    close(serial_port);
    return 0;
}