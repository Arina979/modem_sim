#pragma once

#include <string>
#include <vector>
#include <atomic>


class AtModemSimulator {
public:
    struct Rule {
        std::string pattern;   //Шаблон команды (поддерживает '*' и '.')
        std::string response;  //Ответ на команду (с замененными символами '|')
    };

    AtModemSimulator() = default;
    ~AtModemSimulator();

    // Запрет копирования класса для предотвращения повторного закрытия файлового дескриптора
    AtModemSimulator(const AtModemSimulator&) = delete;
    AtModemSimulator& operator=(const AtModemSimulator&) = delete;

    // Загружает правила ответов модема из CSV-файла
    bool load_rules_from_csv(const std::string& filename);

    /**
     * Инициализирует виртуальный псевдотерминал (PTY) и создает символическую ссылку.
     * Путь к создаваемому файлу виртуального COM-порта (по умолчанию "./virtual-tty").
     */
    bool start(const std::string& symlink_path = "./virtual-tty");

    /**
     * Запускает главный цикл обработки входящих данных из PTY.
     * Блокирует выполнение до получения сигнала завершения (SIGINT/SIGTERM) или вызова stop().
     */
    void run();

    // Останавливает работу модема, закрывает PTY дескриптор и удаляет символическую ссылку
    void stop();

private:
    int m_master_fd = -1;                 // Файловый дескриптор Master псевдотерминала
    bool m_echo_enabled = true;           // Флаг режима эхо-вывода (включается/выключается командами ATE1/ATE0)
    bool m_running = false;               // Флаг состояния выполнения главного цикла
    std::string m_symlink_path;           // Cимволическая ссылке на PTY
    std::string m_command_accumulator;    // Накопительный буфер для сборки текущей AT-команды посимвольно
    std::vector<Rule> m_rules;            // Список загруженных правил (шаблон -> ответ)

    static std::atomic<bool> s_stop_requested;  ///< Атомарный флаг запроса остановки от обработчика сигналов
    
    // Настраивает обработчики системных сигналов (SIGINT, SIGTERM) для корректного завершения
    static void setup_signal_handler();

    // Функция-обработчик сигналов Linux
    static void handle_signal(int signal);

    // Проверяет соответствие строки заданному шаблону (Wildcard matching)
    static bool match_pattern(const std::string& pattern, const std::string& str);

    // Заменяет разделители '|' в строке ответа на последовательности CRLF ("\r\n")
    static std::string replace_pipe_with_crlf(const std::string& input);

    // Выполняет поиск правила для накопленной команды и отправляет ответ в PTY
    void handle_at_command(const std::string& cmd);

    // Посимвольная обработка поступающего потока данных из PTY
    void process_char(char ch);
};