#include <stdio.h>
#include <string.h>
#include "dacap.h"


void dacap_generate_packet(char *sendline, int dest_address, MessageType type, const char *data) {
    const char *payload;    // Текст сообщения
    int len;                // Длина сообщения
    const char *ack;        // Флаг подтверждения доставки
    
    // Выбор типа генерируемого пакета
    switch (type) {
        // Формироваение запроса на отправку
        case MSG_RTS:
            payload = "RTS";
            len = strlen(payload);
            ack = "noack";
            break;
        // Формирование разрешения на отправку
        case MSG_CTS:
            payload = "CTS";
            len = strlen(payload);
            ack = "noack";
            break;
        // Формирование информационного пакета
        case MSG_INFO:
            char info_payload[30]; // Буфер для INFO;<data>
            snprintf(info_payload, sizeof(info_payload), "INFO;%s", data);
            payload = info_payload;
            len = strlen(payload);
            ack = "ack";
            break;
        // Возврат пустой строки, если тип сообщения неизвестен
        default:
            sendline[0] = '\0';
            return;
    }
    // Итоговая сборка строки
    sprintf(sendline, "AT*SENDIM,%i,%i,%s,%s\n", len, dest_address, ack, payload);
}

int dacap_parse_packet(char *buffer, Packet *packet) {
    char chunks[1024][20];          // Выделение буффера для хранения фрагментов строки 
    int i = 0;                      // Итератор подстрок в строке
    char *dup = strdup(buffer);     // Копия строки, чтобы не изменять исходную 
    char *chunk = strtok(dup, ","); // Выделение первой подстроки по разделителю
    
    while (chunk && i < 1024) {
        strncpy(chunks[i], chunk, 19);
        chunks[i][19] = '\0';
        i++;
        chunk = strtok(NULL, ",");
    }
    
    free(dup);  // Освобождение ресурсов после выполнения разбиения на подстроки

    // Инциализация пакета по умолчанию
    packet->src = 0;
    packet->dest = 0;
    packet->payload[0] = '\0';

    // Анализ пакета по его содержимому и запись параметров в структуру
    if (strcmp(chunks[0], "RECVIM") == 0 && i >= 10) {
        packet->src = atoi(chunks[2]);      
        packet->dest = atoi(chunks[3]);
        if (strncmp(chunks[9], "RTS", 3) == 0) {
            packet->type = MSG_RTS;
            strcpy(packet->payload, "RTS");
        } else if (strncmp(chunks[9], "CTS", 3) == 0) {
            packet->type = MSG_CTS;
            strcpy(packet->payload, "CTS");
        } else if (strncmp(chunks[9], "INFO;", 5) == 0) {
            packet->type = MSG_INFO;
            strncpy(packet->payload, chunks[9] + 5, sizeof(packet->payload) - 1);
            packet->payload[sizeof(packet->payload) - 1] = '\0';
            char *end = packet->payload + strlen(packet->payload) - 1;
            while (end >= packet->payload && (*end == '\r' || *end == '\n')) {
                *end = '\0';
                end--;
            }
        } else {
            return -1;
        }
    } else if (strncmp(chunks[0], "DELIVERED", 9) == 0 && i >= 2) {
        packet->type = MSG_DELIVERED;
        packet->dest = atoi(chunks[1]);
        packet->src = packet->dest;
        packet->payload[0] = '\0';
    } else {
        return -1; // Неизвестный формат сообщения
    }
    return 0; // Успех
}

DacapResult dacap_send(int my_address, int dest_address, const char *message, Logger *logger) {
    DacapResult result = {0, MSG_RTS, {0}}; // Инициализация структуры результата
    char log_buffer[100];

    // Проверяем, что адрес получателя валидный
    if (dest_address <= 0) {
        snprintf(log_buffer, sizeof(log_buffer), "Invalid dest address %d", dest_address);
        log_details(logger, log_buffer);
        result.status = -1;
        return result;
    }

    // Формирование пакета RTS как стартового в алгоритме
    dacap_generate_packet(result.sendline, dest_address, MSG_RTS, message);
    snprintf(log_buffer, sizeof(log_buffer), "Prepared RTS to %d", dest_address);
    log_details(logger, log_buffer);
    result.type = MSG_RTS;
    return result;
}

DacapResult dacap_handle_packet(Packet *packet, int my_address, Logger *logger) {
    DacapResult result = {1, MSG_RTS, {0}}; // Инициализация структуры по умолчанию
    char log_buffer[100];

    // Логирование информации о пакете
    snprintf(log_buffer, sizeof(log_buffer), "Handling packet type=%d, src=%d, dest=%d", packet->type, packet->src, packet->dest);
    log_details(logger, log_buffer);

    // Игнорирование пакетов, не предназначенных данному узлу
    if (packet->type != MSG_DELIVERED && packet->dest != my_address) {
        snprintf(log_buffer, sizeof(log_buffer), "Packet for %d, not %d, ignoring", packet->dest, my_address);
        log_details(logger, log_buffer);
        result.status = 1;
        return result;
    }

    // Обработка типа входящего пакета
    switch (packet->type) {
        case MSG_RTS:
            snprintf(log_buffer, sizeof(log_buffer), "RTS from %d", packet->src);
            log_details(logger, log_buffer);
            dacap_generate_packet(result.sendline, packet->src, MSG_CTS, NULL);
            snprintf(log_buffer, sizeof(log_buffer), "Prepared CTS for %d", packet->src);
            log_details(logger, log_buffer);
            result.status = 0;
            result.type = MSG_CTS;
            break;

        case MSG_CTS:
            snprintf(log_buffer, sizeof(log_buffer), "CTS from %d", packet->src);
            log_details(logger, log_buffer);
            result.status = 0;
            result.type = MSG_CTS;
            break;

        case MSG_INFO:
            snprintf(log_buffer, sizeof(log_buffer), "INFO from %d: %s", packet->src, packet->payload);
            log_details(logger, log_buffer);
            printf("Message from %d: %s\n", packet->src, packet->payload);
            result.status = 0;
            result.type = MSG_INFO;
            break;

        case MSG_DELIVERED:
            snprintf(log_buffer, sizeof(log_buffer), "DELIVERED for dest %d", packet->dest);
            log_details(logger, log_buffer);
            result.status = 0;
            result.type = MSG_DELIVERED;
            break;

        default:
            snprintf(log_buffer, sizeof(log_buffer), "Unknown packet type %d", packet->type);
            log_details(logger, log_buffer);
            result.status = -1;
            break;
    }
    return result;
}