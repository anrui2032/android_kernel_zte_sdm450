#ifndef ZFG_LCD_COMMON_H
#define ZFG_LCD_COMMON_H

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/sysfs.h>
#include <linux/proc_fs.h>
#include <linux/kobject.h>
#include "mdss_panel.h"
#include "mdss_dsi.h"
/*
#define CONFIG_ZFG_LCD_COMMON_FUNCTION
#define CONFIG_ZFG_LCD_REG_DEBUG
*/

#ifdef CONFIG_ZFG_LCD_COMMON_FUNCTION

struct zfg_lcd_ctrl_data {
	char lcd_tp_binding_resetpin;
	char lcd_reset_high_sleeping;
#ifdef CONFIG_ZFG_LCD_GPIO_CTRL_POWER
	int disp_avdd_en_gpio;
	int disp_iovdd_en_gpio;
	int disp_vsp_en_gpio;
	int disp_vsn_en_gpio;
	int (*gpio_enable_lcd_power)(struct mdss_panel_data *pdata, int enable);
#endif
#ifdef CONFIG_ZFG_LCDBL_I2C_CTRL_VSP_VSN
	u32 lcd_bl_vsp_vsn_voltage;
#endif
#ifdef CONFIG_ZFG_LCD_BACKLIGHT_LEVEL_CURVE
	u32 lcd_bl_curve_mode;
	int (*zfg_convert_brightness)(int level, u32 bl_max);
#endif
	const char *lcd_panel_name;
	struct kobject *kobj;
	u32 lcd_esd_num;
#ifdef CONFIG_ZFG_LCD_CABC3_EXTREME_POWER_SAVE
	int cabc_value;
	struct dsi_panel_cmds *cabc_off_cmds;
	struct dsi_panel_cmds *cabc_low_cmds;
	struct dsi_panel_cmds *cabc_medium_cmds;
	struct dsi_panel_cmds *cabc_high_cmds;
	struct mutex panel_sys_lock;
	int (*zfg_set_cabc_mode)(int cabc_mode);
#endif
};

void zfg_lcd_common_func(struct mdss_dsi_ctrl_pdata *ctrl_pdata, struct device_node *node);
int zfg_gpio_ctrl_lcd_power_init(struct platform_device *ctrl_pdev, struct mdss_dsi_ctrl_pdata *ctrl_pdata);
int zfg_create_cabc_sys(struct mdss_dsi_ctrl_pdata *pdata);
const char *zfg_get_lcd_panel_name(void);

#endif 

#endif

