#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/proc_fs.h>

#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <net/sock.h>

#include "platform.h"

#define NETLINK_USER	31
struct socket *nl_sk;

static ssize_t ilitek_proc_glove_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	DBG_INFO();
	return size;
}

static ssize_t ilitek_proc_glove_write(struct file *filp, const char __user *buff, size_t size, loff_t *pPos)
{
	DBG_INFO();
	return size;
}
static ssize_t ilitek_proc_gesture_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	DBG_INFO();
	return size;
}

static ssize_t ilitek_proc_gesture_write(struct file *filp, const char __user *buff, size_t size, loff_t *pPos)
{
	DBG_INFO();
	return size;
}

static ssize_t ilitek_proc_mp_test_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	DBG_INFO();
	return size;
}

static ssize_t ilitek_proc_mp_test_write(struct file *filp, const char __user *buff, size_t size, loff_t *pPos)
{
	DBG_INFO();
	return size;
}

static ssize_t ilitek_proc_firmware_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	DBG_INFO();
	return size;
}

static ssize_t ilitek_proc_firmware_write(struct file *filp, const char __user *buff, size_t size, loff_t *pPos)
{
	DBG_INFO();
	return size;
}

static long ilitek_proc_i2c_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
}


static ssize_t ilitek_proc_i2c_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	DBG_INFO();
	return size;
}

static ssize_t ilitek_proc_i2c_write(struct file *filp, const char __user *buff, size_t size, loff_t *pPos)
{
	DBG_INFO();
	return size;
}

struct proc_dir_entry *proc_dir_ilitek;
struct proc_dir_entry *proc_i2c;
struct proc_dir_entry *proc_firmware;
struct proc_dir_entry *proc_mp_test;
struct proc_dir_entry *proc_gesture;
struct proc_dir_entry *proc_glove;

struct file_operations proc_i2c_fops = {
	.read  = ilitek_proc_i2c_read,
	.write = ilitek_proc_i2c_write,
};

struct file_operations proc_firmware_fops = {
	.read  = ilitek_proc_firmware_read,
	.write = ilitek_proc_firmware_write,
};

struct file_operations proc_mp_test_fops = {
	.read  = ilitek_proc_mp_test_read,
	.write = ilitek_proc_mp_test_write,
};

struct file_operations proc_gesture_fops = {
	.read  = ilitek_proc_gesture_read,
	.write = ilitek_proc_gesture_write,
};

struct file_operations proc_glove_fops = {
	.read  = ilitek_proc_glove_read,
	.write = ilitek_proc_glove_write,
};

/*
 * This struct lists all file nodes will be created under /proc filesystem.
 *
 * Before creating a node that you want, declaring its file_operations structure
 * is necessary. After that, puts the structure into proc_table, defines its
 * node's name in the same row, and the init function lterates the table and
 * creates all nodes under /proc.
 *
 */
typedef struct {

	// node's name.
	char *name;

	// point to a node created by proc_create.
	struct proc_dir_entry *node;

	// point to a fops declard the above already.
	struct file_operations *fops;

	// indicate if a node is created.
	bool isCreated;
	
} proc_node_t;

proc_node_t proc_table[] = {
	{"i2c",		 NULL, &proc_i2c_fops,     false},
	{"firmware", NULL, &proc_mp_test_fops, false},
	{"mp_test",  NULL, &proc_mp_test_fops, false},
	{"gesture",  NULL, &proc_mp_test_fops, false},
	{"glove",    NULL, &proc_mp_test_fops, false},
};

int ilitek_proc_init(void)
{
	int i = 0, res = 0;
	int node = sizeof(proc_table) / sizeof(proc_table[0]);

	proc_dir_ilitek = proc_mkdir("ilitek", NULL);

	for(; i < node; i++)
	{
		proc_table[i].node = proc_create(proc_table[i].name, 0666, proc_dir_ilitek, proc_table[i].fops);

		if(proc_table[i].node == NULL)
		{
			proc_table[i].isCreated = false;
			DBG_ERR("Failed to create %s under /proc", proc_table[i].name);
			res = -ENODEV;
		}
		else
		{
			proc_table[i].isCreated = true;
			DBG_INFO("Succeed to create %s under /proc", proc_table[i].name);
		}
	}

	return res;
}

void ilitek_proc_remove(void)
{
	int i = 0;
	int node = sizeof(proc_table) / sizeof(proc_table[0]);

	for(; i < node; i++)
	{
		if(proc_table[i].isCreated == true)
		{
			DBG_INFO("Removed %s under /proc", proc_table[i].name);
			remove_proc_entry(proc_table[i].name, proc_dir_ilitek);
		}
	}

	remove_proc_entry("ilitek", NULL);
}

void netlink_recv_msg(struct sk_buff *skb)
{
#if 0
	struct nlmsghdr *nlh;
	int pid;
	struct sk_buff *skb_out;
	int msg_size;
	char *msg = "Hello from kernel";
	int res;

	DBG_INFO();

	msg_size = strlen(msg);

	nlh = (struct nlmsghdr *)skb->data;
	printk(KERN_INFO "Netlink received msg payload:%s\n", (char *)nlmsg_data(nlh));
	pid = nlh->nlmsg_pid; /*pid of sending process */

	skb_out = nlmsg_new(msg_size, 0);
	if (!skb_out) {
		printk(KERN_ERR "Failed to allocate new skb\n");
		return;
	}

	nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
	NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */
	strncpy(nlmsg_data(nlh), msg, msg_size);

	res = nlmsg_unicast(nl_sk, skb_out, pid);
	if (res < 0)
		printk(KERN_INFO "Error while sending bak to user\n");
#endif
}

int netlink_init(void)
{
	int res = 0;

	DBG_INFO();
#if 0
	struct netlink_kernel_cfg cfg = {
		.input = netlink_recv_msg,
	};

	nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);

	if(!nl_sk)
	{
		DBG_ERR("Failed to create nelink socket");
		return -EFAULT;
	}
#endif 

	return res;
}

void netlink_close(void)
{
	DBG_INFO();

	//netlink_kernel_release(nl_sk);

	ilitek_proc_remove();
}
