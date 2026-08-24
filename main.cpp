#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <cerrno>

// для тестирования
// echo "TEST_OK" | socat - ./virtual-tty,raw,echo=0

int main() {
    const char* port_path = "./arduino-sim"; 

    // Добавляем O_NONBLOCK, чтобы контролировать цикл чтения самостоятельно
    int serial_port = open(port_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serial_port < 0) {
        std::cerr << "Ошибка открытия порта: " << std::strerror(errno) << std::endl;
        return 1;
    }
    std::cout << "Порт успешно открыт!" << std::endl;

    struct termios tty;
    std::memset(&tty, 0, sizeof(tty));
    if (tcgetattr(serial_port, &tty) != 0) {
        std::cerr << "Ошибка tcgetattr: " << std::strerror(errno) << std::endl;
        close(serial_port);
        return 1;
    }

    // Полный сброс в RAW режим, отключаем каноничность и эхо
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL | INLCR | IGNCR);
    tty.c_oflag &= ~(OPOST | ONLCR);

    // Поскольку у нас O_NONBLOCK, эти параметры говорят "возвращать управление сразу"
    tty.c_cc[VMIN] = 0;  
    tty.c_cc[VTIME] = 0; 

    if (tcsetattr(serial_port, TCSANOW, &tty) != 0) {
        std::cerr << "Ошибка tcsetattr: " << std::strerror(errno) << std::endl;
        close(serial_port);
        return 1;
    }
    std::cout << "Ожидание пакетов данных..." << std::endl;

    // Выделяем буфер под полноценный пакет данных
    char buffer[1024]; 

    while (true) {
        std::memset(buffer, 0, sizeof(buffer));
        
        // Читаем всё, что успело накопиться в буфере PTY (до 1023 байт)
        int num_bytes = read(serial_port, buffer, sizeof(buffer) - 1);

        if (num_bytes > 0) {
            // Мгновенно выводим весь принятый пакет данных на экран целиком
            write(STDOUT_FILENO, buffer, num_bytes);
        } else if (num_bytes < 0) {
            // В режиме O_NONBLOCK ошибки EAGAIN/EWOULDBLOCK означают просто "данных в порту пока нет"
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                std::cerr << "\nОшибка чтения: " << std::strerror(errno) << std::endl;
                break;
            }
        }

        // КРИТИЧЕСКИ ВАЖНО: Небольшая пауза в 10 миллисекунд (10000 мкс).
        // Она разгружает процессор и дает время Linux наполнить буфер PTY,
        // если данные отправляются пачкой, предотвращая обрезку строки.
        usleep(10000); 
    }

    close(serial_port);
    return 0;
}