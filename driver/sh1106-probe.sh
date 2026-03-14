#!/bin/bash
# ============================================
# sh1106-probe.sh
# Instancia el SH1106 en un adaptador USB-I2C
#
# Detección automática:
#   - CH341 (vendor 1a86) — cualquier PID
#   - Otros adaptadores USB-I2C conocidos
#
# Llamado por:
#   - udev cuando aparece el adaptador USB
#   - sh1106-probe.service en el boot
# ============================================

# Vendors USB-I2C soportados
# Añadir más según necesidad
KNOWN_VENDORS="1a86"  # CH341 (cualquier PID)

find_i2c_bus() {
    local bus=""

    # Método 1: buscar por vendor en sysfs USB
    for vendor in $KNOWN_VENDORS; do
        for dev in /sys/bus/usb/devices/*/; do
            v=$(cat "${dev}idVendor" 2>/dev/null)
            if [ "$v" = "$vendor" ]; then
                # Buscar el bus i2c creado por este dispositivo USB
                bus=$(find "$dev" -name "i2c-[0-9]*" 2>/dev/null \
                      | head -1 | grep -o 'i2c-[0-9]*' | sed 's/i2c-//')
                if [ -n "$bus" ]; then
                    echo "$bus"
                    return 0
                fi
            fi
        done
    done

    # Método 2: fallback — buscar por descripción en i2cdetect
    bus=$(i2cdetect -l 2>/dev/null \
          | grep -i "usb\|tiny\|ch341\|ch340\|cp210\|ft232" \
          | head -1 | awk '{print $1}' | sed 's/i2c-//')
    if [ -n "$bus" ]; then
        echo "$bus"
        return 0
    fi

    return 1
}

MAX_RETRIES=15
RETRY_DELAY=2

echo "sh1106-probe: Buscando adaptador USB-I2C..."

for i in $(seq 1 $MAX_RETRIES); do
    BUS=$(find_i2c_bus)

    if [ -n "$BUS" ]; then
        echo "sh1106-probe: Adaptador encontrado en i2c-${BUS}"

        # Verificar VID del adaptador encontrado
        VID=$(cat /sys/bus/i2c/devices/i2c-${BUS}/../../../idVendor 2>/dev/null || echo "desconocido")
        PID=$(cat /sys/bus/i2c/devices/i2c-${BUS}/../../../idProduct 2>/dev/null || echo "desconocido")
        echo "sh1106-probe: USB VID:PID = ${VID}:${PID}"

        # Verificar que el SH1106 responde en 0x3C
        if i2cdetect -y "$BUS" 2>/dev/null | grep -q "3c\|3C"; then
            echo "sh1106-probe: SH1106 detectado en 0x3C"
        else
            echo "sh1106-probe: AVISO — SH1106 no responde en 0x3C (¿conectado?)"
        fi

        # Comprobar si ya está instanciado
        if [ -e "/sys/bus/i2c/devices/i2c-${BUS}/i2c-${BUS}-0x3c" ] || \
           [ -e "/sys/bus/i2c/devices/${BUS}-003c" ]; then
            echo "sh1106-probe: Ya instanciado, nada que hacer"
            exit 0
        fi

        # Instanciar — dispara probe() del driver
        echo "sh1106 0x3c" > /sys/bus/i2c/devices/i2c-${BUS}/new_device
        echo "sh1106-probe: SH1106 instanciado en i2c-${BUS}"
        exit 0
    fi

    echo "sh1106-probe: Intento $i/$MAX_RETRIES — esperando USB-I2C..."
    sleep $RETRY_DELAY
done

echo "sh1106-probe: ERROR — Adaptador USB-I2C no encontrado"
exit 1
