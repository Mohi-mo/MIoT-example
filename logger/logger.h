#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

typedef struct {
    FILE *details_file; // Текстовый лог
    FILE *stats_file;  // Статистический лог
    char ip[16];       // IP клиента
} Logger;

/// @brief Функция инициализации объекта логера
/// @param logger   - структура логгера
/// @param ip       - IP адрес текущего узла, на котором ведётся лог
void init_logger(Logger *logger, const char *ip);

/// @brief Функция логирования текстовых полей
/// @param logger   - структура логера
/// @param message  - строка для логирования
void log_details(Logger *logger, const char *message);

/// @brief Функция для хранения статистических данных передачи
/// @param logger   - структура логера
/// @param type     - тип сообщения
/// @param size     - размер пакета
/// @param src      - отправитель сообщения
/// @param dest     - получатель сообщения
/// @param success  - результат
void log_stats(Logger *logger, int type, int size, int src, int dest, int success);

/// @brief Функция для закрытия логера
/// @param logger - структура логгера
void close_logger(Logger *logger);

#endif