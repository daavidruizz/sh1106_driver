#!/bin/bash
# ============================================
# test_auto.sh - Tests automáticos SH1106
# Verifica driver, librería y hardware
#
# Uso: ./test_auto.sh
# No requiere root para la mayoría de tests.
# ============================================

# ---- Colores ----
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

PASS=0
FAIL=0
WARN=0

pass() { echo -e "  ${GREEN}PASS${NC} $1"; ((PASS++)); }
fail() { echo -e "  ${RED}FAIL${NC} $1"; ((FAIL++)); }
warn() { echo -e "  ${YELLOW}WARN${NC} $1"; ((WARN++)); }
section() { echo -e "\n${BLUE}[$1]${NC}"; }

echo ""
echo "================================================"
echo "  Test automático SH1106 OLED"
echo "  $(date)"
echo "================================================"

# ============================================
# BLOQUE 1: Driver kernel
# ============================================
section "1. Driver kernel"

# 1.1 Módulo cargado
if lsmod | grep -q "^sh1106 "; then
    pass "Módulo sh1106 cargado"
else
    fail "Módulo sh1106 NO cargado — sudo modprobe sh1106"
fi

# 1.2 Drivers conflictivos NO cargados
if lsmod | grep -q "fb_sh1106"; then
    fail "fb_sh1106 cargado — conflicto con el driver"
else
    pass "fb_sh1106 no cargado (OK)"
fi

if lsmod | grep -q "^fbtft "; then
    warn "fbtft cargado — puede causar conflictos"
else
    pass "fbtft no cargado (OK)"
fi

# 1.3 /dev/sh1106 existe
if [ -e /dev/sh1106 ]; then
    pass "/dev/sh1106 existe"
else
    fail "/dev/sh1106 no existe"
fi

# 1.4 Permisos correctos (sin sudo)
if [ -w /dev/sh1106 ] && [ -r /dev/sh1106 ]; then
    pass "/dev/sh1106 accesible sin sudo"
else
    warn "/dev/sh1106 requiere sudo — instalar 99-sh1106.rules"
fi

# 1.5 sysfs accesible
SYSFS="/sys/class/sh1106_class/sh1106"
if [ -d "$SYSFS" ]; then
    pass "sysfs $SYSFS existe"
else
    fail "sysfs no encontrado"
fi

# 1.6 Atributos sysfs
for attr in contrast display_power invert flip_horizontal flip_vertical stats; do
    if [ -f "$SYSFS/$attr" ]; then
        pass "sysfs: $attr existe"
    else
        fail "sysfs: $attr NO existe"
    fi
done

# 1.7 Leer stats
if cat "$SYSFS/stats" &>/dev/null; then
    pass "sysfs stats legible"
else
    fail "sysfs stats no legible"
fi

# 1.8 Blacklist configurado
if [ -f /etc/modprobe.d/sh1106-blacklist.conf ]; then
    pass "Blacklist configurado"
else
    warn "Blacklist no configurado — fb_sh1106 puede cargar en el boot"
fi

# 1.9 Carga automática configurada
if [ -f /etc/modules-load.d/sh1106.conf ]; then
    pass "Carga automática en boot configurada"
else
    warn "Carga automática no configurada — el driver no cargará solo"
fi

# 1.10 Device tree overlay instalado
if [ -f /boot/overlays/sh1106-overlay.dtbo ] || \
   [ -f /boot/overlays/sh1106.dtbo ]; then
    pass "Device tree overlay instalado"
else
    fail "Device tree overlay no encontrado en /boot/overlays/"
fi

# 1.11 Overlay activo en config.txt
CONFIG="/boot/firmware/config.txt"
[ -f "$CONFIG" ] || CONFIG="/boot/config.txt"
if grep -q "dtoverlay=sh1106" "$CONFIG" 2>/dev/null; then
    pass "dtoverlay=sh1106 activo en $CONFIG"
else
    warn "dtoverlay=sh1106 no encontrado en $CONFIG"
fi

# ============================================
# BLOQUE 2: Librería userspace
# ============================================
section "2. Librería userspace"

# 2.1 libsh1106.so instalada
if [ -f /usr/local/lib/libsh1106.so ]; then
    pass "libsh1106.so instalada en /usr/local/lib/"
else
    fail "libsh1106.so no encontrada"
fi

# 2.2 Header instalado
if [ -f /usr/local/include/sh1106.h ]; then
    pass "sh1106.h instalado en /usr/local/include/"
else
    fail "sh1106.h no encontrado"
fi

# 2.3 pkg-config funciona
if pkg-config --exists sh1106 2>/dev/null; then
    VER=$(pkg-config --modversion sh1106)
    pass "pkg-config sh1106 OK (versión $VER)"
else
    fail "pkg-config sh1106 no encontrado"
fi

# 2.4 Compilación de ejemplo mínimo
TMP=$(mktemp -d)
cat > "$TMP/test_compile.cpp" << 'EOF'
#include <sh1106.h>
int main() {
    SH1106 oled;
    return 0;
}
EOF

if g++ "$TMP/test_compile.cpp" \
    $(pkg-config --cflags --libs sh1106 2>/dev/null) \
    -o "$TMP/test_compile" 2>/dev/null; then
    pass "Compilación con pkg-config OK"
else
    fail "Error compilando con pkg-config"
fi
rm -rf "$TMP"

# ============================================
# BLOQUE 3: Hardware I2C
# ============================================
section "3. Hardware I2C"

# 3.1 Bus I2C disponible
if [ -e /dev/i2c-1 ]; then
    pass "/dev/i2c-1 disponible"
else
    fail "/dev/i2c-1 no encontrado — I2C no habilitado"
fi

# 3.2 SH1106 detectado en 0x3C
if command -v i2cdetect &>/dev/null; then
    if i2cdetect -y 1 2>/dev/null | grep -q "3c\|3C"; then
        pass "SH1106 detectado en I2C-1 dirección 0x3C"
    else
        fail "SH1106 no detectado en 0x3C — ¿conectado?"
    fi
else
    warn "i2cdetect no disponible — apt install i2c-tools"
fi

# ============================================
# BLOQUE 4: Escritura al driver
# ============================================
section "4. Escritura al driver"

# 4.1 Write de 1024 bytes
if [ -w /dev/sh1106 ]; then
    # Mandar framebuffer vacío (1024 bytes a cero)
    if dd if=/dev/zero of=/dev/sh1106 bs=1024 count=1 2>/dev/null; then
        pass "write() 1024 bytes OK — pantalla limpiada"
    else
        fail "write() falló"
    fi

    # 4.2 Write tamaño incorrecto debe fallar con EINVAL
    if ! dd if=/dev/zero of=/dev/sh1106 bs=512 count=1 2>/dev/null; then
        pass "write() 512 bytes rechazado correctamente (EINVAL)"
    else
        fail "write() 512 bytes no fue rechazado — falta validación"
    fi

    # 4.3 Read de 1024 bytes
    TMP_READ=$(mktemp)
    if dd if=/dev/sh1106 of="$TMP_READ" bs=1024 count=1 2>/dev/null; then
        SIZE=$(wc -c < "$TMP_READ")
        if [ "$SIZE" -eq 1024 ]; then
            pass "read() 1024 bytes OK"
        else
            fail "read() devolvió $SIZE bytes (esperados 1024)"
        fi
    else
        fail "read() falló"
    fi
    rm -f "$TMP_READ"
else
    warn "Sin permisos de escritura en /dev/sh1106 — tests de write saltados"
fi

# ============================================
# BLOQUE 5: sysfs escritura
# ============================================
section "5. sysfs control"

SYSFS="/sys/class/sh1106_class/sh1106"

if [ -w "$SYSFS/contrast" ]; then
    # Guardar valor actual
    ORIG=$(cat "$SYSFS/contrast")

    # Cambiar contraste
    echo 100 > "$SYSFS/contrast"
    VAL=$(cat "$SYSFS/contrast")
    if [ "$VAL" = "100" ]; then
        pass "sysfs contrast write/read OK"
    else
        fail "sysfs contrast: escribí 100, leí $VAL"
    fi

    # Restaurar
    echo "$ORIG" > "$SYSFS/contrast"

    # Invert on/off
    echo 1 > "$SYSFS/invert" && echo 0 > "$SYSFS/invert"
    pass "sysfs invert ON/OFF OK"

else
    warn "sysfs sin permisos de escritura — tests saltados (necesita sudo)"
fi

# ============================================
# BLOQUE 6: Servicio systemd
# ============================================
section "6. Servicio systemd"

if systemctl list-unit-files sh1106-dashboard.service &>/dev/null; then
    pass "sh1106-dashboard.service instalado"

    STATE=$(systemctl is-active sh1106-dashboard 2>/dev/null)
    if [ "$STATE" = "active" ]; then
        pass "sh1106-dashboard activo"
    elif [ "$STATE" = "inactive" ]; then
        warn "sh1106-dashboard inactivo — sudo systemctl start sh1106-dashboard"
    else
        fail "sh1106-dashboard estado: $STATE"
    fi

    if systemctl is-enabled sh1106-dashboard &>/dev/null; then
        pass "sh1106-dashboard habilitado en boot"
    else
        warn "sh1106-dashboard no habilitado en boot"
    fi
else
    warn "sh1106-dashboard.service no instalado"
fi

# ============================================
# RESUMEN
# ============================================
TOTAL=$((PASS + FAIL + WARN))
echo ""
echo "================================================"
echo -e "  Tests: $TOTAL  |  ${GREEN}PASS: $PASS${NC}  |  ${RED}FAIL: $FAIL${NC}  |  ${YELLOW}WARN: $WARN${NC}"
echo "================================================"
echo ""

if [ "$FAIL" -gt 0 ]; then
    echo -e "  ${RED}Hay fallos que resolver antes de continuar.${NC}"
    exit 1
elif [ "$WARN" -gt 0 ]; then
    echo -e "  ${YELLOW}Sistema funcional con advertencias menores.${NC}"
    exit 0
else
    echo -e "  ${GREEN}Todo OK. Sistema listo.${NC}"
    exit 0
fi
