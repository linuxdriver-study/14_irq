#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/types.h>
#include <linux/ide.h>

#define LED_ON          1
#define LED_OFF         0

#define DEVICE_CNT      1
#define DEVICE_NAME     "led"

struct led_device_struct {
        int major;
        int minor;
        dev_t devid;
        struct cdev led_cdev;
        struct class *class;
        struct device *device;
        struct device_node *nd;
        int gpio_led;
};
static struct led_device_struct led_dev;

static int led_open(struct inode *inode, struct file *file);
static ssize_t led_write(struct file *file,
                        const char __user *user,
                        size_t count,
                        loff_t *loff);
static int led_release(struct inode *inode, struct file *file);

static struct file_operations ops = {
        .owner = THIS_MODULE,
        .open = led_open,
        .write = led_write,
        .release = led_release,
};

static int led_open(struct inode *inode, struct file *file)
{
        file->private_data = &led_dev;
        return 0;
}

static ssize_t led_write(struct file *file,
                        const char __user *user,
                        size_t count,
                        loff_t *loff)
{
        int ret = 0;
        unsigned char buf[1] = {0};
        struct led_device_struct *dev = file->private_data;

        ret = copy_from_user(buf, user, 1);
        if (ret != 0) {
                goto error;
        }

        if (buf[0] == LED_ON) {
                gpio_set_value(dev->gpio_led, 0);
        } else if (buf[0] == LED_OFF) {
                gpio_set_value(dev->gpio_led, 1);
        } else {
                ret = -EINVAL;
        }

error:
        return ret;
}

static int led_release(struct inode *inode, struct file *file)
{
        file->private_data = NULL;
        return 0;
}

int led_io_config(struct led_device_struct *dev)
{
        int ret = 0;

        dev->nd = of_find_node_by_path("/gpioled");
        if (dev->nd == NULL) {
                printk("find node error!\n");
                ret = -EFAULT;
                goto fail_find_node;
        }

        dev->gpio_led = of_get_named_gpio(dev->nd, "led-gpios", 0);
        if (dev->gpio_led < 0) {
                printk("get named error!\n");
                ret = -EFAULT;
                goto fail_get_named;
        }

        ret = gpio_request(dev->gpio_led, "led");
        if (ret != 0) {
                printk("gpio request error!\n");
                goto fail_gpio_request;
        }

        ret = gpio_direction_output(dev->gpio_led, 1);
        if (ret != 0) {
                printk("gpio dir set error!\n");
                goto fail_set_dir;
        }

fail_set_dir:
        gpio_free(dev->gpio_led);
fail_gpio_request:
fail_get_named:
fail_find_node:
        return ret;
}

static int __init led_init(void)
{
        int ret = 0;
        if (led_dev.major) {
                led_dev.devid = MKDEV(led_dev.major, led_dev.minor);
                ret = register_chrdev_region(led_dev.devid, DEVICE_CNT, DEVICE_NAME);
        } else {
                ret = alloc_chrdev_region(&led_dev.devid, 0, DEVICE_CNT, DEVICE_NAME);
        }
        if (ret < 0) {
                printk("chrdev region error!\n");
                goto fail_chrdev_region;
        }
        led_dev.major = MAJOR(led_dev.devid);
        led_dev.minor = MINOR(led_dev.devid);
        printk("major:%d minor:%d\n", led_dev.major, led_dev.minor);

        cdev_init(&led_dev.led_cdev, &ops);
        ret = cdev_add(&led_dev.led_cdev, led_dev.devid, DEVICE_CNT);
        if (ret < 0) {
                printk("cdev add error!\n");
                goto fail_cdev_add;
        }
        led_dev.class = class_create(THIS_MODULE, DEVICE_NAME);
        if (IS_ERR(led_dev.class)) {
                printk("class create error!\n");
                ret = -EINVAL;
                goto fail_class_create;
        }
        led_dev.device = device_create(led_dev.class, NULL,
                                       led_dev.devid, NULL, DEVICE_NAME);
        if (IS_ERR(led_dev.device)) {
                printk("device create error!\n");
                ret = -EINVAL;
                goto fail_device_create;
        }

        ret = led_io_config(&led_dev);
        if (ret != 0) {
                printk("led io config error!\n");
                goto fail_io_config;
        }

        goto success;
        
fail_io_config:
        device_destroy(led_dev.class, led_dev.devid);
fail_device_create:
        class_destroy(led_dev.class);
fail_class_create:
        cdev_del(&led_dev.led_cdev);
fail_cdev_add:
        unregister_chrdev_region(led_dev.devid, DEVICE_CNT);
fail_chrdev_region:
success:
        return ret;
}

static void __exit led_exit(void)
{
        gpio_set_value(led_dev.gpio_led, 1);
        gpio_free(led_dev.gpio_led);
        device_destroy(led_dev.class, led_dev.devid);
        class_destroy(led_dev.class);
        cdev_del(&led_dev.led_cdev);
        unregister_chrdev_region(led_dev.devid, DEVICE_CNT);
}

module_init(led_init);
module_exit(led_exit);
MODULE_AUTHOR("wanglei");
MODULE_LICENSE("GPL");
