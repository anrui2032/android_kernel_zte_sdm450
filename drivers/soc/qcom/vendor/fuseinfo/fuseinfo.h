#ifndef FUSEINFO_H
#define FUSEINFO_H
#define BYTES_PER_FUSE_ROW  0x08
#if defined(CONFIG_ARCH_MSM8909)
#define QFPROM_EFUSE_BASE_ADDR  0x00058000
#define QFPROM_EFUSE_MAX_LEN  0x1000
#define QFPROM_SECBOOT_EN_ROW_NUM  19
#define QFPROM_SECBOOT_EN_MASK  0x00303030LL
#define QFPROM_ANTIROLLBACK_EN_ROW_NUM 6
#define QFPROM_ANTIROLLBACK_EN_MASK 0x78000000000000LL
#define QFPROM_ANTIROLLBACK_VAL1_ROW_NUM  3
#define QFPROM_APPS_ANTIROLLBACK_VAL1_MASK 0xFFFC000000000000LL
#define QFPROM_ANTIROLLBACK_VAL2_ROW_NUM  4
#define QFPROM_APPS_ANTIROLLBACK_VAL2_MASK 0x000FFFFFFFFFLL
#elif defined(CONFIG_ARCH_MSM8937)
#define QFPROM_EFUSE_BASE_ADDR  0x000A0000
#define QFPROM_EFUSE_MAX_LEN  0x1000
#define QFPROM_SECBOOT_EN_ROW_NUM  58
#define QFPROM_SECBOOT_EN_MASK  0x00303030LL
#define QFPROM_ANTIROLLBACK_EN_ROW_NUM 42
#define QFPROM_ANTIROLLBACK_EN_MASK 0x0078000000000000LL
#define QFPROM_ANTIROLLBACK_VAL1_ROW_NUM  39
#define QFPROM_APPS_ANTIROLLBACK_VAL1_MASK 0xFFFC000000000000LL
#define QFPROM_ANTIROLLBACK_VAL2_ROW_NUM  40
#define QFPROM_APPS_ANTIROLLBACK_VAL2_MASK 0x0000000FFFFFFFFFLL
#else
#error "fuse registor addrss and mask must be defined"
#endif

/*
 * Type Definition
 */
typedef struct {
	/*secboot enabled flag: 1 -- enabled, 0 -- disabled*/
	uint32_t secboot_en;
	/*rollback enabled flag: 1 -- enabled, 0 -- disabled*/
	uint32_t antirollback_en;
	/*anti rollback value of appsbl readed from fuse region*/
	uint32_t apps_antirollback_ver;
} fuseinfo_sysdev_attrs_t;

typedef struct {
	phys_addr_t pa;
	void __iomem *va;
} fuseinfo_base_addr_t;

typedef struct  {
	fuseinfo_base_addr_t efuse_base_addr;
} fuseinfo_sysdev_priv_data_t;
#endif
