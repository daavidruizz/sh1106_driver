#!/bin/bash
# ============================================
# setup.sh - Instalación completa SH1106
# Soporta: Raspberry Pi 3/4, Pi 5, USB-I2C
#
# Uso: sudo ./setup.sh
# ============================================
set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

ok()   { echo -e "${GREEN}✓${NC} $1"; }
warn() { echo -e "${YELLOW}⚠${NC}  $1"; }
err()  { echo -e "${RED}✗${NC} $1"; exit 1; }
info() { echo -e "  $1"; }

if [ "$EUID" -ne 0 ]; then
    err "Ejecutar como root: sudo ./setup.sh"
fi

BASE_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_VER="$(uname -r)"
MODEL=$(cat /proc/device-tree/model 2>/dev/null || echo "unknown")

echo ""
echo "================================================"
echo "  Instalación SH1106 OLED Driver & Library"
echo "  Kernel:  ${KERNEL_VER}"
echo "  Modelo:  ${MODEL}"
echo "  Dir:     ${BASE_DIR}"
echo "================================================"
echo ""

# ============================================
# PASO 1: Dependencias
# ============================================
echo "[1/7] Verificando dependencias..."

MISSING=""
for pkg in build-essential device-tree-compiler i2c-tools; do
    if ! dpkg -l "$pkg" &>/dev/null; then
        MISSING="$MISSING $pkg"
    fi
done

if [ ! -d "/lib/modules/${KERNEL_VER}/build" ]; then
    MISSING="$MISSING raspberrypi-kernel-headers"
fi

if [ -n "$MISSING" ]; then
    warn "Instalando dependencias:$MISSING"
    apt-get install -y $MISSING
fi
ok "Dependencias OK"

# ============================================
# PASO 2: Driver kernel
# ============================================
echo ""
echo "[2/7] Instalando driver kernel..."

DRIVER_DIR="${BASE_DIR}/driver"
[ -d "$DRIVER_DIR" ] || err "Directorio driver/ no encontrado"

cd "$DRIVER_DIR"
make clean && make
ok "sh1106.ko compilado"

mkdir -p "/lib/modules/${KERNEL_VER}/extra"
cp sh1106.ko "/lib/modules/${KERNEL_VER}/extra/"
depmod -a
ok "Módulo instalado"

cp 99-sh1106.rules /etc/udev/rules.d/
udevadm control --reload-rules
ok "Regla udev instalada"

# Blacklist drivers que compiten
cat > /etc/modprobe.d/sh1106-blacklist.conf << 'EOF'
blacklist fb_sh1106
blacklist fbtft
EOF
ok "Blacklist configurado"

# Carga automática en boot
echo "sh1106" > /etc/modules-load.d/sh1106.conf
ok "Carga automática en boot configurada"

# ============================================
# PASO 3: Detectar hardware y configurar boot
# ============================================
echo ""
echo "[3/7] Detectando hardware I2C..."

# Detectar adaptador USB-I2C
USB_BUS=$(i2cdetect -l 2>/dev/null | grep -i "usb\|tiny\|cp210\|ch341\|ft232" \
          | head -1 | awk '{print $1}' | sed 's/i2c-//')

if [ -n "$USB_BUS" ]; then
    # ---- Modo USB-I2C ----
    info "Adaptador USB-I2C detectado en i2c-${USB_BUS}"
    info "No se instala device tree overlay (no necesario)"

    # Asegurar que el módulo i2c-ch341 carga automáticamente
    if ! grep -q "i2c-ch341" /etc/modules-load.d/sh1106.conf 2>/dev/null; then
        echo "i2c-dev"   >> /etc/modules-load.d/sh1106.conf
        echo "i2c-ch341" >> /etc/modules-load.d/sh1106.conf 2>/dev/null || true
    fi
    modprobe i2c-dev    2>/dev/null || true
    modprobe i2c-ch341  2>/dev/null || warn "i2c-ch341 no disponible — instalar: apt install i2c-ch341-dkms"
    ok "Módulos I2C USB configurados"

    # Instalar script de probe
    cp "${BASE_DIR}/driver/sh1106-probe.sh" /usr/local/bin/
    chmod +x /usr/local/bin/sh1106-probe.sh
    ok "sh1106-probe.sh instalado"

    # Instalar servicio de probe
    cp "${BASE_DIR}/driver/sh1106-probe.service" /etc/systemd/system/
    systemctl daemon-reload
    systemctl enable sh1106-probe
    ok "sh1106-probe.service habilitado"

    USE_USB=true

else
    # ---- Modo GPIO (device tree) ----
    USE_USB=false

    CONFIG="/boot/firmware/config.txt"
    [ -f "$CONFIG" ] || CONFIG="/boot/config.txt"

    # Seleccionar overlay según modelo
    if echo "$MODEL" | grep -q "Pi 5"; then
        DTS="${DRIVER_DIR}/sh1106-overlay-pi5.dts"
        info "Raspberry Pi 5 detectada"
    else
        DTS="${DRIVER_DIR}/sh1106-overlay.dts"
        info "Raspberry Pi 3/4 detectada"
    fi

    dtc -@ -I dts -O dtb -o "${DRIVER_DIR}/sh1106.dtbo" "$DTS" 2>/dev/null
    cp "${DRIVER_DIR}/sh1106.dtbo" /boot/overlays/
    ok "Device tree overlay instalado"

    if ! grep -q "dtoverlay=sh1106" "$CONFIG"; then
        echo "dtoverlay=sh1106" >> "$CONFIG"
        ok "dtoverlay=sh1106 añadido a $CONFIG"
    else
        ok "dtoverlay=sh1106 ya configurado"
    fi

    if ! grep -q "dtparam=i2c_arm=on" "$CONFIG"; then
        echo "dtparam=i2c_arm=on" >> "$CONFIG"
        ok "I2C habilitado en $CONFIG"
    else
        ok "I2C ya estaba habilitado"
    fi
fi

# ============================================
# PASO 4: Librería userspace
# ============================================
echo ""
echo "[4/7] Instalando librería userspace..."

LIB_DIR="${BASE_DIR}/lib"
[ -d "$LIB_DIR" ] || err "Directorio lib/ no encontrado"

cd "$LIB_DIR"
make -f Makefile.lib
make -f Makefile.lib install
ok "libsh1106.so instalada"
ok "pkg-config sh1106 disponible"

# ============================================
# PASO 5: Dashboard
# ============================================
echo ""
echo "[5/7] Instalando dashboard..."

DASH_DIR="${BASE_DIR}/dashboard"
if [ -d "$DASH_DIR" ]; then
    cd "$DASH_DIR"
    make -f Makefile.dashboard
    make -f Makefile.dashboard install
    ok "Dashboard instalado como servicio systemd"
else
    warn "Directorio dashboard/ no encontrado, saltando"
fi

# ============================================
# PASO 6: Cargar módulo ahora
# ============================================
echo ""
echo "[6/7] Cargando módulo..."

rmmod fb_sh1106 2>/dev/null && warn "fb_sh1106 descargado" || true
rmmod fbtft     2>/dev/null && warn "fbtft descargado"     || true
rmmod sh1106    2>/dev/null || true

modprobe sh1106

if [ "$USE_USB" = true ]; then
    # Instanciar manualmente ahora (sin reboot)
    sleep 2
    echo "sh1106 0x3c" > /sys/bus/i2c/devices/i2c-${USB_BUS}/new_device 2>/dev/null || true
fi

sleep 1

# ============================================
# PASO 7: Verificar
# ============================================
echo ""
echo "[7/7] Verificando..."

if [ -e /dev/sh1106 ]; then
    ok "/dev/sh1106 creado"
else
    warn "/dev/sh1106 no aparece — puede necesitar reboot"
fi

# ============================================
# RESUMEN
# ============================================
echo ""
echo "================================================"
echo -e "  ${GREEN}Instalación completada${NC}"
echo "================================================"
echo ""
if [ "$USE_USB" = true ]; then
    echo "  Modo:      USB-I2C (i2c-${USB_BUS})"
else
    echo "  Modo:      GPIO / Device Tree"
    echo "  Modelo:    ${MODEL}"
fi
echo "  Device:    $(ls /dev/sh1106 2>/dev/null && echo OK || echo 'pendiente reboot')"
echo "  Librería:  $(pkg-config --modversion sh1106 2>/dev/null || echo ERROR)"
echo ""
echo "  sudo reboot   ← necesario para activar cambios"
echo ""
