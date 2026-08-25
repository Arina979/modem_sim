#pragma once

#include <string>
#include <vector>
#include <atomic>

class AtModemSimulator {
public:
    struct Rule {
        std::string pattern;
        std::string response;
    };

    AtModemSimulator() = default;
    ~AtModemSimulator();

    AtModemSimulator(const AtModemSimulator&) = delete;
    AtModemSimulator& operator=(const AtModemSimulator&) = delete;

    bool load_rules_from_csv(const std::string& filename);
    bool start(const std::string& symlink_path = "./virtual-tty");
    void run();
    void stop();

private:
    int m_master_fd = -1;
    bool m_echo_enabled = true;
    bool m_running = false;
    std::string m_symlink_path;
    std::string m_command_accumulator;
    std::vector<Rule> m_rules;

    static std::atomic<bool> s_stop_requested;
    static void setup_signal_handler();
    static void handle_signal(int signal);

    static bool match_pattern(const std::string& pattern, const std::string& str);
    static std::string replace_pipe_with_crlf(const std::string& input);

    void handle_at_command(const std::string& cmd);
    void process_char(char ch);
};