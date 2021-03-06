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
#include <linux/irq.h>
#include <linux/of_irq.h>

#define DEVICE_CNT      1
#define DEVICE_NAME     "keyirq"

#define KEY_NUM         1
#define KEY0VALUE       0x01
#define INVAKEY         0xFF

struct irq_keydesc {
        int gpio;
        int irqnum;
        unsigned char value;
        char name[10];
        irqreturn_t (*handler)(int, void *);
};

struct keyirq_device_struct {
        int major;
        int minor;
        dev_t devid;
        struct cdev keyirq_cdev;
        struct class *class;
        struct device *device;
        struct device_node *nd;
        struct irq_keydesc keyirq[KEY_NUM];
        struct work_struct keywork[KEY_NUM];
        struct timer_list timer;
};
static struct keyirq_device_struct keyirq_dev;

static int keyirq_open(struct inode *inode, struct file *file);
static ssize_t keyirq_write(struct file *file,
                        const char __user *user,
                        size_t count,
                        loff_t *loff);
static int keyirq_release(struct inode *inode, struct file *file);

static struct file_operations ops = {
        .owner = THIS_MODULE,
        .open = keyirq_open,
        .write = keyirq_write,
        .release = keyirq_release,
};

static int keyirq_open(struct inode *inode, struct file *file)
{
        file->private_data = &keyirq_dev;
        return 0;
}

static ssize_t keyirq_write(struct file *file,
                        const char __user *user,
                        size_t count,
                        loff_t *loff)
{
        int ret = 0;
        unsigned char buf[1] = {0};
        struct keyirq_device_struct *dev = file->private_data;

        ret = copy_from_user(buf, user, 1);
        if (ret != 0) {
                goto error;
        }

error:
        return ret;
}

static int keyirq_release(struct inode *inode, struct file *file)
{
        file->private_data = NULL;
        return 0;
}

void key0_work_func(struct work_struct *work)
{
        struct keyirq_device_struct *dev = container_of(work, struct keyirq_device_struct, keywork[0]);

        dev->timer.data = (unsigned long)dev;
        mod_timer(&dev->timer, jiffies + msecs_to_jiffies(10));
}

void timer_func(unsigned long arg)
{
        int value = 0;
        struct keyirq_device_struct *dev = (struct keyirq_device_struct *)arg;

        value = gpio_get_value(dev->keyirq[0].gpio);
        if (value == 0) {
                printk("KEY0 Press!\n");
        } else if (value == 1) {
                printk("KEY0 Release!\n");
        }
}

static irqreturn_t key0_handler(int irq, void *dev_id)
{
        struct keyirq_device_struct *dev = dev_id;

        schedule_work(&dev->keywork[0]);

        return IRQ_HANDLED;
}

int key_io_config(struct keyirq_device_struct *dev)
{
        int ret = 0;
        int i = 0;
        int num = 0;
        int num_irq = 0;

        dev->nd = of_find_node_by_path("/key");
        if (dev->nd == NULL) {
                printk("fail find node!\n");
                ret = -EINVAL;
                goto fail_nd;
        }

        for (i = 0; i < KEY_NUM; i++) {
                dev->keyirq[i].gpio =of_get_named_gpio(dev->nd, "key-gpios", i);
                if (dev->keyirq[i].gpio < 0) {
                        printk("fail get named!\n");
                        ret = -EFAULT;
                        goto fail_get_named;
                }
        }

        for (i = 0; i < KEY_NUM; i++) {
                memset(dev->keyirq[i].name, 0, sizeof(dev->keyirq[i].name));
                sprintf(dev->keyirq[i].name, "KEY%d", i);
                ret = gpio_request(dev->keyirq[i].gpio, dev->keyirq[i].name);
                if (ret != 0) {
                        printk("gpio request error!\n");
                        ret = -EFAULT;
                        goto fail_request;
                }
                num ++;
        }

        for (i = 0; i < KEY_NUM; i++) {
                ret = gpio_direction_input(dev->keyirq[i].gpio);
                if (ret != 0) {
                        printk("gpio set dir error!\n");
                        ret = -EFAULT;
                        goto fail_set_dir;
                }
        }

        for (i = 0; i < KEY_NUM; i++) {
#if 1
                dev->keyirq[i].irqnum = gpio_to_irq(dev->keyirq[i].gpio);
#else
                dev->keyirq[i].irqnum = irq_of_parse_and_map(dev->nd, i);
#endif
                printk("key%d:gpio=%d, irqnum=%d\n", i, dev->keyirq[i].gpio,
                       dev->keyirq[i].irqnum);
        }

        dev->keyirq[0].handler = key0_handler;
        dev->keyirq[0].value = KEY0VALUE;

        INIT_WORK(&dev->keywork[0], key0_work_func);

        for (i = 0; i < KEY_NUM; i++) {
                ret = request_irq(dev->keyirq[i].irqnum, 
                                  dev->keyirq[i].handler, 
                                  IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING,
                                  dev->keyirq[i].name,
                                  dev);
                if (ret) {
                        printk("irq%d request error!\n", i);
                        goto fail_request_irq;
                }
                num_irq ++;
        }
        goto success;

fail_request_irq:
        for (i = 0; i < num_irq; i++)
                free_irq(dev->keyirq[i].irqnum, dev);
fail_set_dir:
fail_request:
        for (i = 0; i < num; i++)
                gpio_free(dev->keyirq[i].gpio);
fail_get_named:
fail_nd:
success:
        return ret;
}

static int __init keyirq_init(void)
{
        int ret = 0;
        if (keyirq_dev.major) {
                keyirq_dev.devid = MKDEV(keyirq_dev.major, keyirq_dev.minor);
                ret = register_chrdev_region(keyirq_dev.devid, DEVICE_CNT, DEVICE_NAME);
        } else {
                ret = alloc_chrdev_region(&keyirq_dev.devid, 0, DEVICE_CNT, DEVICE_NAME);
        }
        if (ret < 0) {
                printk("chrdev region error!\n");
                goto fail_chrdev_region;
        }
        keyirq_dev.major = MAJOR(keyirq_dev.devid);
        keyirq_dev.minor = MINOR(keyirq_dev.devid);
        printk("major:%d minor:%d\n", keyirq_dev.major, keyirq_dev.minor);

        cdev_init(&keyirq_dev.keyirq_cdev, &ops);
        ret = cdev_add(&keyirq_dev.keyirq_cdev, keyirq_dev.devid, DEVICE_CNT);
        if (ret < 0) {
                printk("cdev add error!\n");
                goto fail_cdev_add;
        }
        keyirq_dev.class = class_create(THIS_MODULE, DEVICE_NAME);
        if (IS_ERR(keyirq_dev.class)) {
                printk("class create error!\n");
                ret = -EINVAL;
                goto fail_class_create;
        }
        keyirq_dev.device = device_create(keyirq_dev.class, NULL,
                                       keyirq_dev.devid, NULL, DEVICE_NAME);
        if (IS_ERR(keyirq_dev.device)) {
                printk("device create error!\n");
                ret = -EINVAL;
                goto fail_device_create;
        }

        init_timer(&keyirq_dev.timer);
        keyirq_dev.timer.function = timer_func;

        ret = key_io_config(&keyirq_dev);
        if (ret != 0) {
                printk("key io config error!\n");
                goto fail_io_config;
        }

        goto success;
        
fail_io_config:
        del_timer(&keyirq_dev.timer);
        device_destroy(keyirq_dev.class, keyirq_dev.devid);
fail_device_create:
        class_destroy(keyirq_dev.class);
fail_class_create:
        cdev_del(&keyirq_dev.keyirq_cdev);
fail_cdev_add:
        unregister_chrdev_region(keyirq_dev.devid, DEVICE_CNT);
fail_chrdev_region:
success:
        return ret;
}

static void __exit keyirq_exit(void)
{
        int i = 0;

        del_timer(&keyirq_dev.timer);
        for (i = 0; i < KEY_NUM; i++)
                free_irq(keyirq_dev.keyirq[i].irqnum, &keyirq_dev);
        for (i = 0; i < KEY_NUM; i++)
                gpio_free(keyirq_dev.keyirq[i].gpio);
        device_destroy(keyirq_dev.class, keyirq_dev.devid);
        class_destroy(keyirq_dev.class);
        cdev_del(&keyirq_dev.keyirq_cdev);
        unregister_chrdev_region(keyirq_dev.devid, DEVICE_CNT);
}

module_init(keyirq_init);
module_exit(keyirq_exit);
MODULE_AUTHOR("wanglei");
MODULE_LICENSE("GPL");
