#include "AtModemSimulator.hpp"
#include <iostream>

int main() {
    AtModemSimulator modem;

    if (!modem.load_rules_from_csv("at_commands.csv")) {
        std::cerr << "Предупреждение: правила не загружены! Будет возвращаться ERROR." << std::endl;
    }

    if (!modem.start("./virtual-tty")) {
        return 1;
    }

    modem.run();

    return 0;
}