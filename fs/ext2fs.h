#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#if _MSC_VER
#define WINDOWS
#endif

#if _GNUC_
#define UNIX
#endif




#define FILE_WRITE_OVERRIDE 1
#define	FILE_WRITE_APPEND 2
#define BLOCK_SIZE 1024			/*size of every data block 1024B*/
#define INODE_SIZE 128			/*size of inode 128B*/
#define BLOCKS_PER_GRP 100		/*成组链接法每组盘块数*/
#define BLOCK_GROUP_NUM 100
#define PATH_LEN 300
#define DIR_ITEM_NUM 24
#define USR_NAME_SIZE_LIMIT 32
#define GRP_NAME_SIZE_LIMIT 32
#define DIR_NAME_SIZE_LIMIT 32
#define FILE_NAME_SIZE_LIMIT 32
#define FS_READ_BUF 100*BLOCK_SIZE
#define FS_WRITE_BUF 100*BLOCK_SIZE
#define DIRECT_BLOCK_NUM 12
#define INDIRECT1_BLOCK_NUM 1
//#define INDIRECT2_BLOCK_NUM 1
//#define INDIRECT3_BLOCK_NUM 1
#define FILESYSTEM_SIZE 500000
#define INODE_NUM (FILESYSTEM_SIZE/BLOCK_SIZE + 1)
#define BLCOK_NUM INODE_NUM
#define FILE_MAX_SIZE (12*BLOCK_SIZE + BLOCK_SIZE/4 \
		+ (BLOCK_SIZE/4)*(BLOCK_SIZE/4) \
		+ (BLOCK_SIZE/4)*(BLOCK_SIZE/4)*(BLOCK_SIZE/4))


#define FILE_PERMISSION_DEFAULT 113
#define DIR_PERMISSION_UMASK 022
#define DIR_MODE 0
#define FILE_MODE 1
#define USR_RD 04
#define USR_WR 02
#define USR_EX 01
#define GRP_RD 04
#define GRP_WR 02
#define GRP_EX 01
#define OTH_RD 04
#define OTH_WR 02
#define OTH_EX 01


struct mode{
	unsigned int TYPE : 4;
	unsigned int RESERVE : 3;
	unsigned int USR_MODE : 3;
	unsigned int GRP_MODE : 3;
	unsigned int OTH_MODE : 3;
};

typedef struct {
	unsigned int s_inode_num;
	unsigned int s_block_num;

	unsigned int s_free_inode_num;
	unsigned int s_free_block_num;
	unsigned int s_free_top_block;
	int s_free[BLOCKS_PER_GRP];
	unsigned int s_block_per_group;

	unsigned int s_first_data_block;
	unsigned int s_superblock_addr;
	unsigned int s_inode_addr;
	unsigned int s_inode_bitmap;
	unsigned int s_block_bitmap;

}SuperBlock;

typedef struct {
	unsigned int i_ino;
	struct mode i_mode;
	unsigned int i_cnt;

	//char i_usrname[USR_NAME_SIZE_LIMIT];
	//char i_grpname[GRP_NAME_SIZE_LIMIT];
	unsigned int i_uid;
	unsigned int i_gid;
	unsigned int i_size;
	time_t i_ctime;				/*inode changed time*/
	time_t i_mtime;				/*file or directory diritem changed time*/
	time_t i_atime;				/*file or directory access time*/

	int i_direct_block[DIRECT_BLOCK_NUM] = { 0 };
	int i_indirect_block;
	int i_indirect2_block;
	int i_indirect3_block;
}Inode;

typedef struct {
	char dirname[DIR_NAME_SIZE_LIMIT] = { 0 };
	unsigned int inode_addr;
}DirItem;

typedef struct {
	bool isLogin;
	unsigned int uid;
	unsigned int gid;
	char user_name[50] = { 0 };
	char user_group[50] = { 0 };
	char user_home[PATH_LEN] = { 0 };
	char currentdir[PATH_LEN] = { 0 };
	unsigned int currentInode;
}UserInfo;

typedef struct {
	unsigned int freeblock_remain;				/*clean block of current block group*/
	int freeblock[BLOCKS_PER_GRP] = { 0 };		/*address of next block group*/
}BlockGroupInfo;

typedef struct {
	unsigned int indirect_block_addr;
}IndirectBaddr;
