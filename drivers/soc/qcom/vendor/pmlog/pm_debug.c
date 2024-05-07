/*
 *This program is used for recode the ap and modem's sleep and wake time.
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/io.h>
#include <linux/ktime.h>
#include <linux/smp.h>
#include <linux/tick.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/msm-bus.h>
#include <linux/uaccess.h>
#include <linux/dma-mapping.h>
#include <soc/qcom/spm.h>
#include <soc/qcom/pm.h>
#include <soc/qcom/scm.h>
#include <soc/qcom/scm-boot.h>
#include <asm/suspend.h>
#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/system_misc.h>
#include <soc/qcom/socinfo.h>
#include "../../../../clk/msm/clock.h"
#include <linux/seq_file.h>

#include <linux/fb.h>

static uint32_t showmodemsleep = 0;
static uint32_t showmodemawake = 0;
static uint32_t showmodemsleeporawake = 0;
static uint32_t showphyslinktime = 0;

static int msm_pm_debug_mask = 1;
module_param_named(
	debug_mask, msm_pm_debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP
);


static int apSleep_modemAwake_timeThreshold = 10;
static int apSleep_modemAwake_precent = 900;
static int apSleep_modemAwake_count = 5;
module_param_named(zfg_amss_time_threshold, apSleep_modemAwake_timeThreshold, int, S_IRUGO | S_IWUSR | S_IWGRP);
module_param_named(zfg_amss_awake_precent, apSleep_modemAwake_precent, int, S_IRUGO | S_IWUSR | S_IWGRP);
module_param_named(zfg_amss_awake_acount, apSleep_modemAwake_count, int, S_IRUGO | S_IWUSR | S_IWGRP);
static int zfg_amss_acount;
static struct device  *msm_cpu_pm_dev;


enum {
	MSM_PM_DEBUG_ZFG_IDLE_CLOCK = BIT(10),/* LOG default not open*/
};

typedef struct {
	uint32_t app_suspend_state;
	uint32_t modemsleeptime;
	uint32_t modemawaketime;
	uint32_t modemsleep_or_awake;/*1 sleep,2 awake,0 never enter sleep*/
	uint32_t physlinktime;
	uint32_t modemawake_timeout_crash;
} pm_count_time;

static pm_count_time *zfg_imem_ptr = NULL;

static int kernel_sleep_count;



/*screenontime record each on -off time*/
static long screenontime = 0;
static long screenontimebeforesuspend = 0;
static bool screenofffirstime = true;
static void update_screenon_time(bool lcdonoff)
{
	pr_info("[PM_V] turn LCD %s %s\n", lcdonoff ? "ON" : "OFF", screenofffirstime ? " first time":" ");
	if (screenofffirstime) {
		if (!lcdonoff)
			screenofffirstime = false;
		return;
	}
	if (lcdonoff) {
		screenontime = current_kernel_time().tv_sec;
	} else {
		screenontime = current_kernel_time().tv_sec - screenontime;
		screenontimebeforesuspend += screenontime;
	}
}

static int lcd_fb_callback(struct notifier_block *nfb,
				 unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;

	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		/*pr_info("[PM_V] %s enter , blank=%d\n", __func__, *blank);*/

		if (*blank == FB_BLANK_UNBLANK) {
			/*notes:update resume time,
			indicate the LCD will turn on.*/
			update_screenon_time(true);
		} else if ((*blank == FB_BLANK_POWERDOWN) || (*blank == FB_BLANK_NORMAL)) {
			/*notes:update suspend time,
			indicate the LCD will turn off.*/
			update_screenon_time(false);
		}
	}
	return 0;
}

static struct notifier_block __refdata lcd_fb_notifier = {
	.notifier_call = lcd_fb_callback,
};




#define MSM_PM_DPRINTK(mask, level, message, ...) \
	do { \
		if ((mask) & msm_pm_debug_mask) \
			printk(level message, ## __VA_ARGS__); \
	} while (0)

static unsigned pm_modem_sleep_time_get(void)
{
	if (!zfg_imem_ptr) {
		pr_err("zfg_imem_ptr is null,pm_modem_sleep_time_get return\n");
		return 0;
	}
	pr_info("[PM_V] get modemsleeptime %d\n", zfg_imem_ptr->modemsleeptime);
	return zfg_imem_ptr->modemsleeptime;
}

static unsigned pm_modem_phys_link_time_get(void)
{
	if (!zfg_imem_ptr) {
		pr_err("zfg_imem_ptr is null,pm_modem_phys_link_time_get return\n");
		return 0;
	}
	pr_info("[PM_V] get physlinktime %d\n", zfg_imem_ptr->physlinktime);
	return zfg_imem_ptr->physlinktime;
}

static unsigned pm_modem_awake_time_get(int *current_sleep)
{
	if (!zfg_imem_ptr) {
		pr_err("zfg_imem_ptr is null,pm_modem_awake_time_get return\n");
		return 0;
	}
	*current_sleep =  zfg_imem_ptr->modemsleep_or_awake;
	pr_info("[PM_V] get modemawaketime %d,current_sleep=%d\n", zfg_imem_ptr->modemawaketime, *current_sleep);
	return zfg_imem_ptr->modemawaketime;
}

static int pm_modem_sleep_time_show(char *buffer, struct kernel_param *kp)
{
	if (!zfg_imem_ptr) {
		pr_err("zfg_imem_ptr is null,pm_modem_sleep_time_get return\n");
		return 0;
	}
	pr_info("[PM_V] get modemsleeptime %d\n", zfg_imem_ptr->modemsleeptime);
	return  snprintf(buffer, 8,  "%d", (zfg_imem_ptr->modemsleeptime / 1000));
}

static int pm_modem_awake_time_show(char *buffer, struct kernel_param *kp)
{
	if (!zfg_imem_ptr) {
		pr_err("zfg_imem_ptr is null,modemawaketime return\n");
		return 0;
	}
	pr_info("[PM_V] get modemawaketime %d\n", zfg_imem_ptr->modemawaketime);
	return  snprintf(buffer, 8, "%d", (zfg_imem_ptr->modemawaketime / 1000));
}
static int pm_modem_sleep_or_awake_show(char *buffer, struct kernel_param *kp)
{
	if (!zfg_imem_ptr) {
		pr_err("zfg_imem_ptr is null,pm_modem_sleep_or_awake_get return\n");
		return 0;
	}
	pr_info("[PM_V] get modemsleep_or_awake %d,\n", zfg_imem_ptr->modemsleep_or_awake);
	return  snprintf(buffer, 8, "%d", zfg_imem_ptr->modemsleep_or_awake);
}

static int pm_modem_phys_link_time_show(char *buffer, struct kernel_param *kp)
{
	if (!zfg_imem_ptr) {
		pr_err("zfg_imem_ptr is null,pm_modem_phys_link_time_get return\n");
		return 0;
	}
	pr_info("[PM_V] get physlinktime %d\n", zfg_imem_ptr->physlinktime);
	return  snprintf(buffer, 8, "%d", zfg_imem_ptr->physlinktime);
}


module_param_call(showmodemsleep, NULL, pm_modem_sleep_time_show,
						&showmodemsleep, 0644);
module_param_call(showmodemawake, NULL, pm_modem_awake_time_show,
						&showmodemawake, 0644);
module_param_call(showmodemsleeporawake, NULL, pm_modem_sleep_or_awake_show,
						&showmodemsleeporawake, 0644);
module_param_call(showphyslinktime, NULL, pm_modem_phys_link_time_show,
						&showphyslinktime, 0644);




/*Interface For Ril Open F3 Log*/
static int zfg_amss_invalid_parameter(void)
{
	if (apSleep_modemAwake_count <= 0)
		return 0;
	else if (apSleep_modemAwake_precent < 500 || apSleep_modemAwake_precent > 1000)
		return 0;
	else if (apSleep_modemAwake_timeThreshold <= 0)
		return 0;
	else
		return 1;
}

static  void  zfg_amss_updateEvent(int modemState)
{
	char *event = NULL;
	char *envp[2];
	const char *name;

	name = modemState?"OPEN":"CLOSE";
	event = kasprintf(GFP_KERNEL, "AMSS_PM_STATE=%s", name);
	envp[0] = event;
	envp[1] = NULL;

	if (msm_cpu_pm_dev == NULL) {
		pr_info("amss, msm_cpu_pm_dev is NULL");
	} else {
		kobject_uevent_env(&msm_cpu_pm_dev->kobj, KOBJ_CHANGE, envp);
	}
}

static int  zfg_amss_needF3log(int apSleep_time_s, int modemAwake_percent)
{
	int invalidParameter = 0;

	invalidParameter = zfg_amss_invalid_parameter();

	if (apSleep_time_s < 0 || (modemAwake_percent < 900 || modemAwake_percent > 1000))
		return 0;
	if (invalidParameter == 0)
		return 0;

	if (zfg_amss_acount > apSleep_modemAwake_count)
		zfg_amss_acount = 0;

	zfg_amss_acount = zfg_amss_acount + 1;
	if ((zfg_amss_acount == apSleep_modemAwake_count) && invalidParameter == 1)
		return 1;
	else
		return 0;
}

#define AMSS_NEVER_ENTER_SLEEP 0x4
#define AMSS_NOW_SLEEP 0x0
#define AMSS_NOW_AWAKE 0x1
#define THRESOLD_FOR_OFFLINE_AWAKE_TIME 100 /*ms*/
#define THRESOLD_FOR_OFFLINE_TIME 5000 /*s*/

static int mEnableRrecordFlag_ZFG = 0;
module_param_named(zfg_enableRecord,
	mEnableRrecordFlag_ZFG, int, S_IRUGO | S_IWUSR | S_IWGRP);

#define RECORED_TOTAL_TIME
static struct timespec time_updated_when_sleep_awake;

/*ZFG_PM: called after exit PowerCollapse from suspend,
which will inform modem app has exit suspend.*/
static void inform_modem_app_enter_suspend(bool entersuspend)
{
	if (zfg_imem_ptr) {
		if (entersuspend) {/*true?sleep:resume*/
			zfg_imem_ptr->app_suspend_state = 0xAA;
		} else {
			zfg_imem_ptr->app_suspend_state = 0;
			pr_info("PM notify app resume\n");
		}
	}
}

/*enter_sleep?record how long app keep sleep for: record how long app awake for.*/
static void record_sleep_awake_time_vendor(bool enter_sleep)
{
	struct timespec ts;
	unsigned time_updated_when_sleep_awake_s;
#ifdef RECORED_TOTAL_TIME
	static bool record_firsttime = true;
	static bool record_firsttime_modem = true;
	static	unsigned time_modem_firsttime_awake_s;
	static	unsigned time_modem_firsttime_sleep_s;
	static	unsigned time_app_total_awake_s;
	static	unsigned time_app_total_sleep_s;
	static unsigned time_lcdon_total_s;
#endif
	unsigned time_updated_when_sleep_awake_ms;
	unsigned time_updated_when_sleep_awake_ms_temp;
	static unsigned amss_sleep_time_ms = 0;
	static unsigned amss_physlink_current_total_time_s;
	static unsigned amss_physlink_last_total_time_s;
	unsigned amss_sleep_time_ms_temp = 0;
	unsigned deta_sleep_ms = 0;
	unsigned deta_awake_ms = 0;
	unsigned deta_physlink_s = 0;
	unsigned amss_awake_last = 0;
	int result_state = 0;

	unsigned amss_current_sleep_or_awake = 0;/*1 never enter sleep,2 sleep,3 awake*/
	static unsigned  amss_current_sleep_or_awake_previous;

	static unsigned amss_awake_time_ms;
	unsigned amss_awake_time_ms_temp = 0;
	bool get_amss_awake_ok = false;

	unsigned percentage_amss_not_sleep_while_app_suspend = 0;
	static bool sleep_success_flag;

	ts = current_kernel_time();

	time_updated_when_sleep_awake_ms_temp =	(unsigned) ((ts.tv_sec - time_updated_when_sleep_awake.tv_sec) *
		MSEC_PER_SEC + ((ts.tv_nsec / NSEC_PER_MSEC) -
		(time_updated_when_sleep_awake.tv_nsec / NSEC_PER_MSEC)));
	time_updated_when_sleep_awake_s = (time_updated_when_sleep_awake_ms_temp/MSEC_PER_SEC);
	time_updated_when_sleep_awake_ms = (time_updated_when_sleep_awake_ms_temp -
		time_updated_when_sleep_awake_s * MSEC_PER_SEC);

	
	if (!enter_sleep) {
		sleep_success_flag = true;
		amss_sleep_time_ms_temp = amss_sleep_time_ms;
		amss_sleep_time_ms = pm_modem_sleep_time_get();
		deta_sleep_ms = amss_sleep_time_ms - amss_sleep_time_ms_temp;

		amss_awake_time_ms_temp = amss_awake_time_ms;
		amss_awake_time_ms  = pm_modem_awake_time_get(&amss_current_sleep_or_awake);
		deta_awake_ms = amss_awake_time_ms - amss_awake_time_ms_temp;

		amss_physlink_current_total_time_s = pm_modem_phys_link_time_get();
		deta_physlink_s = amss_physlink_current_total_time_s - amss_physlink_last_total_time_s;
		amss_physlink_last_total_time_s = amss_physlink_current_total_time_s;

		/*
		amss_current_sleep_or_awake_previous  amss_current_sleep_or_awake
		X 4 ---modem not enter sleep yet
		0 0 ---previous is sleep,curret is sleep,
				modem awake time is updated,get awake deta directly.
		otherwise get modem sleep time.
		if modem is set to offline,print offline in the log
		*/

		if ((amss_current_sleep_or_awake_previous == AMSS_NOW_SLEEP) &&
				(amss_current_sleep_or_awake == AMSS_NOW_SLEEP)) {
			
			if (deta_awake_ms < THRESOLD_FOR_OFFLINE_AWAKE_TIME) {
				if (time_updated_when_sleep_awake_ms_temp > THRESOLD_FOR_OFFLINE_TIME)
					pr_info("[PM_V] offline mode\n");
			}
			get_amss_awake_ok = true;
			amss_awake_last = deta_awake_ms;
		} else if (amss_current_sleep_or_awake == AMSS_NEVER_ENTER_SLEEP) {
			pr_info("[PM_V] modem not enter sleep yet\n");
		}

		if (!get_amss_awake_ok) {
			amss_awake_last = time_updated_when_sleep_awake_ms_temp - deta_sleep_ms;
		}
		percentage_amss_not_sleep_while_app_suspend =
				(amss_awake_last * 1000/(time_updated_when_sleep_awake_ms_temp + 1));

#ifdef RECORED_TOTAL_TIME
		if (!record_firsttime) {
			time_app_total_awake_s += time_updated_when_sleep_awake_s;
			time_lcdon_total_s += screenontimebeforesuspend;
		}
		record_firsttime = false;
#endif
		pr_info("[PM_V] APP wake for %6d.%03d s, lcd on for %5d s %3d %%\n",
			time_updated_when_sleep_awake_s, time_updated_when_sleep_awake_ms,
			(int) screenontimebeforesuspend,
			(int)(screenontimebeforesuspend * 100/(time_updated_when_sleep_awake_s + 1)));
		pr_info("[PM_V] modem wake for %10d ms(%s) %4d %%o,modem sleep for %10d --%d%d\n",
			amss_awake_last, get_amss_awake_ok ? "get_directly " : "from sleep_time",
			percentage_amss_not_sleep_while_app_suspend,
			deta_sleep_ms, amss_current_sleep_or_awake_previous,
			amss_current_sleep_or_awake);/*in case Division by zero, +1*/

		pr_info("[PM_V] modem_phys_link_total_time %4d min %4d s\n",
			amss_physlink_current_total_time_s/60,
			amss_physlink_current_total_time_s%60);
		pr_info("[PM_V] deta_physlink_s %4d min %4d s during app wake\n",
			deta_physlink_s/60,	deta_physlink_s%60);

		time_updated_when_sleep_awake = ts;
		screenontimebeforesuspend = 0;
	} else {
		
		if (!sleep_success_flag) {
			pr_info("[PM_V] app resume due to fail to suspend\n");
			return;
		}
		sleep_success_flag = false;
		amss_sleep_time_ms_temp = amss_sleep_time_ms;
		amss_sleep_time_ms  = pm_modem_sleep_time_get();
		deta_sleep_ms = amss_sleep_time_ms - amss_sleep_time_ms_temp;
		amss_awake_time_ms_temp = amss_awake_time_ms;
		amss_awake_time_ms  = pm_modem_awake_time_get(&amss_current_sleep_or_awake);
		deta_awake_ms = amss_awake_time_ms - amss_awake_time_ms_temp;

		amss_physlink_current_total_time_s = pm_modem_phys_link_time_get();
		deta_physlink_s = amss_physlink_current_total_time_s - amss_physlink_last_total_time_s;
		amss_physlink_last_total_time_s = amss_physlink_current_total_time_s;

		
		if ((amss_current_sleep_or_awake_previous == AMSS_NOW_SLEEP) &&
			(amss_current_sleep_or_awake == AMSS_NOW_SLEEP)) {
			
			if ((deta_awake_ms < THRESOLD_FOR_OFFLINE_AWAKE_TIME)
				&& (time_updated_when_sleep_awake_ms_temp > THRESOLD_FOR_OFFLINE_TIME)) {
				pr_info("[PM_V] offline mode\n");
			}
			get_amss_awake_ok = true;
			amss_awake_last = deta_awake_ms;
		} else if (amss_current_sleep_or_awake == AMSS_NEVER_ENTER_SLEEP) {
			pr_info("[PM_V] modem not enter sleep yet\n");
		}

		if (!get_amss_awake_ok)
			amss_awake_last = time_updated_when_sleep_awake_ms_temp - deta_sleep_ms;

#ifdef RECORED_TOTAL_TIME
		time_app_total_sleep_s += time_updated_when_sleep_awake_s;
		if (record_firsttime_modem) {
			time_modem_firsttime_awake_s = amss_awake_last/1000;
			time_modem_firsttime_sleep_s = amss_sleep_time_ms/1000;
			record_firsttime_modem = false;
		}
		pr_info("[PM_V] modem total sleep: %d s,modem total awake %d s\n",
			(amss_sleep_time_ms/1000 - time_modem_firsttime_sleep_s),
			(amss_awake_time_ms/1000 - time_modem_firsttime_awake_s));

		pr_info("[PM_V] app total sleep: %d s,app total awake: %d s,lcd on total: %d s\n",
			time_app_total_sleep_s, time_app_total_awake_s, time_lcdon_total_s);
#endif

		if (kernel_sleep_count > 10000) {
			kernel_sleep_count = 1;
			pr_info("[PM_V] init again, kernel_sleep_count=%d\n", kernel_sleep_count);
		} else {
			kernel_sleep_count = kernel_sleep_count+1;
			if (kernel_sleep_count%5 == 0)
				pr_info("[PM_V] kernel_sleep_count=%d\n", kernel_sleep_count);
		}

		percentage_amss_not_sleep_while_app_suspend =
				(amss_awake_last * 1000/(time_updated_when_sleep_awake_ms_temp + 1));

		pr_info("[PM_V] APP sleep for %3d.%03d s, modem wake %6d ms,(%s),%3d %%o\n",
			time_updated_when_sleep_awake_s,
			time_updated_when_sleep_awake_ms, amss_awake_last,
			get_amss_awake_ok ? "get_directly " : "from sleep_time",
			percentage_amss_not_sleep_while_app_suspend);
		pr_info("[PM_V] modem_sleep for %3d ---%d%d\n",
			deta_sleep_ms, amss_current_sleep_or_awake_previous,
			amss_current_sleep_or_awake);

		pr_info("[PM_V] PhysLinkTotalTime %4d min %4d, DetaPhyslink %4d min %4d in this time\n",
			amss_physlink_current_total_time_s/60,
			amss_physlink_current_total_time_s%60,
			deta_physlink_s/60,	deta_physlink_s%60);

		time_updated_when_sleep_awake = ts;

		/*Interface For Ril Open F3 Log*/
		result_state = zfg_amss_needF3log(time_updated_when_sleep_awake_s,
								percentage_amss_not_sleep_while_app_suspend);
		zfg_amss_updateEvent(result_state);
	}

	amss_current_sleep_or_awake_previous = amss_current_sleep_or_awake;

}


static int zfg_pm_debug_probe(struct platform_device *pdev)
{
	int ret = 0;

	
	msm_cpu_pm_dev = &pdev->dev;
	record_sleep_awake_time_vendor(true);
	inform_modem_app_enter_suspend(false);
	pr_info("PM notify app resume when %s \n", __func__);
	return ret;
}

static int zfg_pm_debug_suspend(struct device *dev)
{
	if (mEnableRrecordFlag_ZFG == 0) {
		return 0;
	}
	record_sleep_awake_time_vendor(false);
	inform_modem_app_enter_suspend(true);
	return 0;
}

static int zfg_pm_debug_resume(struct device *dev)
{
	if (mEnableRrecordFlag_ZFG == 0) {
		pr_info("[PM_V]: not enable to record zfg_pm_debug_resume vendor!\n");
		return 0;
	}
	record_sleep_awake_time_vendor(true);
	inform_modem_app_enter_suspend(false);
	return 0;
}

static int  zfg_pm_debug_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id zfg_pm_debug_table[] = {
	{.compatible = "zfg_pm_debug_vendor"},
	{},
};

static const struct dev_pm_ops zfg_pm_debug_ops = {
	.suspend	= zfg_pm_debug_suspend,
	.resume = zfg_pm_debug_resume,
};

static struct platform_driver zfg_pm_debug_driver = {
	.probe = zfg_pm_debug_probe,
	.remove	= zfg_pm_debug_remove,
	.driver = {
		.name = "zfg_pm_debug_vendor",
		.owner = THIS_MODULE,
		.pm	= &zfg_pm_debug_ops,
		.of_match_table = zfg_pm_debug_table,
	},
};

int __init zfg_pm_debug_vendor_init(void)
{
	static bool registered;
	struct device_node *np; 

	if (registered)
		return 0;
	registered = true;

	fb_register_client(&lcd_fb_notifier);

	pr_info("%s: msm-vendor-imem-pm-count-time\n", __func__);
	np = of_find_compatible_node(NULL, NULL, "qcom,msm-vendor-imem-pm-count-time");
	if (!np) {
		pr_err("unable to find DT imem msm-imem-pm-count-time node\n");
	} else {
		zfg_imem_ptr = (pm_count_time  *)of_iomap(np, 0);
		if (!zfg_imem_ptr)
			pr_err("unable to map imem golden copyoffset\n");
	}

	return platform_driver_register(&zfg_pm_debug_driver);
}
late_initcall(zfg_pm_debug_vendor_init);

static void __exit zfg_pm_debug_vendor_exit(void)
{
	platform_driver_unregister(&zfg_pm_debug_driver);
}
module_exit(zfg_pm_debug_vendor_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("pm sleep wake time for zfg");
MODULE_ALIAS("platform:zfg_pm_debug_vendor");