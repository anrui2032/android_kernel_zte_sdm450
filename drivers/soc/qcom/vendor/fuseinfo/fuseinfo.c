/*
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <soc/qcom/scm.h>
#include <soc/qcom/smem.h>
#include "linux/stat.h"
#include "fuseinfo.h"

/*
 * Macro Definition
 */
#define FUSEINFO_VERSION  "1.0"
#define FUSEINFO_CLASS_NAME  "fuseinfo"
#define FUSEINFO_DRIVER_NAME "fuseinfo"

/*
 * Global Variables Definition
 */
static fuseinfo_sysdev_attrs_t fuseinfo_sysdev_attrs;
static fuseinfo_sysdev_priv_data_t fuseinfo_sysdev_priv_data;

/*
 * Function Definition
 */
static u64 fuseinfo_read_row(fuseinfo_base_addr_t *pbase,
	u32 row_num, bool use_tz_api)
{
	int rc;
	u64 efuse_bits;
	struct scm_desc desc = {0};
	struct efuse_read_req {
		u32 row_address;
		int addr_type;
	} req;

	struct efuse_read_rsp {
		u32 row_data[2];
		u32 status;
	} rsp;

	if (!use_tz_api) {
		efuse_bits = readq_relaxed(pbase->va
		+ row_num * BYTES_PER_FUSE_ROW);

		return efuse_bits;
	}

	desc.args[0] = req.row_address = pbase->pa + row_num * BYTES_PER_FUSE_ROW;
	desc.args[1] = req.addr_type = 0;
	desc.arginfo = SCM_ARGS(2);

	efuse_bits = 0;

	if (!is_scm_armv8()) {
		rc = scm_call(SCM_SVC_FUSE, SCM_FUSE_READ, &req, sizeof(req), &rsp, sizeof(rsp));
	} else {
		rc = scm_call2(SCM_SIP_FNID(SCM_SVC_FUSE, SCM_FUSE_READ), &desc);
		rsp.row_data[0] = desc.ret[0];
		rsp.row_data[1] = desc.ret[1];
		rsp.status = desc.ret[2];
	}

	if (rc) {
		pr_err("read row %d failed, err code = %d\n", row_num, rc);
	} else {
		efuse_bits = ((u64)(rsp.row_data[1]) << 32) + (u64)rsp.row_data[0];
	}

	return efuse_bits;
}

static u32 fuseinfo_rollback_count_set_bits(u64 input_val)
{
	u32 count = 0;

	for (; input_val != 0; count++) {
		input_val &= input_val - 1;
	}

	return count;
}

/* show_fuse_secboot_enabled
 * show whether the secboot function is enabled or not
 * return: 1-- enabled, 0 -- disabled
*/
static ssize_t show_fuse_secboot_enabled(struct device *dev, struct device_attribute *attr, char *buf)
{
	u64 efuse_data = 0;
	fuseinfo_base_addr_t *pbase = &fuseinfo_sysdev_priv_data.efuse_base_addr;

	efuse_data = fuseinfo_read_row(pbase, QFPROM_SECBOOT_EN_ROW_NUM, 0);
	dev_info(dev, "%s: secboot status = %16llx\n", __func__, efuse_data);
	fuseinfo_sysdev_attrs.secboot_en = ((efuse_data & QFPROM_SECBOOT_EN_MASK) == QFPROM_SECBOOT_EN_MASK) ? 1 : 0;

	return snprintf(buf, PAGE_SIZE, "%d\n", fuseinfo_sysdev_attrs.secboot_en);
}

static DEVICE_ATTR(secboot_en, S_IRUGO | S_IRUSR, show_fuse_secboot_enabled, NULL);

/* show_fuse_antirollback_enabled
 * show whether the antirollback func is enabled or not
 * return: 1-- enabled, 0 -- disabled
*/
static ssize_t show_fuse_antirollback_enabled(struct device *dev, struct device_attribute *attr, char *buf)
{
	u64 efuse_data = 0;
	uint32_t secboot_en = 0;
	fuseinfo_base_addr_t *pbase = &fuseinfo_sysdev_priv_data.efuse_base_addr;

	/*1、is secboot enabled*/
	efuse_data = fuseinfo_read_row(pbase, QFPROM_SECBOOT_EN_ROW_NUM, 0);
	dev_info(dev, "%s: secboot status = %16llx\n", __func__, efuse_data);

	secboot_en = ((efuse_data & QFPROM_SECBOOT_EN_MASK) == QFPROM_SECBOOT_EN_MASK) ? 1 : 0;
	if (!secboot_en) {
		fuseinfo_sysdev_attrs.antirollback_en = 0;
		goto exit;
	}

	/*2、is apps anti roll back enabled?*/
	efuse_data = fuseinfo_read_row(pbase, QFPROM_ANTIROLLBACK_EN_ROW_NUM, 0);
	dev_info(dev, "%s: antirollback_en status = %16llx\n", __func__, efuse_data);

	fuseinfo_sysdev_attrs.antirollback_en =
		((efuse_data & QFPROM_ANTIROLLBACK_EN_MASK) == QFPROM_ANTIROLLBACK_EN_MASK) ? 1 : 0;

exit:
	return snprintf(buf, PAGE_SIZE, "%d\n", fuseinfo_sysdev_attrs.antirollback_en);
}

static DEVICE_ATTR(antirollback_en, S_IRUGO | S_IRUSR, show_fuse_antirollback_enabled, NULL);

/*
 *show_fuse_apps_antirollback_version: return the antirollback version
*/
static ssize_t show_fuse_apps_antirollback_version(struct device *dev, struct device_attribute *attr, char *buf)
{
	u64 efuse_data = 0;
	fuseinfo_base_addr_t *pbase = &fuseinfo_sysdev_priv_data.efuse_base_addr;

	efuse_data = fuseinfo_read_row(pbase, QFPROM_ANTIROLLBACK_VAL1_ROW_NUM, 0);
	dev_info(dev, "%s: anti rollback value#1 = %16llx\n", __func__, efuse_data);
	fuseinfo_sysdev_attrs.apps_antirollback_ver =
		fuseinfo_rollback_count_set_bits(efuse_data & QFPROM_APPS_ANTIROLLBACK_VAL1_MASK);

	efuse_data = fuseinfo_read_row(pbase, QFPROM_ANTIROLLBACK_VAL2_ROW_NUM, 0);
	dev_info(dev, "%s: anti rollback value#2 = %16llx\n", __func__, efuse_data);
	fuseinfo_sysdev_attrs.apps_antirollback_ver +=
		fuseinfo_rollback_count_set_bits(efuse_data & QFPROM_APPS_ANTIROLLBACK_VAL2_MASK);

	return snprintf(buf, PAGE_SIZE, "%d\n", fuseinfo_sysdev_attrs.apps_antirollback_ver);
}

static DEVICE_ATTR(apps_antirollback_ver, S_IRUGO | S_IRUSR, show_fuse_apps_antirollback_version, NULL);

static struct attribute  *fuseinfo_attrs[] = {
	&dev_attr_secboot_en.attr,
	&dev_attr_antirollback_en.attr,
	&dev_attr_apps_antirollback_ver.attr,
	NULL,
};

static struct attribute_group fuseinfo_attr_grp = {
	.name = "fuseinfo",
	.attrs = fuseinfo_attrs
};


static int32_t fuseinfo_addr_map(phys_addr_t pa, size_t len)
{
	fuseinfo_sysdev_priv_data.efuse_base_addr.pa = pa;

	fuseinfo_sysdev_priv_data.efuse_base_addr.va = ioremap(fuseinfo_sysdev_priv_data.efuse_base_addr.pa, len);

	if (fuseinfo_sysdev_priv_data.efuse_base_addr.va == NULL) {
		pr_err("%s: failed to map memory\n", __func__);
		return -ENOMEM;
	}
	return 0;
}

static int32_t __init fuseinfo_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret = 0;

	dev_info(dev, "%s: e\n", __func__);

	ret = fuseinfo_addr_map((phys_addr_t)QFPROM_EFUSE_BASE_ADDR, QFPROM_EFUSE_MAX_LEN);
	if (ret) {
		dev_err(dev, "cannot map fuse phyaddr  err: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&dev->kobj, &fuseinfo_attr_grp);
	if (ret) {
		dev_err(dev, "cannot create sysfs group err: %d\n", ret);
		return ret;
	}

	dev_info(dev, "%s: x\n", __func__);

	return 0;
}

static int32_t fuseinfo_remove(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s: e\n", __func__);
	dev_info(&pdev->dev, "%s: x\n", __func__);

	return 0;
}

static struct platform_device fuseinfo_dev = {
	.name = FUSEINFO_DRIVER_NAME,
	.id   = -1,
};


static struct platform_driver fuseinfo_driver = {
	.remove = fuseinfo_remove,
	.driver = {
		.name = FUSEINFO_DRIVER_NAME,
	},
};

static int32_t __init fuseinfo_init(void)
{
	int ret;

	pr_info("%s: e\n", __func__);

	ret = platform_device_register(&fuseinfo_dev);
	if (ret) {
		pr_err("platform_device_register error: %d\n", ret);
		return -ENODEV;
	}

	ret = platform_driver_probe(&fuseinfo_driver, fuseinfo_probe);
	if (ret) {
		pr_err("platform_driver_probe error: %d\n", ret);
		return ret;
	}

	pr_info("%s: x\n", __func__);

	return 0;
}

static void __exit fuseinfo_exit(void)
{
	platform_driver_unregister(&fuseinfo_driver);
	platform_device_unregister(&fuseinfo_dev);
}

module_init(fuseinfo_init);
module_exit(fuseinfo_exit);

MODULE_DESCRIPTION("Fuse information Driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");
