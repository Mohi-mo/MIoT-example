#include <stdio.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "logger.h"

void init_logger(Logger *logger, const char *ip) {
    char details_filename[32], stats_filename[32];
    snprintf(details_filename, sizeof(details_filename), "details_%s.txt", ip);
    snprintf(stats_filename, sizeof(stats_filename), "stats_%s.csv", ip);
    strncpy(logger->ip, ip, sizeof(logger->ip) - 1);
    logger->ip[sizeof(logger->ip) - 1] = '\0';

    logger->details_file = fopen(details_filename, "a");
    if (!logger->details_file) {
        perror("Failed to open details log file");
    }

    logger->stats_file = fopen(stats_filename, "a");
    if (!logger->stats_file) {
        perror("Failed to open stats log file");
    } else {
        fseek(logger->stats_file, 0, SEEK_END);
        if (ftell(logger->stats_file) == 0) {
            fprintf(logger->stats_file, "Timestamp,MessageType,Size,Source,Destination,Success\n");
            fflush(logger->stats_file);
        }
    }
}

void log_details(Logger *logger, const char *message) {
    if (!logger->details_file) return;
    SYSTEMTIME st;
    GetSystemTime(&st);
    fprintf(logger->details_file, "[%04d-%02d-%02d %02d:%02d:%02d.%03d] %s\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, message);
    fflush(logger->details_file);
}

void log_stats(Logger *logger, int type, int size, int src, int dest, int success) {
    if (!logger->stats_file) return;
    SYSTEMTIME st;
    GetSystemTime(&st);
    fprintf(logger->stats_file, "%04d-%02d-%02d %02d:%02d:%02d.%03d,%s,%d,%d,%d,%d\n",
            st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
            type == 0 ? "RTS" : type == 1 ? "CTS" : type == 2 ? "INFO" : "DELIVERED",
            size, src, dest, success);
    fflush(logger->stats_file);
}

void close_logger(Logger *logger) {
    if (logger->details_file) {
        fclose(logger->details_file);
        logger->details_file = NULL;
    }
    if (logger->stats_file) {
        fclose(logger->stats_file);
        logger->stats_file = NULL;
    }
}