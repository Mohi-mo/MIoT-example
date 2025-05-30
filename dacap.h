#ifndef DACAP_H
#define DACAP_H

#include <stdlib.h>
#include "logger/logger.h"


/// Виды сообщений в рамках протокола 
typedef enum { 
    MSG_RTS,        // Запрос на отправку
    MSG_CTS,        // Разрешение на отправку
    MSG_INFO,       // Передача данных
    MSG_DELIVERED   // Подтверждение доставки
} MessageType;

/// Структура для хранения информации о сообщении
typedef struct {
    int src;            // Кто отпавил сообщение
    int dest;           // Кому доставить сообщение
    MessageType type;   // Тип сообщения (RTS, CTS и т.д.)
    char payload[64];   // Текст сообщения
} Packet;

/// @brief  Структура для результата обработки сообщения
typedef struct {
    int status;         // Результат: 0 - успех, -1 - ошибка, 1 - пропуск сообщения
    MessageType type;   // Тип сформированного пакета (RTS, CTS, INFO)
    char sendline[100]; // Строка для отправки
} DacapResult;

/// @brief Функция для генерации пакета
/// @param sendline         - строка для отправки
/// @param dest_address     - адресат (кому отправляем)
/// @param type             - тип передаваемого сообщения
/// @param data             - полезные данные
void dacap_generate_packet(char *sendline, int dest_address, MessageType type, const char *data);

/// @brief Функция анализа входящих пакетов
/// @param buffer   - пришедшая строка
/// @param packet   - структура для разбора пакета
/// @return         - код результата
int dacap_parse_packet(char *buffer, Packet *packet);

/// @brief Функция для подготовки сообщения для отправки
/// @param my_address   - адрес узла-отправителя
/// @param dest_address - адрес узла-принимающего
/// @param message      - передаваемое сообщение
/// @param logger       - объект логгера
/// @return             - структура с результатом отправки
DacapResult dacap_send(int my_address, int dest_address, const char *message, Logger *logger);

/// @brief Функция для обработки входящих сообщений и принятия решений о дальнейших действиях протоколов
/// @param packet       - структура для хранения данных о пакете
/// @param my_address   - гидроакустический адрес текущего узла
/// @param logger       - объект логгера
/// @return             - структура с результатом отправки
DacapResult dacap_handle_packet(Packet *packet, int my_address, Logger *logger);

#endif