// SPDX-License-Identifier: GPL-2.0

/* Core kernel */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <generated/utsrelease.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/printk.h>
#include <linux/sched.h>

/* Concurrency & timing */
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/workqueue.h>

/* Power management */
#include <linux/reboot.h>
#include <linux/freezer.h>
#include <linux/suspend.h>
#include <linux/pm_wakeup.h>

/* Interfaces */
#include <linux/fs.h>
#include <linux/path.h>
#include <linux/mount.h>
#include <linux/fs_struct.h>
#include <linux/blkdev.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>

/* ACPI */
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>

static struct sys_off_handler *sysoff_hook_h = NULL;

static u8   param_s5divert_enabled = 1;
static bool param_s5divert_poweroff = false;
static bool param_s5divert_reboot = false;
static bool param_s5divert_stroff = false;

static bool lid_found = false;

static struct proc_dir_entry *proc_dir_s5divert = NULL;
static struct proc_dir_entry *proc_file_s5divert_enabled = NULL;
static struct proc_dir_entry *proc_file_s5divert_poweroff = NULL;
static struct proc_dir_entry *proc_file_s5divert_reboot = NULL;
static struct proc_dir_entry *proc_file_s5divert_stroff = NULL;

static struct kobject *sysfs_dir_s5divert = NULL;

//static struct workqueue_struct *wq = NULL;
//static void wq_worker(struct work_struct *work);
//static DECLARE_WORK(wq_work, wq_worker);

static void system_poweroff(void);
static void system_reboot(bool hard);

static int acpi_call_dsw_or_psw(acpi_handle handle, u8 enable, u8 sstate, u8 dstate)
{
    if (acpi_has_method(handle, "_DSW")) {
        union acpi_object in[3] = {
            { .type = ACPI_TYPE_INTEGER, .integer.value = enable },  // 1 = enable
            { .type = ACPI_TYPE_INTEGER, .integer.value = sstate  }, // e.g. ACPI_STATE_S4
            { .type = ACPI_TYPE_INTEGER, .integer.value = dstate  }, // e.g. D3hot
        };
        struct acpi_object_list args = { .count = 3, .pointer = in };
	    pr_info("s5divert: Calling _DSW\n");
		msleep(5000);
        return ACPI_SUCCESS(acpi_evaluate_object(handle, "_DSW", &args, NULL)) ? 0 : -EIO;
    }
    pr_info("s5divert: Calling _PSW\n");
    return ACPI_SUCCESS(acpi_execute_simple_method(handle, "_PSW", enable)) ? 0 : -EIO;
}

static const struct acpi_device_id lid_ids[] = {
    { "PNP0C0D", 0 },     /* ACPI Lid device */
    { }
};

static bool is_lid_device(struct acpi_device *adev)
{
    return acpi_match_device_ids(adev, lid_ids) == 0;
}

static acpi_status enable_wake_gpe_cb(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	struct acpi_device *adev = acpi_fetch_acpi_dev(handle);
	if (!adev) return AE_OK;

	if (adev->wakeup.flags.valid && device_may_wakeup(&adev->dev)) {
		acpi_call_dsw_or_psw(handle, 1, ACPI_STATE_S4, ACPI_STATE_D3_HOT);
		acpi_set_gpe_wake_mask(adev->wakeup.gpe_device, adev->wakeup.gpe_number, ACPI_GPE_ENABLE);
		if (is_lid_device(adev)) {
			pr_debug("s5divert: Wakeup from lid enabled\n");
			lid_found = true;
		}
	} else {
		if (is_lid_device(adev)) {
			pr_debug("s5divert: Wakeup from lid not enabled\n");
			lid_found = true;
		}
	}

	return AE_OK;
}

static void acpi_enable_wakeup_devices(void)
{
	lid_found = false;
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX, enable_wake_gpe_cb, NULL, NULL, NULL);
	if(!lid_found) pr_debug("s5divert: No lid wakeup source found\n");
}

static inline void fs_sync(void)
{
    struct path root;

	pr_info("s5divert: Syncing discs... (expect a kernel waring from sync_filesystem)\n");
	get_fs_root(current->fs, &root);
    if (root.mnt && root.mnt->mnt_sb) {
        struct super_block *sb = root.mnt->mnt_sb;
        struct block_device *bdev = sb->s_bdev;
        sync_filesystem(sb);	// this will throw an ugly warning. SIGH.
        if (bdev) {
			sync_blockdev(bdev);
            blkdev_issue_flush(bdev);
		}
        msleep(100);
    }
    path_put(&root);
}

static int enter_s4_noimage(void)
{
	acpi_status st;

	pr_info("s5divert: Entering ACPI S4 without hibernation...\n");
    acpi_execute_simple_method(NULL, "\\_TTS", ACPI_STATE_S4);
    acpi_enable_wakeup_devices();
	// acpi_execute_simple_method(NULL, "\\_PTS", ACPI_STATE_S4); // included in acpi_enter_sleep_state_prep
	st = acpi_enter_sleep_state_prep(ACPI_STATE_S4);
	if (ACPI_FAILURE(st)) {
		pr_err("s5divert: Unable to enter ACPI S4, proceeding to ACPI S5\n");
		return -EOPNOTSUPP;
	}
	msleep(300);
    // acpi_execute_simple_method(NULL, "\\_GTS", ACPI_STATE_S4); // deprecated
	local_irq_disable();
	st = acpi_enter_sleep_state(ACPI_STATE_S4);
	local_irq_enable();
	acpi_leave_sleep_state_prep(ACPI_STATE_S4);
    // acpi_execute_simple_method(NULL, "\\_BFS", ACPI_STATE_S4); // deprecated
	acpi_leave_sleep_state(ACPI_STATE_S4);
    // acpi_execute_simple_method(NULL, "\\_WAK", ACPI_STATE_S4); // included in acpi_leave_sleep_state

	if (ACPI_SUCCESS(st)) {
		pr_err("s5divert: ACPI S4 returned unexpectedly\n");
	} else {
		pr_err("s5divert: Unable to send system into ACPI S4\n");
	}
	return -EIO;
}

static int enter_s3_noreturn(void)
{
	acpi_status st;

	pr_info("s5divert: Entering ACPI S3 without return point...\n");
    acpi_execute_simple_method(NULL, "\\_TTS", ACPI_STATE_S3);
    acpi_enable_wakeup_devices();
	// acpi_execute_simple_method(NULL, "\\_PTS", ACPI_STATE_S3); // included in acpi_enter_sleep_state_prep
	st = acpi_enter_sleep_state_prep(ACPI_STATE_S3);
	if (ACPI_FAILURE(st)) {
		pr_err("s5divert: Unable to enter ACPI S3, proceeding to ACPI S5\n");
		return -EOPNOTSUPP;
	}
	msleep(300);
    // acpi_execute_simple_method(NULL, "\\_GTS", ACPI_STATE_S3); // deprecated
	local_irq_disable();
	st = acpi_enter_sleep_state(ACPI_STATE_S3);
	local_irq_enable();
	acpi_leave_sleep_state_prep(ACPI_STATE_S3);
    // acpi_execute_simple_method(NULL, "\\_BFS", ACPI_STATE_S3); // deprecated
	acpi_leave_sleep_state(ACPI_STATE_S3);
    // acpi_execute_simple_method(NULL, "\\_WAK", ACPI_STATE_S3); // included in acpi_leave_sleep_state

	if (ACPI_SUCCESS(st)) {
		pr_err("s5divert: ACPI S3 returned unexpectedly\n");
	} else {
		pr_err("s5divert: Unable to send system into ACPI S3\n");
	}
	return -EIO;
}

static int enter_s3_reboot(void)
{
	struct wakeup_source* ws;
	int rc;

	//if (WARN_ON_ONCE(irqs_disabled())) return -EINVAL;

	pr_info("s5divert: Entering ACPI S3 just to reboot right after resuming...\n");

	might_sleep(); set_freezable();
	msleep(300);

	ws = wakeup_source_register(NULL, "enter_s3_guard");
	if (!ws) return -ENOMEM;

	__pm_stay_awake(ws);
	rc = pm_suspend(PM_SUSPEND_MEM);

	if (rc == 0) {
		pr_info("s5divert: Resumed from ACPI S3. Rebooting...\n");
		system_reboot(false);
		system_reboot(true);
	} else {
		pr_err("s5divert: Failed to enter ACPI S3 system state: %pe\n", ERR_PTR(rc));
	}

	__pm_relax(ws);
	wakeup_source_unregister(ws);

	return rc;
}

static void system_poweroff(void)
{
    kernel_power_off();
    kernel_halt();
}

static void system_sync_poweroff(void)
{
	// Call fs_sync, iff sysoff_hook_cb is not installed.
	// If so then it will be called later anyway.
	if (sysoff_hook_h==NULL || IS_ERR(sysoff_hook_h)) fs_sync();
	system_poweroff();
}

static void system_reboot(bool hard)
{
    if (hard)	emergency_restart();
    else 		kernel_restart(NULL);
}

//static void wq_worker(struct work_struct *work) {
	// msleep(300);
	// queue_work(wq, &wq_work);
//}

static int sysoff_hook_cb(struct sys_off_data *data)
{
	if(param_s5divert_enabled>0) fs_sync();

	switch (param_s5divert_enabled) {
		case 1:
		pr_warn("s5divert: Diverting ACPI S5 to S4\n");
		param_s5divert_enabled = 0;
		(void)enter_s4_noimage();
		break;

		case 2:
		pr_warn("s5divert: Diverting ACPI S5 to S3\n");
		param_s5divert_enabled = 0;
		(void)enter_s3_noreturn();
		break;

		case 3:
		pr_warn("s5divert: Diverting ACPI S5 to system reboot\n");
		param_s5divert_enabled = 0;
		msleep(300);
		system_reboot(false);
		system_reboot(true);
		break;

		default:
		break;
	}
	return NOTIFY_DONE;
}

static int sysoff_hook_register(void)
{
	if (sysoff_hook_h!=NULL && !IS_ERR(sysoff_hook_h)) return 0;
	switch (param_s5divert_enabled) {
		case 1:
		pr_info("s5divert: ACPI S5 will be diverted to S4\n");
		// Run handler after all preparations have been made, but before the system actually starts powering down.
		sysoff_hook_h = register_sys_off_handler(SYS_OFF_MODE_POWER_OFF_PREPARE, SYS_OFF_PRIO_PLATFORM - 1, sysoff_hook_cb, NULL);
		break;

		case 2:
		pr_info("s5divert: ACPI S5 will be diverted to S3\n");
		// Run handler after all preparations have been made, but before the system actually starts powering down.
		sysoff_hook_h = register_sys_off_handler(SYS_OFF_MODE_POWER_OFF_PREPARE, SYS_OFF_PRIO_PLATFORM - 1, sysoff_hook_cb, NULL);
		break;

		case 3:
		pr_info("s5divert: ACPI S5 will be diverted to system reboot\n");
		// Run handler after all preparations have been made, but before the system actually starts powering down.
		sysoff_hook_h = register_sys_off_handler(SYS_OFF_MODE_POWER_OFF_PREPARE, SYS_OFF_PRIO_PLATFORM - 1, sysoff_hook_cb, NULL);
		break;

		default:
		sysoff_hook_h = NULL;
		break;
	}
	if (IS_ERR(sysoff_hook_h)) {
		pr_err("s5divert: ACPI S5 diversion failed to register\n");
		return PTR_ERR(sysoff_hook_h);
	}
	pr_info("s5divert: ACPI S5 diversion enabled\n");
	return 0;
}

static int sysoff_hook_unregister(void)
{
	if (sysoff_hook_h!=NULL && !IS_ERR(sysoff_hook_h)) {
		unregister_sys_off_handler(sysoff_hook_h);
		pr_info("s5divert: ACPI S5 diversion disabled\n");
	}
	sysoff_hook_h = NULL;
	return 0;
}

static bool sysoff_hook_already_applied(void)
{
	if (param_s5divert_enabled==0) {
		if (sysoff_hook_h==NULL || !IS_ERR(sysoff_hook_h)) return true;
	} else {
		if (sysoff_hook_h!=NULL && !IS_ERR(sysoff_hook_h)) return true;
	}
	return false;
}

static int sysoff_hook_apply(void)
{
	sysoff_hook_unregister();
	if (param_s5divert_enabled != 0) sysoff_hook_register();
	return 0;
}

static ssize_t proc_s5divert_enabled_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[8];
	int len;

	len = scnprintf(kbuf, sizeof(kbuf), "%u\n", param_s5divert_enabled);
	if (*ppos >= len)return 0;
	if (count > len - *ppos) count = len - *ppos;
	if (copy_to_user(ubuf, kbuf + *ppos, count)) return -EFAULT;
	*ppos += count;

	return count;
}

static ssize_t proc_s5divert_enabled_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[32];
	size_t n = min(count, sizeof(kbuf) - 1);
	u8 val;
	int ret;

	if (copy_from_user(kbuf, ubuf, n)) return -EFAULT;
	kbuf[n] = '\0';

	ret = kstrtou8(kbuf, 0, &val);
	if (ret) return ret;
	if (val>3) return -ERANGE;

	param_s5divert_enabled=val;
	sysoff_hook_apply();
	return count;
}

static const struct proc_ops proc_s5divert_enabled_ops = {
	.proc_lseek	= noop_llseek,
	.proc_read = proc_s5divert_enabled_read,
	.proc_write = proc_s5divert_enabled_write,
};

static ssize_t proc_s5divert_poweroff_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[8];
	int len;

	len = scnprintf(kbuf, sizeof(kbuf), "%d\n", param_s5divert_poweroff?1:0);
	if (*ppos >= len) return 0;
	if (count > len - *ppos) count = len - *ppos;
	if (copy_to_user(ubuf, kbuf + *ppos, count)) return -EFAULT;
	*ppos += count;

	return count;
}

static ssize_t proc_s5divert_poweroff_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[32];
	size_t n = min(count, sizeof(kbuf) - 1);
	bool val;
	int ret;

	if (copy_from_user(kbuf, ubuf, n)) return -EFAULT;
	kbuf[n] = '\0';

	ret = kstrtobool(kbuf, &val);
	if (ret) return ret;

	param_s5divert_poweroff=val?true:false;
	if (val) system_sync_poweroff();
	return count;
}

static const struct proc_ops proc_s5divert_poweroff_ops = {
	.proc_lseek	= noop_llseek,
	.proc_read = proc_s5divert_poweroff_read,
	.proc_write = proc_s5divert_poweroff_write,
};

static ssize_t proc_s5divert_reboot_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[8];
	int len;

	len = scnprintf(kbuf, sizeof(kbuf), "%d\n", param_s5divert_reboot?1:0);
	if (*ppos >= len) return 0;
	if (count > len - *ppos) count = len - *ppos;
	if (copy_to_user(ubuf, kbuf + *ppos, count)) return -EFAULT;
	*ppos += count;

	return count;
}

static ssize_t proc_s5divert_reboot_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[32];
	size_t n = min(count, sizeof(kbuf) - 1);
	bool val;
	int ret;

	if (copy_from_user(kbuf, ubuf, n)) return -EFAULT;
	kbuf[n] = '\0';

	ret = kstrtobool(kbuf, &val);
	if (ret) return ret;

	param_s5divert_reboot = val?true:false;
	if (val) system_reboot(false);
	return count;
}

static const struct proc_ops proc_s5divert_reboot_ops = {
	.proc_lseek	= noop_llseek,
	.proc_read = proc_s5divert_reboot_read,
	.proc_write = proc_s5divert_reboot_write,
};

static ssize_t proc_s5divert_stroff_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[8];
	int len;

	len = scnprintf(kbuf, sizeof(kbuf), "%d\n", param_s5divert_stroff?1:0);
	if (*ppos >= len) return 0;
	if (count > len - *ppos) count = len - *ppos;
	if (copy_to_user(ubuf, kbuf + *ppos, count)) return -EFAULT;
	*ppos += count;

	return count;
}

static ssize_t proc_s5divert_stroff_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
	char kbuf[32];
	size_t n = min(count, sizeof(kbuf) - 1);
	bool val;
	int ret;

	if (copy_from_user(kbuf, ubuf, n)) return -EFAULT;
	kbuf[n] = '\0';

	ret = kstrtobool(kbuf, &val);
	if (ret) return ret;

	param_s5divert_stroff = val?true:false;
	if (val) enter_s3_reboot();
	return count;
}

static const struct proc_ops proc_s5divert_stroff_ops = {
	.proc_lseek	= noop_llseek,
	.proc_read = proc_s5divert_stroff_read,
	.proc_write = proc_s5divert_stroff_write,
};


static int procfs_register(void)
{
	proc_dir_s5divert = proc_mkdir("s5divert", NULL);
	if (proc_dir_s5divert) {
		proc_file_s5divert_enabled = proc_create("enabled", 0664, proc_dir_s5divert, &proc_s5divert_enabled_ops);
		proc_file_s5divert_poweroff = proc_create("poweroff", 0220, proc_dir_s5divert, &proc_s5divert_poweroff_ops);
		proc_file_s5divert_reboot = proc_create("reboot", 0220, proc_dir_s5divert, &proc_s5divert_reboot_ops);
		proc_file_s5divert_stroff = proc_create("stroff", 0220, proc_dir_s5divert, &proc_s5divert_stroff_ops);
	} else {
		proc_file_s5divert_enabled = NULL;
		proc_file_s5divert_poweroff = NULL;
		proc_file_s5divert_reboot = NULL;
		proc_file_s5divert_stroff = NULL;
	}
	return 0;
}

static int procfs_unregister(void)
{
	if (proc_dir_s5divert) {
		if (proc_file_s5divert_enabled) { remove_proc_entry("enabled", proc_dir_s5divert); proc_file_s5divert_enabled = NULL; }
		if (proc_file_s5divert_poweroff) { remove_proc_entry("poweroff", proc_dir_s5divert); proc_file_s5divert_poweroff = NULL; }
		if (proc_file_s5divert_reboot) { remove_proc_entry("reboot", proc_dir_s5divert); proc_file_s5divert_reboot = NULL; }
		if (proc_file_s5divert_stroff) { remove_proc_entry("stroff", proc_dir_s5divert); proc_file_s5divert_stroff = NULL; }
		remove_proc_entry("s5divert", NULL); proc_dir_s5divert = 0;
	}
	return 0;
}

static ssize_t sysfs_s5divert_enabled_read(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%u\n", param_s5divert_enabled);
}

static ssize_t sysfs_s5divert_enabled_write(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	u8 v;
	int ret = kstrtou8(buf, 0, &v);
	if (ret) return ret;
	if (v>3) return -ERANGE;
	param_s5divert_enabled = v;
	sysoff_hook_apply();
	return count;
}

static struct kobj_attribute sysfs_s5divert_enabled_attr = __ATTR(enabled, 0664, sysfs_s5divert_enabled_read, sysfs_s5divert_enabled_write);

static ssize_t sysfs_s5divert_poweroff_read(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", param_s5divert_poweroff?1:0);
}

static ssize_t sysfs_s5divert_poweroff_write(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	bool b;
	int ret = kstrtobool(buf, &b);
	if (ret) return ret;
	param_s5divert_poweroff = b?true:false;
	if (b) system_sync_poweroff();
	return count;
}

static struct kobj_attribute sysfs_s5divert_poweroff_attr = __ATTR(poweroff, 0220, sysfs_s5divert_poweroff_read, sysfs_s5divert_poweroff_write);

static ssize_t sysfs_s5divert_reboot_read(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", param_s5divert_reboot?1:0);
}

static ssize_t sysfs_s5divert_reboot_write(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	bool b;
	int ret = kstrtobool(buf, &b);
	if (ret) return ret;
	param_s5divert_reboot = b?true:false;
	if (b) system_reboot(false);
	return count;
}

static struct kobj_attribute sysfs_s5divert_reboot_attr = __ATTR(reboot, 0220, sysfs_s5divert_reboot_read, sysfs_s5divert_reboot_write);

static ssize_t sysfs_s5divert_stroff_read(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", param_s5divert_stroff?1:0);
}

static ssize_t sysfs_s5divert_stroff_write(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	bool b;
	int ret = kstrtobool(buf, &b);
	if (ret) return ret;
	param_s5divert_stroff = b?true:false;
	if (b) enter_s3_reboot();
	return count;
}

static struct kobj_attribute sysfs_s5divert_stroff_attr = __ATTR(stroff, 0220, sysfs_s5divert_stroff_read, sysfs_s5divert_stroff_write);

static int sysfs_register(void)
{
	int ret;
	sysfs_dir_s5divert = kobject_create_and_add("s5divert", kernel_kobj);
	if (sysfs_dir_s5divert) {
		ret = sysfs_create_file(sysfs_dir_s5divert, &sysfs_s5divert_enabled_attr.attr);
		ret = sysfs_create_file(sysfs_dir_s5divert, &sysfs_s5divert_poweroff_attr.attr);
		ret = sysfs_create_file(sysfs_dir_s5divert, &sysfs_s5divert_reboot_attr.attr);
		ret = sysfs_create_file(sysfs_dir_s5divert, &sysfs_s5divert_stroff_attr.attr);
	}
	return 0;
}

static int sysfs_unregister(void)
{
	if (sysfs_dir_s5divert) {
			sysfs_remove_file(sysfs_dir_s5divert, &sysfs_s5divert_stroff_attr.attr);
			sysfs_remove_file(sysfs_dir_s5divert, &sysfs_s5divert_reboot_attr.attr);
			sysfs_remove_file(sysfs_dir_s5divert, &sysfs_s5divert_poweroff_attr.attr);
			sysfs_remove_file(sysfs_dir_s5divert, &sysfs_s5divert_enabled_attr.attr);
			kobject_put(sysfs_dir_s5divert);
			sysfs_dir_s5divert = NULL;
	}
	return 0;
}

static int param_s5divert_enabled_set(const char *val, const struct kernel_param *kp)
{
	u8 v;
	int ret = kstrtou8(val, 0, &v);
	if (ret) return ret;
	if (v>3) return -ERANGE;
	param_s5divert_enabled = v;
	*(u8 *)kp->arg = v;
	if(param_s5divert_enabled == 2) enter_s3_reboot();
	sysoff_hook_apply();
	return 0;
}

static int param_s5divert_enabled_get(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%u\n", param_s5divert_enabled);
}

static const struct kernel_param_ops param_s5divert_enabled_ops = {
	.set = param_s5divert_enabled_set,
	.get = param_s5divert_enabled_get,
};

static int param_s5divert_poweroff_set(const char *val, const struct kernel_param *kp)
{
	bool b;
	int ret = kstrtobool(val, &b);
	if (ret) return ret;
	*(bool *)kp->arg = b;
	if (b) system_sync_poweroff();
	return 0;
}

static int param_s5divert_poweroff_get(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%d\n", param_s5divert_poweroff ? 1 : 0);
}

static const struct kernel_param_ops param_s5divert_poweroff_ops = {
	.set = param_s5divert_poweroff_set,
	.get = param_s5divert_poweroff_get,
};

static int param_s5divert_reboot_set(const char *val, const struct kernel_param *kp)
{
	bool b;
	int ret = kstrtobool(val, &b);
	if (ret) return ret;
	*(bool *)kp->arg = b;
	if (b) system_reboot(false);
	return 0;
}

static int param_s5divert_reboot_get(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%d\n", param_s5divert_reboot ? 1 : 0);
}

static const struct kernel_param_ops param_s5divert_reboot_ops = {
	.set = param_s5divert_reboot_set,
	.get = param_s5divert_reboot_get,
};

static int param_s5divert_stroff_set(const char *val, const struct kernel_param *kp)
{
	bool b;
	int ret = kstrtobool(val, &b);
	if (ret) return ret;
	*(bool *)kp->arg = b;
	if (b) enter_s3_reboot();
	return 0;
}

static int param_s5divert_stroff_get(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%d\n", param_s5divert_stroff ? 1 : 0);
}

static const struct kernel_param_ops param_s5divert_stroff_ops = {
	.set = param_s5divert_stroff_set,
	.get = param_s5divert_stroff_get,
};

static int __init s5divert_init(void)
{
	//wq = alloc_workqueue("s5divert_wq", WQ_UNBOUND | WQ_HIGHPRI | WQ_FREEZABLE, 1);
	//if (!wq) return -ENOMEM;
	//INIT_WORK(&wq_work, wq_worker);

	procfs_register();
	sysfs_register();
	if (!sysoff_hook_already_applied()) sysoff_hook_apply();
	pr_info("s5divert: loaded (kernel %s)\n", UTS_RELEASE);
	return 0;
}

static void __exit s5divert_exit(void)
{
	//destroy_workqueue(wq); wq = NULL;

	sysfs_unregister();
	procfs_unregister();
	sysoff_hook_unregister();
	pr_info("s5divert: unloaded\n");
}

MODULE_VERSION("1.0.0");
MODULE_DESCRIPTION("Divert system power off from ACPI S5 to S4, S3, or system reboot");
MODULE_AUTHOR("rbm78bln");
MODULE_LICENSE("GPL");

MODULE_PARM_DESC(enabled, " Enable/disable diversion of ACPI S5 to either\n"
	"                   0: diversion disabled\n"
	"                   1: ACPI state S4 (without saving) [default]\n"
	"                   2: ACPI state S3 (without return vector)\n"
	"                   3: ACPI state S0 (reboot)");
MODULE_PARM_DESC(poweroff, " Instantly power off the system. Default: 0");
MODULE_PARM_DESC(reboot, " Instantly reboot the system. Default: 0");
MODULE_PARM_DESC(stroff, " Instantly enter ACPI state S3 and reboot the system right away after waking up. Default: 0");

module_param_cb(enabled, &param_s5divert_enabled_ops, &param_s5divert_enabled, 0664);
module_param_cb(poweroff, &param_s5divert_poweroff_ops, &param_s5divert_poweroff, 0220);
module_param_cb(reboot, &param_s5divert_reboot_ops, &param_s5divert_reboot, 0220);
module_param_cb(stroff, &param_s5divert_stroff_ops, &param_s5divert_stroff, 0220);

module_init(s5divert_init);
module_exit(s5divert_exit);
