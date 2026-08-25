#include "AtModemSimulator.hpp"

#include <iostream>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <csignal>

static void write_data(int fd, const void* buf, size_t count) {
    if (fd < 0) return;
    ssize_t bytes_written = write(fd, buf, count);
    if (bytes_written < 0) {
        std::cerr << "Ошибка записи в TTY: " << std::strerror(errno) << std::endl;
    }
}

std::atomic<bool> AtModemSimulator::s_stop_requested{false};

AtModemSimulator::~AtModemSimulator() {
    stop();
}

void AtModemSimulator::handle_signal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        s_stop_requested = true;
    }
}

void AtModemSimulator::setup_signal_handler() {
    struct sigaction sa{};
    sa.sa_handler = AtModemSimulator::handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

bool AtModemSimulator::load_rules_from_csv(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Ошибка: не удалось открыть CSV файл " << filename << std::endl;
        return false;
    }

    m_rules.clear();
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty() || line[0] == '#') continue;

        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            Rule rule;
            rule.pattern = line.substr(0, pos);
            rule.response = replace_pipe_with_crlf(line.substr(pos + 1));
            m_rules.push_back(rule);
        }
    }
    file.close();
    std::cout << "Загружено правил из CSV: " << m_rules.size() << std::endl;
    return true;
}

bool AtModemSimulator::start(const std::string& symlink_path) {
    s_stop_requested = false;
    setup_signal_handler();

    m_master_fd = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_master_fd < 0) {
        std::cerr << "Ошибка posix_openpt: " << std::strerror(errno) << std::endl;
        return false;
    }

    if (grantpt(m_master_fd) != 0 || unlockpt(m_master_fd) != 0) {
        std::cerr << "Ошибка настройки TTY" << std::endl;
        stop();
        return false;
    }

    char* pts_name = ptsname(m_master_fd);
    if (!pts_name) {
        std::cerr << "Не удалось получить имя псевдотерминала TTY" << std::endl;
        stop();
        return false;
    }

    m_symlink_path = symlink_path;
    unlink(m_symlink_path.c_str());
    if (symlink(pts_name, m_symlink_path.c_str()) != 0) {
        std::cerr << "Ошибка создания симлинка: " << std::strerror(errno) << std::endl;
    }

    std::cout << "Виртуальный порт создан: " << pts_name << std::endl;
    std::cout << "Симлинк: " << m_symlink_path << std::endl;
    std::cout << "Модем запущен. Ожидание AT-команд (Ctrl+C для выхода)...\n" << std::endl;
    return true;
}

void AtModemSimulator::run() {
    if (m_master_fd < 0) return;

    m_running = true;
    char buffer[1024];

    while (m_running && !s_stop_requested) {
        int num_bytes = read(m_master_fd, buffer, sizeof(buffer) - 1);

        if (num_bytes > 0) {
            for (int i = 0; i < num_bytes; ++i) {
                process_char(buffer[i]);
            }
        } else if (num_bytes < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "\nОшибка чтения: " << std::strerror(errno) << std::endl;
                break;
            }
        }

        usleep(10000);
    }

    std::cout << "\nПолучен сигнал завершения. Завершение работы..." << std::endl;
    stop();
}

void AtModemSimulator::stop() {
    m_running = false;
    if (m_master_fd >= 0) {
        close(m_master_fd);
        m_master_fd = -1;
    }
    if (!m_symlink_path.empty()) {
        unlink(m_symlink_path.c_str());
        m_symlink_path.clear();
    }
}

bool AtModemSimulator::match_pattern(const std::string& pattern, const std::string& str) {
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

std::string AtModemSimulator::replace_pipe_with_crlf(const std::string& input) {
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

void AtModemSimulator::handle_at_command(const std::string& cmd) {
    if (cmd.empty()) return;

    if (cmd == "ATE0") {
        m_echo_enabled = false;
    } else if (cmd == "ATE1") {
        m_echo_enabled = true;
    }

    std::string response = "";
    bool matched = false;

    for (const auto& rule : m_rules) {
        if (match_pattern(rule.pattern, cmd)) {
            response = rule.response;
            matched = true;
            break;
        }
    }

    if (!matched) {
        response = "ERROR\r\n";
    }

    write_data(m_master_fd, response.c_str(), response.length());

    std::cout << "\n[Входная команда]: " << cmd << std::endl;
    std::cout << "[Отправлен ответ]:\n" << response << "------------------------" << std::endl;
}

void AtModemSimulator::process_char(char ch) {
    if (ch == 0) return;

    if (m_echo_enabled) {
        if (ch == '\r' || ch == '\n') {
            write_data(m_master_fd, "\r\n", 2);
        } else if (ch == '\b' || ch == 127) {
            write_data(m_master_fd, "\b \b", 3);
        } else if (ch >= 32) {
            write_data(m_master_fd, &ch, 1);
        }
    }

    if (ch == '\b' || ch == 127) {
        if (!m_command_accumulator.empty()) {
            m_command_accumulator.pop_back();
        }
        return;
    }

    if (ch == '\r' || ch == '\n') {
        while (!m_command_accumulator.empty() && 
              (m_command_accumulator.back() == '\r' || m_command_accumulator.back() == '\n')) {
            m_command_accumulator.pop_back();
        }

        if (!m_command_accumulator.empty()) {
            handle_at_command(m_command_accumulator);
            m_command_accumulator.clear();
        }
    } else {
        if (ch >= 32) {
            m_command_accumulator += ch;
        }
    }
}