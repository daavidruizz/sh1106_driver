obj-m += sh1106.o

KDIR  := /lib/modules/$(shell uname -r)/build
MDIR  := /lib/modules/$(shell uname -r)/extra

all:
	make -C $(KDIR) M=$(PWD) modules

clean:
	make -C $(KDIR) M=$(PWD) clean

install: all
	sudo mkdir -p $(MDIR)
	sudo cp sh1106.ko $(MDIR)/
	sudo depmod -a
	sudo cp 99-sh1106.rules /etc/udev/rules.d/
	sudo udevadm control --reload-rules
	sudo udevadm trigger
	sudo rmmod sh1106 2>/dev/null || true
	sudo modprobe sh1106
	@echo "✓ Driver instalado y cargado"

reload:
	sudo rmmod sh1106 2>/dev/null || true
	sudo cp sh1106.ko $(MDIR)/
	sudo depmod -a
	sudo modprobe sh1106
	@echo "✓ Driver recargado"

uninstall:
	sudo rmmod sh1106 2>/dev/null || true
	sudo rm -f $(MDIR)/sh1106.ko
	sudo rm -f /etc/udev/rules.d/99-sh1106.rules
	sudo depmod -a
	sudo udevadm control --reload-rules
	@echo "✓ Driver desinstalado"

status:
	@echo "=== Módulo ==="
	@lsmod | grep sh1106 || echo "No cargado"
	@echo "=== Device ==="
	@ls -la /dev/sh1106 2>/dev/null || echo "No existe"
	@echo "=== dmesg ==="
	@dmesg | grep sh1106 | tail -5

.PHONY: all clean install reload uninstall status
