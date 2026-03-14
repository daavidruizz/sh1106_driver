// ============================================
// ARCHIVO: sh1106.cpp
// Librería userspace para SH1106
// Usa /dev/sh1106 (driver kernel) en vez de /dev/i2c-1
//
// Arquitectura:
//   Dibujo  → framebuffer en memoria (este fichero)
//   display() → write(fd, buffer, 1024) → driver → I2C → pantalla
//   Control → sysfs (/sys/class/sh1106_class/sh1106/)
//   Acciones → ioctl sobre /dev/sh1106
// ============================================
#include "sh1106.h"
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// ============================================
// FUENTE 5x7
// ============================================
const uint8_t SH1106::font5x7[38][5] = {
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // !
};

// ============================================
// CONSTRUCTOR
// ============================================
SH1106::SH1106() : fd(-1), drawMode(DRAW_NORMAL), contrast(0xFF), displayOn(true), last_write_ok(false) {
    memset(buffer, 0, sizeof(buffer));
}

// ============================================
// SYSFS: escritura de atributos
//
// Abre el fichero sysfs, escribe el valor y cierra.
// Equivale a: echo valor > /sys/class/sh1106_class/sh1106/attr
// ============================================
int SH1106::sysfs_write(const char *attr, const char *value)
{
    char path[128];
    snprintf(path, sizeof(path), SH1106_SYSFS "/%s", attr);

    int sfd = open(path, O_WRONLY);
    if (sfd < 0) {
        perror(path);
        return -1;
    }

    int ret = write(sfd, value, strlen(value));
    close(sfd);
    return ret < 0 ? -1 : 0;
}

// ============================================
// INICIALIZACIÓN
//
// Solo abre /dev/sh1106. El driver ya inicializó
// el hardware en probe() cuando se cargó el módulo.
// ============================================
bool SH1106::init()
{
    fd = open(SH1106_DEV, O_RDWR);
    if (fd < 0) {
        perror("Error abriendo " SH1106_DEV);
        return false;
    }

    // Limpiar framebuffer local y pantalla
    clear();
    display();

    printf("✓ SH1106 inicializado via %s\n", SH1106_DEV);
    return true;
}

// ============================================
// CONTROL DE PANTALLA via sysfs
// ============================================

void SH1106::setPowerSave(bool enable)
{
    displayOn = !enable;
    sysfs_write("display_power", enable ? "0" : "1");
}

void SH1106::setContrast(uint8_t value)
{
    contrast = value;
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", value);
    sysfs_write("contrast", buf);
}

void SH1106::invertDisplay(bool invert)
{
    sysfs_write("invert", invert ? "1" : "0");
}

void SH1106::flipScreenVertical(bool flip)
{
    sysfs_write("flip_vertical", flip ? "1" : "0");
}

void SH1106::flipScreenHorizontal(bool flip)
{
    sysfs_write("flip_horizontal", flip ? "1" : "0");
}

// ============================================
// BUFFER
// ============================================

void SH1106::clear()
{
    memset(buffer, 0, WIDTH * PAGES);
}

void SH1106::clearBuffer()
{
    clear();
}

void SH1106::sendBuffer()
{
    display();
}

/*
 * display() - Vuelca el framebuffer a la pantalla
 *
 * Un solo write() de 1024 bytes al driver.
 * El driver recibe los bytes, los manda página a página por I2C.
 * Toda la lógica del SH1106 (offset +2, page addressing) está en el driver.
 */
void SH1106::display()
{
    if (fd < 0) { last_write_ok = false; return; }

    ssize_t ret = write(fd, buffer, WIDTH * PAGES);
    if (ret < 0) {
        perror("Error escribiendo framebuffer");
        last_write_ok = false;
    } else {
        last_write_ok = true;
    }
}

bool SH1106::display_ok()
{
    return last_write_ok;
}

void SH1106::fillScreen(bool on)
{
    memset(buffer, on ? 0xFF : 0x00, WIDTH * PAGES);
}

// ============================================
// MODOS DE DIBUJO
// ============================================

void SH1106::setDrawMode(DrawMode mode) { drawMode = mode; }
DrawMode SH1106::getDrawMode() { return drawMode; }

// ============================================
// PÍXELES
// ============================================

void SH1106::setPixel(int x, int y, bool on)
{
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;

    int page  = y / 8;
    int bit   = y % 8;
    int index = page * WIDTH + x;

    switch (drawMode) {
        case DRAW_NORMAL:
            if (on) buffer[index] |=  (1 << bit);
            else    buffer[index] &= ~(1 << bit);
            break;
        case DRAW_INVERSE:
            if (!on) buffer[index] |=  (1 << bit);
            else     buffer[index] &= ~(1 << bit);
            break;
        case DRAW_XOR:
            buffer[index] ^= (1 << bit);
            break;
    }
}

bool SH1106::getPixel(int x, int y)
{
    if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return false;
    return buffer[(y / 8) * WIDTH + x] & (1 << (y % 8));
}

// ============================================
// LÍNEAS
// ============================================

void SH1106::drawLine(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (true) {
        setPixel(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

void SH1106::drawHLine(int x, int y, int w)
{
    for (int i = 0; i < w; i++) setPixel(x + i, y);
}

void SH1106::drawVLine(int x, int y, int h)
{
    for (int i = 0; i < h; i++) setPixel(x, y + i);
}

// ============================================
// RECTÁNGULOS
// ============================================

void SH1106::drawRect(int x, int y, int w, int h)
{
    drawHLine(x, y, w);
    drawHLine(x, y + h - 1, w);
    drawVLine(x, y, h);
    drawVLine(x + w - 1, y, h);
}

void SH1106::drawBox(int x, int y, int w, int h)   { drawRect(x, y, w, h); }
void SH1106::drawFrame(int x, int y, int w, int h)  { drawRect(x, y, w, h); }

void SH1106::fillRect(int x, int y, int w, int h)
{
    for (int i = 0; i < h; i++) drawHLine(x, y + i, w);
}

void SH1106::drawFilledBox(int x, int y, int w, int h) { fillRect(x, y, w, h); }

void SH1106::drawRBox(int x, int y, int w, int h, int r)
{
    drawHLine(x + r, y, w - 2*r);
    drawHLine(x + r, y + h - 1, w - 2*r);
    drawVLine(x, y + r, h - 2*r);
    drawVLine(x + w - 1, y + r, h - 2*r);
    drawCircleHelper(x + r,         y + r,         r, 1);
    drawCircleHelper(x + w - r - 1, y + r,         r, 2);
    drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4);
    drawCircleHelper(x + r,         y + h - r - 1, r, 8);
}

void SH1106::fillRBox(int x, int y, int w, int h, int r)
{
    fillRect(x + r, y, w - 2*r, h);
    for (int i = 0; i < r; i++) {
        int len = (int)sqrt((double)(r*r - i*i));
        drawHLine(x + r - len, y + i,           len);
        drawHLine(x + w - r,   y + i,           len);
        drawHLine(x + r - len, y + h - 1 - i,   len);
        drawHLine(x + w - r,   y + h - 1 - i,   len);
    }
}

// ============================================
// CÍRCULOS
// ============================================

void SH1106::drawCircle(int x0, int y0, int r)
{
    int x = r, y = 0, err = 0;
    while (x >= y) {
        setPixel(x0 + x, y0 + y); setPixel(x0 + y, y0 + x);
        setPixel(x0 - y, y0 + x); setPixel(x0 - x, y0 + y);
        setPixel(x0 - x, y0 - y); setPixel(x0 - y, y0 - x);
        setPixel(x0 + y, y0 - x); setPixel(x0 + x, y0 - y);
        if (err <= 0) { y++; err += 2*y + 1; }
        if (err >  0) { x--; err -= 2*x + 1; }
    }
}

void SH1106::drawDisc(int x0, int y0, int r)
{
    int x = r, y = 0, err = 0;
    while (x >= y) {
        drawHLine(x0 - x, y0 + y, 2*x + 1);
        drawHLine(x0 - x, y0 - y, 2*x + 1);
        drawHLine(x0 - y, y0 + x, 2*y + 1);
        drawHLine(x0 - y, y0 - x, 2*y + 1);
        if (err <= 0) { y++; err += 2*y + 1; }
        if (err >  0) { x--; err -= 2*x + 1; }
    }
}

void SH1106::fillCircle(int x0, int y0, int r) { drawDisc(x0, y0, r); }

void SH1106::drawCircleHelper(int x0, int y0, int r, uint8_t corners)
{
    int x = r, y = 0, err = 0;
    while (x >= y) {
        if (corners & 1) { setPixel(x0 + x, y0 - y); setPixel(x0 + y, y0 - x); }
        if (corners & 2) { setPixel(x0 + x, y0 + y); setPixel(x0 + y, y0 + x); }
        if (corners & 4) { setPixel(x0 - x, y0 + y); setPixel(x0 - y, y0 + x); }
        if (corners & 8) { setPixel(x0 - x, y0 - y); setPixel(x0 - y, y0 - x); }
        if (err <= 0) { y++; err += 2*y + 1; }
        if (err >  0) { x--; err -= 2*x + 1; }
    }
}

// ============================================
// TRIÁNGULOS
// ============================================

void SH1106::drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2)
{
    drawLine(x0, y0, x1, y1);
    drawLine(x1, y1, x2, y2);
    drawLine(x2, y2, x0, y0);
}

void SH1106::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2)
{
    // Ordenar vértices por Y ascendente
    if (y0 > y1) { int t=y0; y0=y1; y1=t; t=x0; x0=x1; x1=t; }
    if (y1 > y2) { int t=y1; y1=y2; y2=t; t=x1; x1=x2; x2=t; }
    if (y0 > y1) { int t=y0; y0=y1; y1=t; t=x0; x0=x1; x1=t; }

    for (int y = y0; y <= y2; y++) {
        int xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0 + 1);
        int xb = y < y1
            ? x0 + (x1 - x0) * (y - y0) / (y1 - y0 + 1)
            : x1 + (x2 - x1) * (y - y1) / (y2 - y1 + 1);
        if (xa > xb) { int t=xa; xa=xb; xb=t; }
        drawHLine(xa, y, xb - xa + 1);
    }
}

// ============================================
// ELIPSES
// ============================================

void SH1106::drawEllipse(int x0, int y0, int rx, int ry)
{
    int rx2=rx*rx, ry2=ry*ry, fx2=4*rx2, fy2=4*ry2, s;
    int x, y;
    for (x=0, y=ry, s=2*ry2+rx2*(1-2*ry); ry2*x<=rx2*y; x++) {
        setPixel(x0+x,y0+y); setPixel(x0-x,y0+y);
        setPixel(x0-x,y0-y); setPixel(x0+x,y0-y);
        if (s>=0) { s+=fx2*(1-y); y--; }
        s+=ry2*((4*x)+6);
    }
    for (x=rx, y=0, s=2*rx2+ry2*(1-2*rx); rx2*y<=ry2*x; y++) {
        setPixel(x0+x,y0+y); setPixel(x0-x,y0+y);
        setPixel(x0-x,y0-y); setPixel(x0+x,y0-y);
        if (s>=0) { s+=fy2*(1-x); x--; }
        s+=rx2*((4*y)+6);
    }
}

void SH1106::fillEllipse(int x0, int y0, int rx, int ry)
{
    int rx2=rx*rx, ry2=ry*ry, fx2=4*rx2, fy2=4*ry2, s;
    int x, y;
    for (x=0, y=ry, s=2*ry2+rx2*(1-2*ry); ry2*x<=rx2*y; x++) {
        drawHLine(x0-x, y0+y, 2*x+1);
        drawHLine(x0-x, y0-y, 2*x+1);
        if (s>=0) { s+=fx2*(1-y); y--; }
        s+=ry2*((4*x)+6);
    }
    for (x=rx, y=0, s=2*rx2+ry2*(1-2*rx); rx2*y<=ry2*x; y++) {
        drawHLine(x0-x, y0+y, 2*x+1);
        drawHLine(x0-x, y0-y, 2*x+1);
        if (s>=0) { s+=fy2*(1-x); x--; }
        s+=rx2*((4*y)+6);
    }
}

// ============================================
// TEXTO
// ============================================

void SH1106::drawChar(int x, int y, char c)
{
    int index;
    if      (c >= 'A' && c <= 'Z') index = c - 'A';
    else if (c >= 'a' && c <= 'z') index = c - 'a';
    else if (c == ' ')              index = 26;
    else if (c >= '0' && c <= '9') index = 27 + (c - '0');
    else if (c == '!')              index = 37;
    else return;

    for (int col = 0; col < 5; col++) {
        uint8_t line = font5x7[index][col];
        for (int row = 0; row < 8; row++) {
            if (line & (1 << row))
                setPixel(x + col, y + row);
        }
    }
}

void SH1106::drawText(int x, int y, const char* text)
{
    int offset = 0;
    while (*text) {
        drawChar(x + offset, y, *text++);
        offset += 6;
    }
}

void SH1106::drawStr(int x, int y, const char* str) { drawText(x, y, str); }

int SH1106::getStrWidth(const char* str) { return strlen(str) * 6; }

void SH1106::drawCenteredText(int y, const char* text)
{
    drawText((WIDTH - getStrWidth(text)) / 2, y, text);
}

// ============================================
// BITMAPS
// ============================================

void SH1106::drawBitmap(int x, int y, int w, int h, const uint8_t *bitmap)
{
    int pages = h / 8;
    for (int page = 0; page < pages; page++) {
        for (int col = 0; col < w; col++) {
            uint8_t data = bitmap[page * w + col];
            for (int bit = 0; bit < 8; bit++) {
                if (data & (1 << bit))
                    setPixel(x + col, y + page * 8 + bit);
            }
        }
    }
}

void SH1106::drawXBM(int x, int y, int w, int h, const uint8_t *bitmap)
{
    drawBitmap(x, y, w, h, bitmap);
}

// ============================================
// FORMAS AVANZADAS
// ============================================

void SH1106::drawStar(int x, int y, int r1, int r2, int points)
{
    float angle = 0, step = M_PI / points;
    int x0=0, y0=0, x1=0, y1=0;
    for (int i = 0; i < points * 2; i++) {
        int r  = (i % 2 == 0) ? r1 : r2;
        int xp = x + (int)(r * cos(angle));
        int yp = y + (int)(r * sin(angle));
        if (i == 0) { x0=xp; y0=yp; x1=xp; y1=yp; }
        else        { drawLine(x1, y1, xp, yp); x1=xp; y1=yp; }
        angle += step;
    }
    drawLine(x1, y1, x0, y0);
}

void SH1106::drawProgressBar(int x, int y, int w, int h, int progress)
{
    drawRect(x, y, w, h);
    int fill = (progress * (w - 4)) / 100;
    if (fill > 0) fillRect(x + 2, y + 2, fill, h - 4);
}

void SH1106::drawScrollBar(int x, int y, int w, int h,
                            int total, int current, int visible)
{
    drawRect(x, y, w, h);
    int barH = (visible * h) / total;
    int barY = (current * h) / total;
    fillRect(x + 1, y + barY, w - 2, barH);
}

// ============================================
// UI ELEMENTS
// ============================================

void SH1106::drawCheckBox(int x, int y, int size, bool checked)
{
    drawRect(x, y, size, size);
    if (checked) {
        drawLine(x + 2, y + size/2, x + size/2, y + size - 2);
        drawLine(x + size/2, y + size - 2, x + size - 2, y + 2);
    }
}

void SH1106::drawRadioButton(int x, int y, int r, bool selected)
{
    drawCircle(x, y, r);
    if (selected) fillCircle(x, y, r - 2);
}

void SH1106::drawBattery(int x, int y, int w, int h, int level)
{
    drawRect(x, y, w, h);
    fillRect(x + w, y + h/4, 2, h/2);
    int fill = (level * (w - 4)) / 100;
    if (fill > 0) fillRect(x + 2, y + 2, fill, h - 4);
}

void SH1106::drawSignalStrength(int x, int y, int bars, int maxBars)
{
    int barW = 3, barH = 2, gap = 1;
    for (int i = 0; i < maxBars; i++) {
        int h = (i + 1) * barH;
        if (i < bars) fillRect(x + i*(barW+gap), y + (maxBars*barH) - h, barW, h);
        else          drawRect(x + i*(barW+gap), y + (maxBars*barH) - h, barW, h);
    }
}

// ============================================
// ANIMACIONES
// ============================================

void SH1106::scrollLeft(int speed)
{
    for (int s = 0; s < PAGES; s++) {
        for (int i = 0; i < WIDTH - 1; i++)
            buffer[s * WIDTH + i] = buffer[s * WIDTH + i + 1];
        buffer[s * WIDTH + WIDTH - 1] = 0;
    }
    display();
    usleep(speed * 10000);
}

void SH1106::scrollRight(int speed)
{
    for (int s = 0; s < PAGES; s++) {
        for (int i = WIDTH - 1; i > 0; i--)
            buffer[s * WIDTH + i] = buffer[s * WIDTH + i - 1];
        buffer[s * WIDTH] = 0;
    }
    display();
    usleep(speed * 10000);
}

void SH1106::fadeOut(int steps)
{
    for (int i = 255; i >= 0; i -= 255/steps) {
        setContrast((uint8_t)i);
        usleep(50000);
    }
    setContrast(0);
}

void SH1106::fadeIn(int steps)
{
    for (int i = 0; i <= 255; i += 255/steps) {
        setContrast((uint8_t)i);
        usleep(50000);
    }
    setContrast(255);
}

// ============================================
// CLEANUP
// ============================================

void SH1106::close_display()
{
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

// ============================================
// IOCTL DIRECTO
// clear_hw() limpia el framebuffer interno del driver via ioctl
// ============================================
void SH1106::clear_hw()
{
    if (fd < 0) return;
    if (ioctl(fd, SH1106_IOC_CLEAR) < 0)
        perror("ioctl SH1106_IOC_CLEAR");
    // sincronizar buffer local también
    memset(buffer, 0, WIDTH * PAGES);
}
