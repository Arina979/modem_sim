#include "AtModemSimulator.hpp"
#include <iostream>
#include <sys/wait.h>

//Вспомогательная функция для запуска screen в новом окне эмулятора терминала
void open_screen_in_new_terminal(const std::string& tty_path) {
    pid_t pid = fork();
    if (pid == 0) {
        // Дочерний процесс
        std::string screen_cmd = "screen " + tty_path;

        // Перебираем популярные терминалы Linux
        //execlp("x-terminal-emulator", "x-terminal-emulator", "-e", screen_cmd.c_str(), (char*)NULL);
        execlp("gnome-terminal", "gnome-terminal", "--", "screen", tty_path.c_str(), (char*)NULL);
        //("konsole", "konsole", "-e", "screen", tty_path.c_str(), (char*)NULL);
        //execlp("xfce4-terminal", "xfce4-terminal", "-e", screen_cmd.c_str(), (char*)NULL);
        //execlp("xterm", "xterm", "-e", screen_cmd.c_str(), (char*)NULL);

        // Если ни один графический терминал не сработал
        std::cerr << "Не удалось автоматически открыть терминал для screen." << std::endl;
        std::exit(1);
    }
}

int main() {

    AtModemSimulator modem;
    std::string symlink_path = "./virtual-tty";

    if (!modem.load_rules_from_csv("at_commands.csv")) {
        std::cerr << "Предупреждение: правила не загружены! Будет возвращаться ERROR." << std::endl;
    }

    if (!modem.start(symlink_path)) {
        return 1;
    }

    open_screen_in_new_terminal(symlink_path);

    modem.run();

    return 0;
}