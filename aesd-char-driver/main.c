/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd_ioctl.h"
int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Li-Huan Lu"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
	
	struct aesd_dev *dev; // device information
	
	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev;
	
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
	struct aesd_dev *dev = filp->private_data; 
	struct aesd_buffer_entry *entry;
	size_t entry_offset_byte;
	size_t bytes_read, bytes_copy;
	
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	
	// partial read rule, read one command at a time, update the f_pos, and user will keep calling
	entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->aesd_cb, *f_pos, &entry_offset_byte);
	if (!entry){
		retval = 0;
		goto out;
	}
	
	bytes_read = entry->size - entry_offset_byte;
	
	if (bytes_read > count) bytes_copy = count;
	else                    bytes_copy = bytes_read;
	
	if (copy_to_user(buf, (entry->buffptr + entry_offset_byte), bytes_copy)) {
		retval = -EFAULT;
		goto out;
	}
	*f_pos += bytes_copy;
	retval = bytes_copy;
	
out:
	mutex_unlock(&dev->lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
	struct aesd_dev *dev = filp->private_data; 
	const char *rtn_ptr; 
	char *tmp_buffer;
	
	tmp_buffer = kmalloc(count, GFP_KERNEL);
	if (!tmp_buffer){
		retval = -ENOMEM;
		goto out;
	}
	
	if (copy_from_user(tmp_buffer, buf, count)) {
		retval = -EFAULT;
		kfree(tmp_buffer);
		goto out;
	}
	
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	
	if (dev->tmp_entry.buffptr){
	    char *new_buf = kmalloc(dev->tmp_entry.size + count, GFP_KERNEL);
        if (!new_buf) {
            kfree(tmp_buffer);
            retval = -ENOMEM;
            goto out;
        }
        memcpy(new_buf, dev->tmp_entry.buffptr, dev->tmp_entry.size);
        memcpy(new_buf + dev->tmp_entry.size, tmp_buffer, count);
        kfree(dev->tmp_entry.buffptr);
        dev->tmp_entry.buffptr = new_buf;
        dev->tmp_entry.size += count;
        kfree(tmp_buffer);		
	}
	else {
		dev->tmp_entry.buffptr = tmp_buffer;
        dev->tmp_entry.size = count;
	}
	
	retval = count;
	
	// check '\n'
	if (dev->tmp_entry.buffptr[dev->tmp_entry.size - 1] == '\n') {
        const struct aesd_buffer_entry entry_to_add = {
            .buffptr = dev->tmp_entry.buffptr,
            .size = dev->tmp_entry.size
        };
		
        rtn_ptr = aesd_circular_buffer_add_entry(&dev->aesd_cb, &entry_to_add);
        if (rtn_ptr)
            kfree((void *)rtn_ptr);

        // reset dev->tmp_entry
        dev->tmp_entry.buffptr = NULL;
        dev->tmp_entry.size = 0;
    }
	
out:
	mutex_unlock(&dev->lock);
    return retval;
}

loff_t aesd_llseek(struct file *filp, loff_t off, int whence)
{
	struct aesd_dev *dev = filp->private_data; 
	loff_t newpos;
	loff_t retval;
    
	uint8_t index;
	struct aesd_buffer_entry *entry;
	size_t total_size = 0;
	
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	
	AESD_CIRCULAR_BUFFER_FOREACH(entry,&dev->aesd_cb,index){
		total_size += entry->size;
	}
	
    PDEBUG("llseek: off=%lld, whence=%d, total_size=%zu", off, whence, total_size);

    retval = fixed_size_llseek(filp, off, whence, total_size);

    mutex_unlock(&dev->lock);
    return retval;
}

/*
 *    Adjust the file offset (f_pos) of @para, filp based on the location specified by
 *    @param wirte_cmd (the zero referenced command to locate)
 *    and @param write_cmd_offset (the zero referenced offset into the command)
 *    @return 0 if success
 *            -ERESTARTSYS if mutex could not be obtained
 *            -EINVAL if write_cmd or write_cmd_offset is out of range
 */
static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset)
{
	if (write_cmd >= AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) return -EINVAL;
	
	struct aesd_dev *dev = filp->private_data;
	uint8_t index;
	struct aesd_buffer_entry *entry;
	int retval = 0;
	unsigned int start_offset = 0;
	
	if (mutex_lock_interruptible(&dev->lock))
		return -ERESTARTSYS;
	
	index = dev->aesd_cb.out_offs;

	for (int i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++){
		entry = &dev->aesd_cb.entry[index];
		
		if (entry->buffptr == NULL) break; // unused
		
		if (i == write_cmd){
			if (write_cmd_offset >= entry->size){
		        retval = -EINVAL;
		        goto out;
	        }
			
			filp->f_pos = start_offset + write_cmd_offset;
			goto out;
		}
		
		start_offset += entry->size;

		index = (index + 1) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
	}
		
	
	
	filp->f_pos = start_offset + write_cmd_offset;
	
out:	
	mutex_unlock(&dev->lock);
    return retval;
}

long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;

	if (_IOC_TYPE(cmd) != AESD_IOC_MAGIC) return -ENOTTY;
	if (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR) return -ENOTTY;
    
    switch(cmd) {
        case AESDCHAR_IOCSEEKTO: {
		    struct aesd_seekto seekto;
			if (copy_from_user(&seekto, (const void __user *)arg, sizeof(seekto)) != 0)
				retval = EFAULT;
			else 
				retval = aesd_adjust_file_offset(filp, seekto.write_cmd, seekto.write_cmd_offset);
		    break;
		}
		
		default: // redundant, as cmd was checked against MAXNR
		    return -ENOTTY;
    }	

    return retval;	
}

struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
	.llseek =   aesd_llseek,
	.unlocked_ioctl = aesd_ioctl,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    mutex_init(&aesd_device.lock); // initialize locking primitive
    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */
    // free memories, handle the locking primitives 
	uint8_t index;
	struct aesd_buffer_entry *entry;
	AESD_CIRCULAR_BUFFER_FOREACH(entry,&aesd_device.aesd_cb,index){
		if (entry->buffptr)
			kfree(entry->buffptr);
	}
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
