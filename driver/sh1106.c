#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/atomic.h>
#include "sh1106_ioctl.h"
#include "sh1106_logo.h"

#define DEVICE_NAME      "sh1106"
#define CLASS_NAME       "sh1106_class"
#define FRAMEBUFFER_SIZE 1024

struct sh1106_data {
    struct i2c_client *client;
    struct cdev cdev;
    dev_t devnum;
    struct class *class;
    struct device *device;
    u8 framebuffer[FRAMEBUFFER_SIZE];
    u8 contrast;
    bool power_on;
    bool inverted;
    bool flip_vertical;
    bool flip_horizontal;
    struct mutex lock;
    unsigned long write_count;
    unsigned long ioctl_count;
    ktime_t last_update;

    /* MEJORA 1: acceso exclusivo — solo un proceso puede abrir /dev/sh1106 */
    atomic_t open_count;

    /* Flag de hardware presente — false si la pantalla no responde por I2C.
     * El driver carga igualmente pero las operaciones devuelven -ENODEV
     * hasta que la pantalla vuelva a estar disponible. */
    bool hw_present;
};

// =============== FUNCIONES I2C ===============

/* MEJORA 3: manejo de errores I2C — dev_err ya estaba, añadimos
 * retorno consistente y comprobación en todos los callers */
static int sh1106_send_command(struct i2c_client *client, u8 cmd)
{
    u8 buf[2] = {0x00, cmd};
    int ret = i2c_master_send(client, buf, 2);
    if (ret != 2) {
        dev_err(&client->dev, "Error I2C enviando comando 0x%02x (ret=%d)\n",
                cmd, ret);
        return ret < 0 ? ret : -EIO;
    }
    return 0;
}

static int sh1106_send_data(struct i2c_client *client, u8 data)
{
    u8 buf[2] = {0x40, data};
    int ret = i2c_master_send(client, buf, 2);
    if (ret != 2) {
        dev_err(&client->dev, "Error I2C enviando dato 0x%02x (ret=%d)\n",
                data, ret);
        return ret < 0 ? ret : -EIO;
    }
    return 0;
}

static int sh1106_hw_init(struct i2c_client *client)
{
    int ret;

    /* MEJORA 4: dev_info → dev_dbg para no saturar el log en producción.
     * Para verlo: echo 8 > /proc/sys/kernel/printk  o  dyndbg */
    dev_dbg(&client->dev, "Inicializando hardware...\n");

    ret = sh1106_send_command(client, 0xAE);
    if (ret) return ret;

    usleep_range(100000, 110000);

    for (int page = 0; page < 8; page++) {
        ret = sh1106_send_command(client, 0xB0 + page);
        if (ret) return ret;
        ret = sh1106_send_command(client, 0x00);
        if (ret) return ret;
        ret = sh1106_send_command(client, 0x10);
        if (ret) return ret;
        for (int col = 0; col < 132; col++) {
            ret = sh1106_send_data(client, 0x00);
            if (ret) return ret;
        }
    }

    usleep_range(100000, 110000);

    ret = sh1106_send_command(client, 0xD5); if (ret) return ret;
    ret = sh1106_send_command(client, 0x80); if (ret) return ret;
    ret = sh1106_send_command(client, 0xA8); if (ret) return ret;
    ret = sh1106_send_command(client, 0x3F); if (ret) return ret;
    ret = sh1106_send_command(client, 0xD3); if (ret) return ret;
    ret = sh1106_send_command(client, 0x00); if (ret) return ret;
    ret = sh1106_send_command(client, 0x40 | 0x00); if (ret) return ret;
    ret = sh1106_send_command(client, 0xAD); if (ret) return ret;
    ret = sh1106_send_command(client, 0x8B); if (ret) return ret;
    ret = sh1106_send_command(client, 0xA0); if (ret) return ret;
    ret = sh1106_send_command(client, 0xC0); if (ret) return ret;
    ret = sh1106_send_command(client, 0xDA); if (ret) return ret;
    ret = sh1106_send_command(client, 0x12); if (ret) return ret;
    ret = sh1106_send_command(client, 0x81); if (ret) return ret;
    ret = sh1106_send_command(client, 0xFF); if (ret) return ret;
    ret = sh1106_send_command(client, 0xD9); if (ret) return ret;
    ret = sh1106_send_command(client, 0x22); if (ret) return ret;
    ret = sh1106_send_command(client, 0xDB); if (ret) return ret;
    ret = sh1106_send_command(client, 0x35); if (ret) return ret;
    ret = sh1106_send_command(client, 0x33); if (ret) return ret;
    ret = sh1106_send_command(client, 0xA4); if (ret) return ret;
    ret = sh1106_send_command(client, 0xA6); if (ret) return ret;

    usleep_range(100000, 110000);

    ret = sh1106_send_command(client, 0xAF);
    if (ret) return ret;

    usleep_range(100000, 110000);

    dev_dbg(&client->dev, "Hardware inicializado OK\n");
    return 0;
}

// =============== SYSFS ===============

static ssize_t contrast_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    return sprintf(buf, "%d\n", data->contrast);
}

static ssize_t contrast_store(struct device *dev,
                               struct device_attribute *attr,
                               const char *buf, size_t count)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    u8 val;
    int ret;

    ret = kstrtou8(buf, 10, &val);
    if (ret)
        return ret;

    mutex_lock(&data->lock);
    data->contrast = val;
    ret = sh1106_send_command(data->client, 0x81);
    if (ret) goto out;
    ret = sh1106_send_command(data->client, val);
    if (ret) goto out;
    ret = count;
out:
    mutex_unlock(&data->lock);
    return ret;
}
static DEVICE_ATTR(contrast, 0644, contrast_show, contrast_store);

static ssize_t display_power_show(struct device *dev,
                                   struct device_attribute *attr, char *buf)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    return sprintf(buf, "%d\n", data->power_on ? 1 : 0);
}

static ssize_t display_power_store(struct device *dev,
                                    struct device_attribute *attr,
                                    const char *buf, size_t count)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    int val, ret;

    ret = kstrtoint(buf, 10, &val);
    if (ret) return ret;
    if (val != 0 && val != 1) return -EINVAL;

    mutex_lock(&data->lock);
    data->power_on = (bool)val;
    ret = sh1106_send_command(data->client, val ? 0xAF : 0xAE);
    if (ret) goto out;
    ret = count;
out:
    mutex_unlock(&data->lock);
    return ret;
}
static DEVICE_ATTR(display_power, 0644, display_power_show, display_power_store);

static ssize_t invert_show(struct device *dev,
                            struct device_attribute *attr, char *buf)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    return sprintf(buf, "%d\n", data->inverted ? 1 : 0);
}

static ssize_t invert_store(struct device *dev,
                             struct device_attribute *attr,
                             const char *buf, size_t count)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    int val, ret;

    ret = kstrtoint(buf, 10, &val);
    if (ret) return ret;
    if (val != 0 && val != 1) return -EINVAL;

    mutex_lock(&data->lock);
    data->inverted = (bool)val;
    ret = sh1106_send_command(data->client, val ? 0xA7 : 0xA6);
    if (ret) goto out;
    ret = count;
out:
    mutex_unlock(&data->lock);
    return ret;
}
static DEVICE_ATTR(invert, 0644, invert_show, invert_store);

static ssize_t flip_horizontal_show(struct device *dev,
                                     struct device_attribute *attr, char *buf)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    return sprintf(buf, "%d\n", data->flip_horizontal ? 1 : 0);
}

static ssize_t flip_horizontal_store(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf, size_t count)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    int val, ret;

    ret = kstrtoint(buf, 10, &val);
    if (ret) return ret;
    if (val != 0 && val != 1) return -EINVAL;

    mutex_lock(&data->lock);
    data->flip_horizontal = (bool)val;
    ret = sh1106_send_command(data->client, val ? 0xA1 : 0xA0);
    if (ret) goto out;
    ret = count;
out:
    mutex_unlock(&data->lock);
    return ret;
}
static DEVICE_ATTR(flip_horizontal, 0644, flip_horizontal_show, flip_horizontal_store);

static ssize_t flip_vertical_show(struct device *dev,
                                   struct device_attribute *attr, char *buf)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    return sprintf(buf, "%d\n", data->flip_vertical ? 1 : 0);
}

static ssize_t flip_vertical_store(struct device *dev,
                                    struct device_attribute *attr,
                                    const char *buf, size_t count)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    int val, ret;

    ret = kstrtoint(buf, 10, &val);
    if (ret) return ret;
    if (val != 0 && val != 1) return -EINVAL;

    mutex_lock(&data->lock);
    data->flip_vertical = (bool)val;
    ret = sh1106_send_command(data->client, val ? 0xC8 : 0xC0);
    if (ret) goto out;
    ret = count;
out:
    mutex_unlock(&data->lock);
    return ret;
}
static DEVICE_ATTR(flip_vertical, 0644, flip_vertical_show, flip_vertical_store);

static ssize_t stats_show(struct device *dev,
                           struct device_attribute *attr, char *buf)
{
    struct sh1106_data *data = dev_get_drvdata(dev);
    s64 last_ms = 0;

    if (data->write_count > 0)
        last_ms = ktime_to_ms(ktime_sub(ktime_get(), data->last_update));

    return sprintf(buf,
        "write_count:  %lu\n"
        "ioctl_count:  %lu\n"
        "contrast:     %d\n"
        "power:        %d\n"
        "inverted:     %d\n"
        "flip_h:       %d\n"
        "flip_v:       %d\n"
        "open:         %d\n"
        "hw_present:   %d\n"
        "last_update:  %lld ms ago\n",
        data->write_count,
        data->ioctl_count,
        data->contrast,
        data->power_on ? 1 : 0,
        data->inverted ? 1 : 0,
        data->flip_horizontal ? 1 : 0,
        data->flip_vertical ? 1 : 0,
        atomic_read(&data->open_count),
        data->hw_present ? 1 : 0,
        last_ms);
}
static DEVICE_ATTR(stats, 0444, stats_show, NULL);

static struct attribute *sh1106_attrs[] = {
    &dev_attr_contrast.attr,
    &dev_attr_display_power.attr,
    &dev_attr_invert.attr,
    &dev_attr_flip_horizontal.attr,
    &dev_attr_flip_vertical.attr,
    &dev_attr_stats.attr,
    NULL
};

static const struct attribute_group sh1106_attr_group = {
    .attrs = sh1106_attrs,
};

// =============== FILE OPERATIONS ===============

static int sh1106_open(struct inode *inode, struct file *file)
{
    struct sh1106_data *data;

    data = container_of(inode->i_cdev, struct sh1106_data, cdev);

    /* MEJORA 1: acceso exclusivo
     * atomic_cmpxchg(ptr, old, new) — si *ptr == old → escribe new y devuelve old
     * Si devuelve != 0 significa que ya había alguien → EBUSY */
    if (atomic_cmpxchg(&data->open_count, 0, 1) != 0) {
        dev_warn(&data->client->dev,
                 "Device ocupado — ya hay un proceso usando /dev/sh1106\n");
        return -EBUSY;
    }

    file->private_data = data;
    dev_dbg(&data->client->dev, "Device opened\n");
    return 0;
}

static int sh1106_release(struct inode *inode, struct file *file)
{
    struct sh1106_data *data = file->private_data;

    /* MEJORA 1: liberar el acceso exclusivo al cerrar */
    atomic_set(&data->open_count, 0);

    dev_dbg(&data->client->dev, "Device closed\n");
    return 0;
}

static int sh1106_update_display(struct sh1106_data *data)
{
    int ret, page, col;
    u8 buf[129];

    /* Si la pantalla no estaba disponible, intentar reinicializar */
    if (!data->hw_present) {
        ret = sh1106_hw_init(data->client);
        if (ret < 0)
            return -ENODEV;
        dev_info(&data->client->dev, "Pantalla reconectada\n");
        data->hw_present = true;
    }

    for (page = 0; page < 8; page++) {
        /* MEJORA 3: comprobar ret en cada comando I2C,
         * abortar y reportar en qué página falló */
        ret = sh1106_send_command(data->client, 0xB0 + page);
        if (ret) {
            dev_err(&data->client->dev,
                    "Error I2C en página %d (cmd page)\n", page);
            return ret;
        }
        ret = sh1106_send_command(data->client, 0x02);
        if (ret) {
            dev_err(&data->client->dev,
                    "Error I2C en página %d (cmd col low)\n", page);
            return ret;
        }
        ret = sh1106_send_command(data->client, 0x10);
        if (ret) {
            dev_err(&data->client->dev,
                    "Error I2C en página %d (cmd col high)\n", page);
            return ret;
        }

        /* Mandar los 128 bytes de la página en un solo i2c_master_send
         * en vez de byte a byte — más eficiente y menos ruido en el bus */
        buf[0] = 0x40;  /* Co=0, D/C=1 → datos */
        for (col = 0; col < 128; col++)
            buf[col + 1] = data->framebuffer[page * 128 + col];

        ret = i2c_master_send(data->client, buf, 129);
        if (ret != 129) {
            dev_err(&data->client->dev,
                    "Error I2C enviando página %d (ret=%d) — marcando hw ausente\n",
                    page, ret);
            data->hw_present = false;
            return ret < 0 ? ret : -EIO;
        }
    }
    return 0;
}

static ssize_t sh1106_write(struct file *file, const char __user *buffer,
                            size_t len, loff_t *offset)
{
    int ret;
    struct sh1106_data *data = file->private_data;

    /* MEJORA 6: validar tamaño exacto — el framebuffer es siempre 1024 bytes */
    if (len != FRAMEBUFFER_SIZE) {
        dev_warn(&data->client->dev,
                 "write(): se esperan %d bytes exactos, recibidos %zu\n",
                 FRAMEBUFFER_SIZE, len);
        return -EINVAL;
    }

    mutex_lock(&data->lock);

    if (copy_from_user(data->framebuffer, buffer, FRAMEBUFFER_SIZE)) {
        mutex_unlock(&data->lock);
        return -EFAULT;
    }

    /* MEJORA 3: propagar error de I2C al proceso de userspace */
    ret = sh1106_update_display(data);
    if (ret < 0) {
        mutex_unlock(&data->lock);
        return ret;
    }

    data->write_count++;
    data->last_update = ktime_get();

    mutex_unlock(&data->lock);
    return FRAMEBUFFER_SIZE;
}

/* MEJORA 5: read() — permite leer el framebuffer actual desde userspace
 * Útil para debug o para que un proceso lea el estado antes de modificarlo */
static ssize_t sh1106_read(struct file *file, char __user *buffer,
                           size_t len, loff_t *offset)
{
    struct sh1106_data *data = file->private_data;

    if (len != FRAMEBUFFER_SIZE)
        return -EINVAL;

    if (*offset >= FRAMEBUFFER_SIZE)
        return 0;

    mutex_lock(&data->lock);

    if (copy_to_user(buffer, data->framebuffer, FRAMEBUFFER_SIZE)) {
        mutex_unlock(&data->lock);
        return -EFAULT;
    }

    mutex_unlock(&data->lock);
    *offset += FRAMEBUFFER_SIZE;
    return FRAMEBUFFER_SIZE;
}

static long sh1106_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct sh1106_data *data = file->private_data;
    int ret = 0;
    int value;

    mutex_lock(&data->lock);

    switch (cmd) {

    case SH1106_IOC_CLEAR:
        dev_dbg(&data->client->dev, "ioctl: CLEAR\n");
        memset(data->framebuffer, 0, FRAMEBUFFER_SIZE);
        ret = sh1106_update_display(data);
        break;

    case SH1106_IOC_SET_CONTRAST:
        value = (int)arg;
        if (value < 0 || value > 255) { ret = -EINVAL; break; }
        dev_dbg(&data->client->dev, "ioctl: SET_CONTRAST %d\n", value);
        data->contrast = (u8)value;
        ret = sh1106_send_command(data->client, 0x81);
        if (ret) break;
        ret = sh1106_send_command(data->client, data->contrast);
        break;

    case SH1106_IOC_INVERT:
        value = (int)arg;
        dev_dbg(&data->client->dev, "ioctl: INVERT %d\n", value);
        data->inverted = (bool)value;
        ret = sh1106_send_command(data->client, value ? 0xA7 : 0xA6);
        break;

    case SH1106_IOC_FLIP_VERTICAL:
        value = (int)arg;
        dev_dbg(&data->client->dev, "ioctl: FLIP_VERTICAL %d\n", value);
        data->flip_vertical = (bool)value;
        ret = sh1106_send_command(data->client, value ? 0xC8 : 0xC0);
        break;

    case SH1106_IOC_FLIP_HORIZONTAL:
        value = (int)arg;
        dev_dbg(&data->client->dev, "ioctl: FLIP_HORIZONTAL %d\n", value);
        data->flip_horizontal = (bool)value;
        ret = sh1106_send_command(data->client, value ? 0xA1 : 0xA0);
        break;

    case SH1106_IOC_POWER:
        value = (int)arg;
        dev_dbg(&data->client->dev, "ioctl: POWER %d\n", value);
        data->power_on = (bool)value;
        ret = sh1106_send_command(data->client, value ? 0xAF : 0xAE);
        break;

    default:
        dev_warn(&data->client->dev,
                 "ioctl: Comando desconocido 0x%x\n", cmd);
        ret = -ENOTTY;
        break;
    }

    if (ret == 0)
        data->ioctl_count++;

    mutex_unlock(&data->lock);
    return ret;
}

static const struct file_operations sh1106_fops = {
    .owner          = THIS_MODULE,
    .open           = sh1106_open,
    .release        = sh1106_release,
    .write          = sh1106_write,
    .read           = sh1106_read,
    .unlocked_ioctl = sh1106_ioctl,
};

// =============== PROBE / REMOVE ===============

static int sh1106_probe(struct i2c_client *client)
{
    struct sh1106_data *data;
    int ret;

    dev_info(&client->dev, "Probe ejecutado\n");

    data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    data->client          = client;
    data->contrast        = 0xFF;
    data->power_on        = false;
    data->inverted        = false;
    data->flip_vertical   = false;
    data->flip_horizontal = false;

    /* MEJORA 1: inicializar contador de acceso exclusivo a 0 (libre) */
    atomic_set(&data->open_count, 0);
    data->hw_present = false;  /* se actualiza tras hw_init */

    mutex_init(&data->lock);
    memset(data->framebuffer, 0, FRAMEBUFFER_SIZE);
    i2c_set_clientdata(client, data);

    ret = sh1106_hw_init(client);
    if (ret < 0) {
        /* Pantalla no responde — cargar el driver igualmente.
         * /dev/sh1106 se crea, sysfs funciona, pero write/ioctl
         * devolverán -ENODEV hasta que la pantalla vuelva. */
        dev_warn(&client->dev,
                 "Hardware no responde (ret=%d) — driver cargado sin pantalla\n",
                 ret);
        data->hw_present = false;
    } else {
        data->hw_present = true;
        sh1106_boot_build(data->framebuffer);
        ret = sh1106_update_display(data);
        if (ret < 0) {
            dev_warn(&client->dev,
                     "Error mostrando boot screen: %d\n", ret);
            data->hw_present = false;
        } else {
            data->power_on = true;
        }
    }

    ret = alloc_chrdev_region(&data->devnum, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        dev_err(&client->dev, "Error reservando char device\n");
        return ret;
    }

    dev_info(&client->dev, "Char device: major=%d, minor=%d\n",
             MAJOR(data->devnum), MINOR(data->devnum));

    cdev_init(&data->cdev, &sh1106_fops);
    data->cdev.owner = THIS_MODULE;

    ret = cdev_add(&data->cdev, data->devnum, 1);
    if (ret < 0) {
        dev_err(&client->dev, "Error añadiendo cdev\n");
        goto err_cdev;
    }

    data->class = class_create(CLASS_NAME);
    if (IS_ERR(data->class)) {
        ret = PTR_ERR(data->class);
        dev_err(&client->dev, "Error creando clase: %d\n", ret);
        goto err_class;
    }

    data->device = device_create(data->class, &client->dev,
                                  data->devnum, NULL, DEVICE_NAME);
    if (IS_ERR(data->device)) {
        ret = PTR_ERR(data->device);
        dev_err(&client->dev, "Error creando device: %d\n", ret);
        goto err_device;
    }

    dev_set_drvdata(data->device, data);

    ret = sysfs_create_group(&data->device->kobj, &sh1106_attr_group);
    if (ret) {
        dev_err(&client->dev, "Error creando sysfs: %d\n", ret);
        goto err_sysfs;
    }

    dev_info(&client->dev, "Driver inicializado: /dev/sh1106 creado\n");
    return 0;

err_sysfs:
    device_destroy(data->class, data->devnum);
err_device:
    class_destroy(data->class);
err_class:
    cdev_del(&data->cdev);
err_cdev:
    unregister_chrdev_region(data->devnum, 1);
    return ret;
}

static void sh1106_remove(struct i2c_client *client)
{
    struct sh1106_data *data = i2c_get_clientdata(client);

    dev_info(&client->dev, "Remove ejecutado\n");

    sh1106_send_command(client, 0xAE);

    sysfs_remove_group(&data->device->kobj, &sh1106_attr_group);
    device_destroy(data->class, data->devnum);
    class_destroy(data->class);
    cdev_del(&data->cdev);
    unregister_chrdev_region(data->devnum, 1);
    mutex_destroy(&data->lock);

    dev_info(&client->dev, "Driver descargado\n");
}

// =============== MATCHING ===============

static const struct of_device_id sh1106_of_match[] = {
    { .compatible = "sinowealth,sh1106" },
    { }
};
MODULE_DEVICE_TABLE(of, sh1106_of_match);

// =============== DRIVER ===============

static struct i2c_driver sh1106_driver = {
    .driver = {
        .name           = "sh1106",
        .of_match_table = sh1106_of_match,
    },
    .probe  = sh1106_probe,
    .remove = sh1106_remove,
};

module_i2c_driver(sh1106_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Ruiz");
MODULE_DESCRIPTION("Driver SH1106 OLED");
