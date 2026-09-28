#include "AtModemSimulator.hpp"
#include <iostream>
#include <sys/wait.h>
#include <csignal>
#include <atomic>

// Атомарный флаг для остановки из обработчика сигналов
static std::atomic<bool> g_stop_requested{false};

// Обработчик сигналов (SIGINT, SIGTERM)
void handle_signal(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        g_stop_requested = true;
    }
}

// Настройка сигналов
void setup_signal_handler() {
    struct sigaction sa{};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

// Запуска утилиты `screen` в новом окне терминала
void open_screen_in_new_terminal(const std::string& tty_path) {
    pid_t pid = fork();
    if (pid == 0) {
        execlp("gnome-terminal", "gnome-terminal", "--", "screen", tty_path.c_str(), (char*)NULL);

        // В случае других Linux-терминалов можно переключить на один из вариантов:
        // execlp("x-terminal-emulator", "x-terminal-emulator", "-e", ("screen " + tty_path).c_str(), (char*)NULL);
        // execlp("konsole", "konsole", "-e", "screen", tty_path.c_str(), (char*)NULL);
        // execlp("xfce4-terminal", "xfce4-terminal", "-e", ("screen " + tty_path).c_str(), (char*)NULL);

        // Выполняется, только если не удалось запустить указанный эмулятор терминала
        std::cerr << "Не удалось автоматически открыть терминал для screen." << std::endl;
        std::exit(1);
    }
}

int main() {
    setup_signal_handler();

    AtModemSimulator modem;
    std::string symlink_path = "./virtual-tty";

    // 1. Загрузка правил соответствия AT-команд и ответов из CSV-файла
    if (!modem.load_rules_from_csv("at_commands.csv")) {
        std::cerr << "Предупреждение: правила не загружены! На все команды будет возвращаться ERROR." << std::endl;
    }

    // 2. Инициализация и запуск PTY псевдотерминала
    if (!modem.start(symlink_path)) {
        return 1;
    }

    // 3. Автоматический запуск клиенского терминала screen
    open_screen_in_new_terminal(symlink_path);

    // 4. Запуск основного регистратора и обработчика событий модема
    modem.run(g_stop_requested);

    return 0;
}