#ifndef __HARDWARE_H
#define __HARDWARE_H

#define START_HARDWARE_ADDRESS 0x10001000
#define END_HARDWARE_ADDRESS 0x10009000
#define SCREEN_ADDRESS 0x10000000
#define UART_STATUS 0x10000005
#define COM1_PORT 0x3F8
#define VIRTIO_DEVICE_STATUS 0x070
#define VIRTIO_DEVICE_ID 0x008
#define VIRTIO_VERSION 0x004
#define VIRTIO_MAGIC_VALUE 0x000
#define VIRTIO_MAGIC_EQUAL_TO 0x74726976
#define VIRTIO_DEVICE_FEATURES 0x010
#define VIRTIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_DRIVER_FEATURES 0x020
#define VIRTIO_DRIVER_FEATURES_SEL 0x024
#define VIRTQ_NUM 16
#define VRING_DESC_F_NEXT 1
#define VRING_DESC_F_WRITE 2
#define VIRTIO_QUEUE_SEL 0x030
#define VIRTIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_QUEUE_NUM 0x038
#define VIRTIO_QUEUE_READY 0x044
#define VIRTIO_QUEUE_DESC_LOW 0x080
#define VIRTIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_QUEUE_DRIVER_LOW 0x090
#define VIRTIO_QUEUE_DRIVER_HIGH 0x094
#define VIRTIO_QUEUE_DEVICE_LOW 0x0a0
#define VIRTIO_QUEUE_DEVICE_HIGH 0x0a4
#define VIRTIO_QUEUE_NOTIFY 0x050
#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
struct virtq_desc {
   uint64_t addr;
   uint32_t len;
   uint16_t flags;
   uint16_t next;
};
struct virtq_avail {
   uint16_t flags;
   uint16_t idx;
   uint16_t ring[VIRTQ_NUM];
   uint16_t unused;
};
struct virtq_used_elem {
   uint32_t id;
   uint32_t len;
};    
struct virtq_used {
   uint16_t flags;
   uint16_t idx;
   struct virtq_used_elem ring[VIRTQ_NUM];
};
struct virtio_blk_req {
   uint32_t type;
   uint32_t reserved;
   uint64_t sector;
};

struct virtio_ipc_req {
   uint32_t cmd;
   uint32_t sector;
   void *buffer;
};

#define EXT2_MAGIC 0xEF53

struct ext2_super_block {
   uint32_t ext2_inodes_count;
   uint32_t ext2_blocks_count;
   uint32_t ext2_r_blocks_count;
   uint32_t ext2_free_blocks_count;
   uint32_t ext2_free_inodes_count;
   uint32_t ext2_first_data_block;
   uint32_t ext2_log_block_size;
   uint32_t ext2_log_frag_size;
   uint32_t ext2_blocks_per_group;
   uint32_t ext2_frags_per_group;
   uint32_t ext2_inodes_per_group;
   uint32_t ext2_mtime;
   uint32_t ext2_wtime;
   uint16_t ext2_mnt_count;
   uint16_t ext2_max_mnt_count;
   uint16_t ext2_magic;
   uint16_t ext2_state;
   uint16_t ext2_errors;
   uint16_t ext2_minor_rev_level;
   uint32_t ext2_lastcheck;
   uint32_t ext2_checkinterval;
   uint32_t ext2_creator_os;
   uint32_t ext2_rev_level;
   uint16_t ext2_def_resuid;
   uint16_t ext2_def_resgid;
   uint32_t ext2_first_ino;
   uint16_t ext2_inode_size;
};

struct ext2_group_desc {
   uint32_t bg_block_bitmap;
   uint32_t bg_inode_bitmap;
   uint32_t bg_inode_table;
   uint16_t bg_free_blocks_count;
   uint16_t bg_free_inodes_count;
   uint16_t bg_used_dirs_count;
   uint16_t bg_pad;
   uint32_t bg_reserved[3];
};

struct ext2_inode {
   uint16_t inode_mode;
   uint16_t inode_uid;
   uint32_t inode_size;
   uint32_t inode_atime;
   uint32_t inode_ctime;
   uint32_t inode_mtime;
   uint32_t inode_dtime;
   uint16_t inode_gid;
   uint16_t inode_links_count;
   uint32_t inode_blocks;
   uint32_t inode_flags;
   uint32_t inode_osd1;
   uint32_t inode_block[15];
   uint32_t inode_generation;
   uint32_t inode_file_acl;
   uint32_t inode_dir_acl;
   uint32_t inode_faddr;
   uint32_t inode_osd2[3];
};

struct ext2_dir_entry {
   uint32_t inode;
   uint16_t rec_len;
   uint8_t name_len;
   uint8_t file_type;
   char name[];
};

#endif
