#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/seq_file.h>
/*This program is used for dump sleep gpio stats.*/
#ifndef ZTE_GPIO_DEBUG
#define ZTE_GPIO_DEBUG
#endif

#ifdef ZTE_GPIO_DEBUG
static int msm_pm_debug_mask = 0;
module_param_named(
	gpio_debug_mask, msm_pm_debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP
);


extern int msm_dump_gpios(struct seq_file *s, int curr_len, char *gpio_buffer) __attribute__((weak));
extern int pmic_dump_pins(struct seq_file *s, int curr_len, char *gpio_buffer) __attribute__((weak));
static char *gpio_sleep_status_info;

int vendor_print_gpio_buffer(struct seq_file *s)
{
	if (gpio_sleep_status_info)
		seq_printf(s, gpio_sleep_status_info);
	else if (msm_pm_debug_mask) {
		seq_puts(s, "Not suspended yet!\n");
	} else{
		seq_puts(s, " please echo 1 > /sys/module/gpio_show_suspend/parameters/gpio_debug_mask\n");
	}
	return 0;
}
EXPORT_SYMBOL(vendor_print_gpio_buffer);

int vendor_free_gpio_buffer(void)
{
	kfree(gpio_sleep_status_info);
	gpio_sleep_status_info = NULL;

	return 0;
}
EXPORT_SYMBOL(vendor_free_gpio_buffer);


void zte_pm_vendor_before_powercollapse(void)
{
	int curr_len = 0;/*Default close*/

	do {
		if (msm_pm_debug_mask) {
		if (gpio_sleep_status_info) {
			memset(gpio_sleep_status_info, 0, sizeof(*gpio_sleep_status_info));
		} else {
			gpio_sleep_status_info = kmalloc(25000, GFP_KERNEL);
			if (!gpio_sleep_status_info) {
				pr_err("[PM] kmalloc memory failed in %s\n", __func__);
				break;
			}
		}

		/*how to debug:
		1> echo 1 > /sys/module/gpio_show_suspend/parameters/gpio_debug_mask
		2> let device sleep
		3> cat dump_sleep_gpios
		*/
		curr_len = msm_dump_gpios(NULL, curr_len, gpio_sleep_status_info);
		curr_len = pmic_dump_pins(NULL, curr_len, gpio_sleep_status_info);
		}
	} while (0);

}
EXPORT_SYMBOL(zte_pm_vendor_before_powercollapse);
#endif
