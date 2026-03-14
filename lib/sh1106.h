// ============================================
// ARCHIVO: sh1106.h
// Header de la clase SH1106 - Versión driver
// Usa /dev/sh1106 en vez de /dev/i2c-1 directamente
// ============================================
#ifndef SH1106_H
#define SH1106_H

#include <stdint.h>

// Ya no necesitamos OLED_ADDR ni PADDING:
// el driver conoce la dirección I2C y gestiona el offset
#define WIDTH  128
#define HEIGHT 64
#define PAGES  8

// Incluir los comandos ioctl compartidos con el driver
// El guard __KERNEL__ permite usar el mismo header en kernel y userspace
#include "sh1106_ioctl.h"

// Ruta al char device creado por el driver
#define SH1106_DEV "/dev/sh1106"

// Ruta base a los atributos sysfs
#define SH1106_SYSFS "/sys/class/sh1106_class/sh1106"

// Modos de dibujo
enum DrawMode {
    DRAW_NORMAL = 0,
    DRAW_INVERSE,
    DRAW_XOR
};

// Alineación de texto
enum TextAlign {
    ALIGN_LEFT = 0,
    ALIGN_CENTER,
    ALIGN_RIGHT
};

class SH1106 {
private:
    int fd;                        // File descriptor de /dev/sh1106
    uint8_t buffer[WIDTH * PAGES]; // Framebuffer en memoria (1024 bytes)
    DrawMode drawMode;
    uint8_t contrast;
    bool displayOn;
    bool last_write_ok;            // resultado del último display()

    static const uint8_t font5x7[38][5];

    // Escribe un valor en un atributo sysfs
    // Ej: sysfs_write("contrast", "128")
    int sysfs_write(const char *attr, const char *value);

public:
    SH1106();

    // Inicialización - abre /dev/sh1106
    bool init();

    // Control de pantalla via sysfs
    void setPowerSave(bool enable);
    void setContrast(uint8_t value);
    void invertDisplay(bool invert);
    void flipScreenVertical(bool flip);
    void flipScreenHorizontal(bool flip);

    // Buffer
    void clear();
    void clearBuffer();
    void sendBuffer();
    void display();        // write(fd, buffer, 1024) → driver → I2C → pantalla
    bool display_ok();     // true si el último display() fue exitoso
    void fillScreen(bool on = true);

    // Modos de dibujo
    void setDrawMode(DrawMode mode);
    DrawMode getDrawMode();

    // Píxeles
    void setPixel(int x, int y, bool on = true);
    bool getPixel(int x, int y);

    // Líneas
    void drawLine(int x0, int y0, int x1, int y1);
    void drawHLine(int x, int y, int w);
    void drawVLine(int x, int y, int h);

    // Rectángulos
    void drawRect(int x, int y, int w, int h);
    void drawBox(int x, int y, int w, int h);
    void drawFrame(int x, int y, int w, int h);
    void drawRBox(int x, int y, int w, int h, int r);
    void fillRect(int x, int y, int w, int h);
    void drawFilledBox(int x, int y, int w, int h);
    void fillRBox(int x, int y, int w, int h, int r);

    // Círculos
    void drawCircle(int x0, int y0, int r);
    void drawDisc(int x0, int y0, int r);
    void fillCircle(int x0, int y0, int r);
    void drawCircleHelper(int x0, int y0, int r, uint8_t corners);

    // Triángulos
    void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2);
    void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2);

    // Elipses
    void drawEllipse(int x0, int y0, int rx, int ry);
    void fillEllipse(int x0, int y0, int rx, int ry);

    // Texto
    void drawChar(int x, int y, char c);
    void drawText(int x, int y, const char* text);
    void drawStr(int x, int y, const char* str);
    int getStrWidth(const char* str);
    void drawCenteredText(int y, const char* text);

    // Bitmaps
    void drawBitmap(int x, int y, int w, int h, const uint8_t *bitmap);
    void drawXBM(int x, int y, int w, int h, const uint8_t *bitmap);

    // Formas avanzadas
    void drawStar(int x, int y, int r1, int r2, int points);
    void drawProgressBar(int x, int y, int w, int h, int progress);
    void drawScrollBar(int x, int y, int w, int h, int total, int current, int visible);

    // UI Elements
    void drawCheckBox(int x, int y, int size, bool checked);
    void drawRadioButton(int x, int y, int r, bool selected);
    void drawBattery(int x, int y, int w, int h, int level);
    void drawSignalStrength(int x, int y, int bars, int maxBars);

    // Animaciones
    void scrollLeft(int speed = 1);
    void scrollRight(int speed = 1);
    void fadeOut(int steps = 10);
    void fadeIn(int steps = 10);

    // Cleanup
    void close_display();
    void clear_hw();   // ioctl SH1106_IOC_CLEAR al driver
};

#endif // SH1106_H
