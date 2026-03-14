// ============================================
// dashboard.cpp
// Dashboard para SH1106 - Fase 2 systemd
//
// Muestra: fecha/hora, hostname, IP, CPU, RAM,
//          temperatura y uptime en bucle.
//
// Compilar:
//   g++ -o sh1106_dashboard dashboard.cpp sh1106.cpp -lm
//
// Instalar servicio:
//   sudo cp sh1106_dashboard /usr/local/bin/
//   sudo cp sh1106-dashboard.service /etc/systemd/system/
//   sudo systemctl enable sh1106-dashboard
//   sudo systemctl start sh1106-dashboard
// ============================================
#include "sh1106.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/sysinfo.h>
#include <sys/utsname.h>

// Intervalo de refresco en segundos
#define REFRESH_SEC 1

static volatile int running = 1;

void handle_signal(int sig)
{
    (void)sig;
    running = 0;
}

// ----------------------------------------
// Obtener IP local (primera interfaz no lo)
// ----------------------------------------
bool get_local_ip(char *buf, size_t len)
{
    struct ifaddrs *ifaddr, *ifa;

    if (getifaddrs(&ifaddr) < 0) {
        snprintf(buf, len, "NO IP");
        return false;
    }

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr) continue;
        if (ifa->ifa_addr->sa_family != AF_INET) continue;
        if (strcmp(ifa->ifa_name, "lo") == 0) continue;

        struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sa->sin_addr, buf, len);
        freeifaddrs(ifaddr);
        return true;
    }

    freeifaddrs(ifaddr);
    snprintf(buf, len, "NO IP");
    return false;
}

// ----------------------------------------
// Obtener uso de CPU (%)
// Lee /proc/stat dos veces con 200ms de diferencia
// ----------------------------------------
int get_cpu_percent()
{
    static long prev_idle = 0, prev_total = 0;

    FILE *f = fopen("/proc/stat", "r");
    if (!f) return 0;

    long user, nice, system, idle, iowait, irq, softirq;
    fscanf(f, "cpu %ld %ld %ld %ld %ld %ld %ld",
           &user, &nice, &system, &idle, &iowait, &irq, &softirq);
    fclose(f);

    long total = user + nice + system + idle + iowait + irq + softirq;
    long diff_idle  = idle  - prev_idle;
    long diff_total = total - prev_total;

    prev_idle  = idle;
    prev_total = total;

    if (diff_total == 0) return 0;
    return (int)(100 * (diff_total - diff_idle) / diff_total);
}

// ----------------------------------------
// Obtener temperatura CPU (°C)
// ----------------------------------------
int get_cpu_temp()
{
    FILE *f = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (!f) return 0;
    int temp_milli = 0;
    fscanf(f, "%d", &temp_milli);
    fclose(f);
    return temp_milli / 1000;
}

// ----------------------------------------
// Formatear uptime legible
// ----------------------------------------
void format_uptime(char *buf, size_t len)
{
    struct sysinfo si;
    sysinfo(&si);

    long uptime = si.uptime;
    int days    = uptime / 86400;
    int hours   = (uptime % 86400) / 3600;
    int minutes = (uptime % 3600) / 60;

    if (days > 0)
        snprintf(buf, len, "UP %dd %dh %dm", days, hours, minutes);
    else if (hours > 0)
        snprintf(buf, len, "UP %dh %dm", hours, minutes);
    else
        snprintf(buf, len, "UP %dm", minutes);
}

// ----------------------------------------
// Formatear RAM usada/total en MB
// ----------------------------------------
void format_ram(char *buf, size_t len)
{
    struct sysinfo si;
    sysinfo(&si);

    long total_mb = (si.totalram * si.mem_unit) / (1024 * 1024);
    long free_mb  = (si.freeram  * si.mem_unit) / (1024 * 1024);
    long used_mb  = total_mb - free_mb;

    snprintf(buf, len, "RAM %ld/%ldMB", used_mb, total_mb);
}

// ----------------------------------------
// Dibujar separador horizontal
// ----------------------------------------
void draw_separator(SH1106 &oled, int y)
{
    oled.drawHLine(0, y, 128);
}

// ----------------------------------------
// MAIN
// ----------------------------------------
int main()
{
    signal(SIGTERM, handle_signal);
    signal(SIGINT,  handle_signal);

    SH1106 oled;

    if (!oled.init()) {
        fprintf(stderr, "ERROR: No se pudo abrir /dev/sh1106\n");
        return 1;
    }

    // Calentar lectura de CPU (primera lectura es siempre 0)
    get_cpu_percent();
    usleep(200000);

    char ip[32];
    char uptime_str[32];
    char ram_str[32];
    char datetime[32];
    char cpu_str[24];
    char hostname[64];

    // Obtener hostname (no cambia)
    struct utsname uts;
    uname(&uts);
    snprintf(hostname, sizeof(hostname), "%s", uts.nodename);

    int  retry_count = 0;

    while (running) {

        // Fecha y hora
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        strftime(datetime, sizeof(datetime), "%d/%m/%Y  %H:%M:%S", tm_info);

        // Datos del sistema
        get_local_ip(ip, sizeof(ip));
        int cpu  = get_cpu_percent();
        int temp = get_cpu_temp();
        snprintf(cpu_str, sizeof(cpu_str), "CPU %d PCT  T%dC", cpu, temp);
        format_uptime(uptime_str, sizeof(uptime_str));
        format_ram(ram_str, sizeof(ram_str));

        // Dibujar
        oled.clear();
        oled.drawStr(0,  0, datetime);
        draw_separator(oled, 10);
        oled.drawStr(0, 14, hostname);
        oled.drawStr(0, 23, ip);
        draw_separator(oled, 33);
        oled.drawStr(0, 36, cpu_str);
        oled.drawStr(0, 46, ram_str);
        oled.drawStr(0, 55, uptime_str);

        // Volcar a pantalla — si falla, intentar reconectar
        oled.display();
        if (!oled.display_ok()) {
            fprintf(stderr, "Error en display — pantalla desconectada, reintentando en 3s\n");
            oled.close_display();
            sleep(3);
            if (oled.init()) {
                fprintf(stderr, "Pantalla reconectada (intento %d)\n", ++retry_count);
                get_cpu_percent();
                usleep(200000);
            }
            continue;
        }

        retry_count = 0;
        sleep(REFRESH_SEC);
    }

    // Apagar pantalla limpiamente al salir
    oled.clear();
    oled.drawCenteredText(28, "OFFLINE");
    oled.display();
    sleep(2);
    oled.setPowerSave(true);

    oled.close_display();
    return 0;
}
