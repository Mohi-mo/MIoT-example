#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "dacap.h"

// Настройка клиента
#define BUFFER_SIZE 1024    // Размер принимаемого пакета
#define PORT 9200           // порт подключения к серверу по умолчанию
#define TIMEOUT_MS 2000     // Время ожидания между передачами 

static Logger logger;       // Структура логера
int success_count = 0;      // Счётчик успешных передач
int failure_count = 0;      // Счётчик провальных передач

/// @brief Структура для описания возможных состояний клиента
typedef enum { 
    IDLE,           // Начальное состояние
    SENDING_RTS,    // Ождиание CTS после отправки RTS
    SENDING_INFO,   // Ожидание DELIVERED после отправки INFO
    RECEIVING       // Приём данных
} ClientState;

/// @brief Информация о текущем отправляемом сообщении 
typedef struct {
    char message[20];   // Текст сообщения
    int dest_address;   // Кому отправляем
    DWORD start_time;   // Временная метка о начале отправки 
} PendingMessage;

static ClientState client_state = IDLE;         // Исходное состояние клиента
static PendingMessage pending = {{0}, 0, 0};    // Текущее сообщение по умолчанию

/// @brief Функция дробления строк по разделителю (запятой)
/// @param sendline - пришедшая строка
/// @param chunks   - буффер для хранения подстрок
/// @return         - количество подстрок
int comma_parser(char* sendline, char* chunks) {
    char *dup = strdup(sendline);   // Дубликат прищедшей строки без изменения исходной
    char *chunk = strtok(dup, ","); // Первичный разбор строки по первой запятой
    int i = 0;
    // Обработка остальных подстрок по остальным запятым в строке
    while (chunk != NULL) {
        if (strcmp(chunk, "") == 0) {
            chunk = strtok(NULL, ",");
            continue;
        }
        strcpy((char *)(chunks + i * 20), chunk);
        i++;
        chunk = strtok(NULL, ",");
    }
    free(dup); // освобождение ресурсов
    return i;
}

/// @brief Отправка сообщения
/// @param socket_fd    - идентификатор сокета
/// @param my_address   - гидроакустический адрес текущего клиента
/// @param dest_address - гидроакустический адрес узла, которому отправляем
/// @param message      - сообщение на отправку
void send_message(int socket_fd, int my_address, int dest_address, const char *message) {
    char log[100];
    snprintf(log, sizeof(log), "DEBUG: send_message: my_address=%d, dest_address=%d, message=%s", my_address, dest_address, message);
    log_details(&logger, log);

    // Проверка, свободен ли клиент сейчас
    if (client_state != IDLE) {
        snprintf(log, sizeof(log), "Client busy, state=%d", client_state);
        log_details(&logger, log);
        return;
    }

    // Подготовка запроса на отправку
    DacapResult result = dacap_send(my_address, dest_address, message, &logger);
    if (result.status == -1) {
        snprintf(log, sizeof(log), "Failed to prepare RTS");
        log_details(&logger, log);
        log_stats(&logger, MSG_RTS, 3, my_address, dest_address, 0);
        failure_count++;
        return;
    }

    // Отправка RTS
    if (send(socket_fd, result.sendline, strlen(result.sendline), 0) < 0) {
        snprintf(log, sizeof(log), "Failed to send RTS: %d", WSAGetLastError());
        log_details(&logger, log);
        log_stats(&logger, MSG_RTS, 3, my_address, dest_address, 0);
        failure_count++;
        return;
    }

    // Логирование данных об отправке
    snprintf(log, sizeof(log), "Sent RTS to %d: %s", dest_address, result.sendline);
    log_details(&logger, log);
    log_stats(&logger, MSG_RTS, 3, my_address, dest_address, 1);
    client_state = SENDING_RTS;
    strncpy(pending.message, message, sizeof(pending.message) - 1);
    pending.message[sizeof(pending.message) - 1] = '\0';
    pending.dest_address = dest_address;
    pending.start_time = GetTickCount();
}

/// @brief Функция чтения данных из сокета
/// @param params - структура параметров подключения
/// @return 
DWORD WINAPI read_from_socket(LPVOID params) {
    // Выгрузка параметров подключения из структуры
    int *args = (int *)params;
    int client_socket = args[0];
    int my_address = args[1];
    char buffer[1024];
    int bytes_received;

    log_details(&logger, "read_from_socket started");

    // Суперцикл для чтения данных из сокета
    while (1) {
        bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0'; // Добавление символа конца строки для каждого нового принятого пакета
            
            char log[150];
            snprintf(log, sizeof(log), "Received: %s", buffer);
            log_details(&logger, log);

            printf("Received: %s\n", buffer);
            
            Packet packet; // Подготовка структуры для дальнейшего разбора пакета
            
            // Разбор пришедшего пакета
            if (dacap_parse_packet(buffer, &packet) == 0) {
                DacapResult result = dacap_handle_packet(&packet, my_address, &logger);
                if (result.status == -1) {
                    log_details(&logger, "Failed to handle packet");
                    continue;
                }
                
                // Автоматическая отправка CTS, если получен RTS
                if (result.status == 0 && result.sendline[0] != '\0') {
                    if (send(client_socket, result.sendline, strlen(result.sendline), 0) < 0) {
                        char log[100];
                        snprintf(log, sizeof(log), "Failed to send %s: %d", 
                                 result.type == MSG_CTS ? "CTS" : "INFO", WSAGetLastError());
                        log_details(&logger, log);
                        log_stats(&logger, result.type, result.type == MSG_CTS ? 3 : strlen(pending.message) + 5, my_address, packet.src, 0);
                    } else {
                        char log[100];
                        snprintf(log, sizeof(log), "Sent %s to %d", 
                                 result.type == MSG_CTS ? "CTS" : "INFO", packet.src);
                        log_details(&logger, log);
                        log_stats(&logger, result.type, result.type == MSG_CTS ? 3 : strlen(pending.message) + 5, my_address, packet.src, 1);
                    }
                }

                // Автоматическая отправка INFO, если получен CTS
                if (result.type == MSG_CTS && client_state == SENDING_RTS && packet.src == pending.dest_address) {
                    char sendline[100];
                    dacap_generate_packet(sendline, pending.dest_address, MSG_INFO, pending.message);
                    if (send(client_socket, sendline, strlen(sendline), 0) < 0) {
                        log_details(&logger, log);
                        log_stats(&logger, MSG_INFO, strlen(pending.message) + 5, my_address, pending.dest_address, 0);
                        client_state = IDLE;
                        failure_count++;
                    } else {
                        log_details(&logger, log);
                        log_stats(&logger, MSG_INFO, strlen(pending.message) + 5, my_address, pending.dest_address, 0);
                        client_state = SENDING_INFO;
                        pending.start_time = GetTickCount();
                    }
                } else if (result.type == MSG_DELIVERED && client_state == SENDING_INFO && packet.dest == pending.dest_address) {
                    snprintf(log, sizeof(log), "DEBUG: DELIVERED dest=%d, pending_dest=%d", packet.dest, pending.dest_address);
                    log_details(&logger, log);
                    log_details(&logger, "Message sent successfully");
                    log_stats(&logger, MSG_DELIVERED, 0, my_address, packet.dest, 1);
                    success_count++;
                    client_state = IDLE;
                    pending.message[0] = '\0';
                    pending.dest_address = 0;
                } else if (result.type == MSG_INFO) {
                    client_state = IDLE;
                    log_stats(&logger, MSG_INFO, strlen(packet.payload), packet.src, my_address, 1);
                }
            }
            memset(buffer, 0, sizeof(buffer));
        } else if (bytes_received == 0) {
            // Сервер закрыл соединение
            printf("Server closed the connection.\n");
            log_details(&logger, "Server closed the connection");
            break;
        } else {
            // Возникла ошибка при чтении
            int error = WSAGetLastError();
            if (error == WSAEWOULDBLOCK) {
                log_details(&logger, "Receive would block, retrying");
                Sleep(100);
                continue;
            }
            printf("Receive error: %d\n", error);
            log_details(&logger, "Receive error");
            break;
        }
    }
    log_details(&logger, "Receive error");
    return 0;
}


/// @brief Функция записи данных в сокет
/// @param params - структура параметров подключения
/// @return 
DWORD WINAPI write_to_client(LPVOID params) {
    // Выгрузка параметров подключения из структуры
    int *args = (int *)params;
    int socket_fd = args[0];
    int my_address = args[1];
    char sendline[100];
    char stats[100];
    int num_dest;
    char input_buffer[100] = {0};
    int input_pos = 0;

    // Буффер сообщений для множественной отправки
    const char *messages[10] = {
        "Message 0", "Message 1", "Message 2", "Message 3", "Message 4",
        "Message 5", "Message 6", "Message 7", "Message 8", "Message 9"
    };

    // Логирование 
    log_details(&logger, "Starting write_to_thread");
    printf("Input format: multiline string, or\nmsi,<address> or exit\n");
    log_details(&logger, "Ready for input");

    // Получение доступа к консоли
    HANDLE stdin_handle = GetStdHandle(STD_INPUT_HANDLE);

    // Супер цикл отправки сообщений в сокет
    while (1) {
        // Проверка на истечение таймера после отправки 
        if (client_state == SENDING_INFO && (GetTickCount() - pending.start_time) > TIMEOUT_MS) {
            char log[100];
            snprintf(log, sizeof(log), "Timeout waiting for %s from %d", 
                     client_state == SENDING_RTS ? "CTS" : "DELIVERED", pending.dest_address);
            log_details(&logger, log);
            log_stats(&logger, client_state == SENDING_RTS ? MSG_CTS : MSG_DELIVERED, 0, my_address, pending.dest_address, 0);
            failure_count++;
            client_state = IDLE;
            pending.message[0] = '\0';
            pending.dest_address = 0;
        }

        // Чтение пользовательского ввода из консоли
        INPUT_RECORD input_record;
        DWORD events_read;
        if (PeekConsoleInput(stdin_handle, &input_record, 1, &events_read) && events_read > 0) {
            if (input_record.EventType == KEY_EVENT && input_record.Event.KeyEvent.bKeyDown) {
                char c = input_record.Event.KeyEvent.uChar.AsciiChar;
                ReadConsoleInput(stdin_handle, &input_record, 1, &events_read);
                // Нажатие ENTER'а
                if (c == '\r') {
                    if (input_pos > 0) {
                        input_buffer[input_pos] = '\0';
                        strncpy(sendline, input_buffer, sizeof(sendline) - 1);
                        sendline[sizeof(sendline) - 1] = '\0';
                        input_buffer[0] = '\0';
                        input_pos = 0;

                        // Выход из приложения, если введён "exit"
                        if (strcmpi(sendline, "exit") == 0) {
                            log_details(&logger, "Received exit command");
                            break;
                        }

                        // Парсинг пользовательской команды
                        char *chunk = strtok(sendline, ",");
                        char *destination = strtok(NULL, ",");
                        if (!destination || !chunk) {
                            printf("Invalid command format. Use: message,<address> or msi,<address>\n");
                            log_details(&logger, "Invalid command format");
                            continue;
                        }
                        num_dest = atoi(destination);
                        if (num_dest == 0) {
                            fprintf(stderr, "Invalid destination address\n");
                            continue;
                        }

                        // Множественная отправка при вводе команды "msi"
                        if (strcmpi(chunk, "msi") == 0) {
                            for (int i = 0; i < 10; i++) {
                                printf("Sending message %d: %s\n", i, messages[i]);
                                send_message(socket_fd, my_address, num_dest, messages[i]);
                                // Ожидание отправки сообщений (цикл от IDLE, RTS/CTS/INFO до обратно IDLE)
                                while (client_state != IDLE && (GetTickCount() - pending.start_time) < TIMEOUT_MS) {
                                    Sleep(10);
                                }
                                if (client_state != IDLE) {
                                    char log[100];
                                    snprintf(log, sizeof(log), "Message %d timed out", i);
                                    log_details(&logger, log);
                                    log_stats(&logger, MSG_DELIVERED, 0, my_address, num_dest, 0);
                                    failure_count++;
                                    client_state = IDLE;
                                    pending.message[0] = '\0';
                                    pending.dest_address = 0;
                                    printf("Message %d timed out\n", i);
                                }
                                Sleep(100); // Пауза между сообщениями
                            }
                            snprintf(stats, sizeof(stats), "Transmission completed: %d successes, %d failures", success_count, failure_count);
                            log_details(&logger, stats);
                        } else {
                            // Отправка одного пользовательского сообщения
                            printf("Sending message: %s to %d\n", chunk, num_dest);
                            send_message(socket_fd, my_address, num_dest, chunk);
                        }
                        printf("Enter command: ");
                        fflush(stdout);
                    }
                } else if (c >= 32 && c <= 126 && input_pos < sizeof(input_buffer) - 1) {
                    // Обработка ввода отдельных символов
                    input_buffer[input_pos++] = c;
                    putchar(c);
                    fflush(stdout);
                } else if (c == 8 && input_pos > 0) {
                    // Нажатие Backspace
                    input_buffer[--input_pos] = '\0';
                    printf("\b \b");
                    fflush(stdout);
                }
            } else {
                ReadConsoleInput(stdin_handle, &input_record, 1, &events_read);
            }
        }
        Sleep(10); // Небольшая пауза
    }

    log_details(&logger, "Exiting write_to_thread");
    return 0;
}


int main(int argc, char *argv[]) {
    // Проверка введённых параметров консоли согласно формату:
    // ./client.exe 127.0.0.n 9200
    if (argc != 3) {
        printf("Usage: %s <IP Address> <Port>\n", argv[0]);
        return 1;
    }

    // Установка параметров клиента из параметров консоли
    char *ip = argv[1];
    int port = atoi(argv[2]);
    int my_address = 1;
    char *last_octet = strrchr(ip, '.');
    if (last_octet) {
        my_address = atoi(last_octet + 1);
    }

    init_logger(&logger, ip); // Инициализация логера

    // Инициализация сети
    WSADATA wsaData;
    SOCKET client_socket;
    struct sockaddr_in server_addr;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("Failed to initialize Winsock.\n");
        log_details(&logger, "Failed to initialize Winsock");
        close_logger(&logger);
        return 1;
    }

    // Создание сокета
    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) {
        printf("Socket creation failed: %d\n", WSAGetLastError());
        log_details(&logger, "Socket creation failed");
        close_logger(&logger);
        WSACleanup();
        return 1;
    }

    printf("Client is active with node address %d!\n", my_address);
    log_details(&logger, "Client started");

    // Настройка адреса сервера
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &server_addr.sin_addr);

    // Подключение к серверу
    if (connect(client_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Connection failed: %d\n", WSAGetLastError());
        log_details(&logger, "Connection failed");
        close_logger(&logger);
        closesocket(client_socket);
        WSACleanup();
        return 1;
    }

    printf("Connected to server %s:%d\n", ip, port);
    log_details(&logger, "Connected to server");

    // Отправка серверу собственного гидроакустического адреса
    // Действует только для локального сервера, 
    // так как для local_host любой клиент имеет адрес 127.0.0.1
    // При подключении к коробочной версии EMU нужно будет исключить этот фрагмент
    // Подключение к коробочной версии осуществляется по IP 10.78.1.n 9200
    char init_buffer[32];
    snprintf(init_buffer, sizeof(init_buffer), "INIT,%d\n", my_address);
    if (send(client_socket, init_buffer, strlen(init_buffer), 0) < 0) {
        printf("Failed to send INIT: %d\n", WSAGetLastError());
        log_details(&logger, "Failed to send INIT");
        closesocket(client_socket);
        close_logger(&logger);
        WSACleanup();
        return 1;
    }

    // Создание двух потоков: для чтения и записи из порта
    int args[2] = {client_socket, my_address};
    HANDLE read_thread = CreateThread(NULL, 0, read_from_socket, args, 0, NULL);
    HANDLE write_thread = CreateThread(NULL, 0, write_to_client, args, 0, NULL);

    // Ожидание завершения потоков
    WaitForSingleObject(read_thread, INFINITE);
    WaitForSingleObject(write_thread, INFINITE);

    // Закрытие соединения с сокетом
    closesocket(client_socket);
    
    
    WSACleanup();
    printf("Disconnected from server\n");
    log_details(&logger, "Disconnected from server");
    
    close_logger(&logger);  // Закрытие файла лога
    
    return 0;
}