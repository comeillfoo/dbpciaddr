#include <linux/module.h> /* essential for modules */
#include <linux/kernel.h> /* essential for KERNEL_INFO */

#include <linux/slab.h> /* essential for kmalloc, kfree */
#include <linux/fdtable.h> /* essential for files_struct */
#include <linux/pid.h> /* essential for get_task_pid, find_get_pid */
#include <linux/fs.h> /* essential for file_operations */
#include <linux/debugfs.h> /* essential for debugfs */
#include <linux/pci.h> /* essential for pci_get_device */

MODULE_LICENSE( "GPL" );

#define MOD_NAME "dbpciaddr"
#define ROOT_DBFS_NAME "dbpciaddr"
#define MOD_CONTROLLER_NAME "ctrlargs"
#define MIN_KERN_BUF_CAP 16
#define MAX_PATH_LEN 4096

static struct dentry* root_dir = NULL;
static struct dentry* ctrlargs = NULL;
static struct dentry* recent_blob = NULL;

static int ctrlargs_open( struct inode* ptr_inode, struct file* ptr_file ) { return 0; }
static int ctrlargs_release( struct inode* ptr_inode, struct file* ptr_file ) { return 0; }

static ssize_t ctrlargs_read( struct file* ptr_file, char __user* buffer, size_t length, loff_t* ptr_offset );
static ssize_t ctrlargs_write( struct file* ptr_file, const char __user* buffer, size_t length, loff_t* ptr_offset );

static struct file_operations ctrlargs_ops = {
  .owner   = THIS_MODULE,
  .open    = ctrlargs_open,
  .read    = ctrlargs_read,
  .write   = ctrlargs_write,
  .release = ctrlargs_release 
};


static int __init init_dbpciaddr( void ) {
    printk( KERN_INFO MOD_NAME ": init_" MOD_NAME ": module loaded\n" );
    root_dir  = debugfs_create_dir( ROOT_DBFS_NAME, NULL );
    ctrlargs = debugfs_create_file( MOD_CONTROLLER_NAME, 0666, root_dir, NULL, &ctrlargs_ops );
    return 0;
}


static void __exit cleanup_dbpciaddr( void ) {
    printk( KERN_INFO MOD_NAME ": cleanup_" MOD_NAME ": module unloaded\n" );
    debugfs_remove_recursive( root_dir );
}


module_init( init_dbpciaddr );
module_exit( cleanup_dbpciaddr );

static ssize_t stub_read( struct file* ptr_file, const char __user* buffer, size_t length, loff_t* ptr_offset ) {
  printk( KERN_INFO MOD_NAME ": stub_read: read inited\n" );
  if ( *ptr_offset > 0 ) return 0;
  *ptr_offset += length;
  printk( KERN_INFO MOD_NAME ": stub_read: read %zu bytes\n", length );
  return length;
}

static ssize_t ctrlargs_read( struct file* ptr_file, char __user* buffer, size_t length, loff_t* ptr_offset ) {
  return stub_read( ptr_file, buffer, length, ptr_offset );
}

enum dest_struct {
  DS_ADDRESS_SPACE,
  DS_PCI_DEV,
  DS_UNKNOWN
};

struct param_address_space {
  enum dest_struct dstruct;
  char* filename;
};

struct param_pci_dev {
  enum dest_struct dstruct;
  u32 vendor;
  u32 device;
};

struct param_unknown {
  enum dest_struct dstruct;
};

union command {
  enum dest_struct dstruct;
  struct param_address_space as_address_space;
  struct param_pci_dev as_pci_dev;
  struct param_unknown as_unknown;
};

static const union command unknown_action = {
  .dstruct = DS_UNKNOWN,
  .as_unknown = { .dstruct = DS_UNKNOWN }
};

static union command common_action = unknown_action;
static struct debugfs_blob_wrapper common_wrapper = {0};

static enum dest_struct ds_of( const char raw_type ) {
  switch ( raw_type ) {
    case 'a':
      return DS_ADDRESS_SPACE;
    case 'p':
      return DS_PCI_DEV;
    default:
      return DS_UNKNOWN;
  }
}

static ssize_t read_address_space( struct param_address_space* as_address_space, const char __user* buffer, size_t length, loff_t* ptr_offset ) {
  char* filename = kmalloc( sizeof( char ) * length, GFP_KERNEL );
  int read_bytes = snprintf( filename, length, "%s", buffer );
  *ptr_offset += read_bytes;
  printk( KERN_INFO MOD_NAME ": read_address_space: read [ filename=\"%s\" ] in %d bytes\n", filename, read_bytes );

  as_address_space->dstruct = DS_ADDRESS_SPACE;
  as_address_space->filename = filename;

  return read_bytes;
}

static size_t get_length( u32 number ) { return snprintf( NULL, 0, "%u", number ); }

static ssize_t read_pci_dev( struct param_pci_dev* as_pci_dev, const char __user* buffer, size_t length, loff_t* ptr_offset ) {
  u32 vendor = PCI_ANY_ID;
  u32 device = PCI_ANY_ID;

  size_t params_count = sscanf( buffer, "%u:%u\n", &vendor, &device );
  size_t read_bytes = get_length( vendor ) + get_length( device ) + 2;
  printk( KERN_INFO MOD_NAME ": read_pci_dev: read %zu parameters [ %u:%u ] contains %zu bytes\n", params_count, vendor, device, read_bytes );
  if ( params_count < 2 ) read_bytes = 0;
  *ptr_offset += read_bytes;

  as_pci_dev->dstruct = DS_PCI_DEV;
  as_pci_dev->vendor = vendor;
  as_pci_dev->device = device;
  return read_bytes;
}

static ssize_t read_action( union command* action, const char __user* buffer, size_t length, loff_t* ptr_offset, enum dest_struct dstruct ) {
  ssize_t read_bytes = 0;
  action->dstruct = dstruct;
  switch ( dstruct ) {
    case DS_ADDRESS_SPACE:
      read_bytes += read_address_space( &( action->as_address_space ), buffer, length, ptr_offset );
      break;
    case DS_PCI_DEV:
      read_bytes += read_pci_dev( &( action->as_pci_dev ), buffer, length, ptr_offset );
      break;
    default:
      *action = unknown_action; 
      read_bytes += 0;
      break;
  }
  return read_bytes;
}

static struct address_space get_address_space( const char* filename ) {
  struct file* file = filp_open( filename, O_RDONLY, 0444 );
  struct address_space result = *(file->f_inode->i_mapping);
  filp_close( file, NULL );
  return result;
}  

static void dbfs_create_address_space_blob( struct param_address_space as_address_space, struct dentry* where ) {
  struct address_space wrapped_object = get_address_space( as_address_space.filename );
  common_wrapper.data = &wrapped_object;
  common_wrapper.size = sizeof( struct address_space );
  recent_blob = debugfs_create_blob( "address_space", 0444, where, &common_wrapper );
  kfree( as_address_space.filename );
}

static void dbfs_create_pci_dev_blob( struct param_pci_dev as_pci_dev, struct dentry* where ) {
  u32 vendor = as_pci_dev.vendor;
  u32 device = as_pci_dev.device;
  struct pci_dev* wrapped_object = pci_get_device( vendor, device, NULL );
  printk( KERN_INFO MOD_NAME ": dbfs_create_pci_dev_blob: wrapped_object=%p, vendor=%u, device=%u\n", wrapped_object, vendor, device );
  common_wrapper.data = &wrapped_object;
  common_wrapper.size = sizeof( struct pci_dev );
  recent_blob = debugfs_create_blob( "pci_dev", 0444, where, &common_wrapper );
}

static void dbfs_create_blob( union command action, struct dentry* where ) {
  if ( recent_blob != NULL ) {
    debugfs_remove( recent_blob );
    printk( KERN_INFO MOD_NAME ": dbfs_create_blob: recent blob successfully removed\n" );
  }
  switch ( action.dstruct ) {
    case DS_ADDRESS_SPACE:
      dbfs_create_address_space_blob( action.as_address_space, where );
      break;
    case DS_PCI_DEV:
      dbfs_create_pci_dev_blob( action.as_pci_dev, where );
      break;
    default:
      return;
      break;
  }
  printk( KERN_INFO MOD_NAME ": dbfs_create_blob: blob successfully created\n" );
}

static ssize_t ctrlargs_write( struct file* ptr_file, const char __user* buffer, size_t length, loff_t* ptr_offset ) {
  printk( KERN_INFO MOD_NAME ": ctrlargs_write: write inited\n" );
  printk( KERN_INFO MOD_NAME ": ctrlargs_write: ptr_file=%p, buffer=\"%s\", length=%zu, offset=%lld\n", ptr_file, buffer, length, *ptr_offset );
  if ( *ptr_offset > 0 ) return length;

  enum dest_struct dstruct = ds_of( buffer[ 0 ] ); // 'a' - address_space; 'p' - pci_dev
  *ptr_offset += 1;
  const ssize_t read_bytes = read_action( &common_action, buffer + 1, length - 1, ptr_offset, dstruct ) + 1;
  length -= *ptr_offset - read_bytes;
  printk( KERN_INFO MOD_NAME ": ctrlargs_write: ptr_file=%p, buffer=\"%s\", length=%zu, offset=%lld, read_bytes=%zu\n", ptr_file, buffer, length, *ptr_offset, read_bytes );
  if ( length != read_bytes ) return -EINVAL;

  printk( KERN_INFO MOD_NAME ": will be created blob for %s\n", ( (dstruct == DS_ADDRESS_SPACE)? "address_space" : "pci_dev" ) );
  dbfs_create_blob( common_action, root_dir );

  return length;
}