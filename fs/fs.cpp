#include "fs.h"

const int Superblock_StartAddr = 0;
const int InodeBitmap_StartAddr = 1 * BLOCK_SIZE;
const int BlockBitmap_StartAddr = InodeBitmap_StartAddr + 1 * BLOCK_SIZE;
const int Inode_StartAddr = BlockBitmap_StartAddr + 1 * BLOCK_SIZE;
const int Datablock_StartAddr = Inode_StartAddr + (INODE_NUM / (BLOCK_SIZE / INODE_SIZE) + 1) * BLOCK_SIZE;

bool inode_bitmap[1 * BLOCK_SIZE];
bool block_bitmap[1 * BLOCK_SIZE];
SuperBlock sblock;
DirItem dir[DIR_ITEM_NUM];
BlockGroupInfo blockgroup_info;
FILE *fsfp;
UserInfo userinfo;
IndirectBaddr ib_addr[BLOCK_SIZE / 4];		/*indirect addr block*/
Inode inode;
char readbuf[FS_READ_BUF];
char writebuf[FS_WRITE_BUF];

int
ialloc()
/*alloc a free inode and return the inode address*/
/*process include:	*/
/***************** 1. modify inode_bitmap*/
/***************** 2. modify superblock*/
/***************** 3. return alloced inode address, if faild return -1*/
{
	if (sblock.s_free_inode_num == 0)
	{
		fprintf(stderr, "no more inode");
		return -1;
	}
	else
	{
		/*read inode_bitmap*/
		fseek(fsfp, InodeBitmap_StartAddr, SEEK_SET);
		fread(inode_bitmap, sizeof(inode_bitmap), 1, fsfp);

		/*read superblock*/
		fseek(fsfp, Superblock_StartAddr, SEEK_SET);
		fread(&sblock, sizeof(SuperBlock), 1, fsfp);


		int i = 0;
		for (i = 0; i < sblock.s_inode_num; i++)
		{
			if (inode_bitmap[i] == 0)	/*found*/
				break;
		}

		/*write back inode_bitmap*/
		inode_bitmap[i] = 1;
		fseek(fsfp, InodeBitmap_StartAddr + i, SEEK_SET);
		fwrite(&inode_bitmap[i], sizeof(bool), 1, fsfp);
		fflush(fsfp);

		/*writeback superblock*/
		sblock.s_free_inode_num--;
		fseek(fsfp, Superblock_StartAddr, SEEK_SET);
		fwrite(&sblock, sizeof(SuperBlock), 1, fsfp);
		fflush(fsfp);

		return (Inode_StartAddr + i * INODE_SIZE);	/*return inode address*/
	}
}

bool
ifree(unsigned int inode_num)
/*free a alloced inode and blocks related*/
/*process include:	*/
/***************** 1. free inode and modify inode_bitmap*/
/***************** 2. free blocks and modify block_bitmap*/
/***************** 3. modify blockgroup_info*/
/***************** 4. modify superblock*/
{
	/*read inode_bitmap*/
	fseek(fsfp, InodeBitmap_StartAddr, SEEK_SET);
	fread(inode_bitmap, sizeof(inode_bitmap), 1, fsfp);

	/*read superblock*/
	fseek(fsfp, Superblock_StartAddr, SEEK_SET);
	fread(&sblock, sizeof(SuperBlock), 1, fsfp);

	/*read block_bitmap*/
	fseek(fsfp, BlockBitmap_StartAddr, SEEK_SET);
	fread(block_bitmap, sizeof(block_bitmap), 1, fsfp);


	unsigned int tmpaddr = Inode_StartAddr;
	int i = 0;
	//int j = 0;
	if (inode_bitmap[inode_num] == 1)
	{
		//inode_bitmap[inode_num] == 0;
		tmpaddr += (inode_num * INODE_SIZE);
		//Inode tmpinode = { 0 };

		/*get inode info*/
		fseek(fsfp, tmpaddr, SEEK_SET);
		fread(&inode, sizeof(Inode), 1, fsfp);

		if (inode.i_uid == userinfo.uid || inode.i_gid == userinfo.gid \
			|| (strcmp(userinfo.user_name, "root") == 0))
		{
			inode_bitmap[inode_num] = 0;	/*free bitmap*/


			//memory needed to store directory or file
			unsigned int need = (inode.i_size % BLOCK_SIZE == 0) ? ((inode.i_size / BLOCK_SIZE)*BLOCK_SIZE) : ((inode.i_size / BLOCK_SIZE + 1)*BLOCK_SIZE);
			/*del file info*/
			/*DIRECT BLOCK USED ONLY*/
			if (need <= DIRECT_BLOCK_NUM * BLOCK_SIZE)
			{
				for (i = DIRECT_BLOCK_NUM - 1; i >= 0; i--)
				{
					if (inode.i_direct_block[i] != -1)
					{
						/*modify block_bitmap*/
						int blocknumber = ((inode.i_direct_block[i] - Datablock_StartAddr) / BLOCK_SIZE);
						block_bitmap[blocknumber] = 0;
						fseek(fsfp, BlockBitmap_StartAddr + blocknumber, SEEK_SET);
						fwrite(&block_bitmap[blocknumber], sizeof(bool), 1, fsfp);
						fflush(fsfp);

						/*modify block group info*/
						fseek(fsfp, Datablock_StartAddr + (blocknumber / BLOCKS_PER_GRP)*BLOCKS_PER_GRP*BLOCK_SIZE, SEEK_SET);
						fread(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);

						blockgroup_info.freeblock_remain++;
						fseek(fsfp, Datablock_StartAddr + (blocknumber / BLOCKS_PER_GRP)*BLOCKS_PER_GRP*BLOCK_SIZE, SEEK_SET);
						fwrite(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);
						fflush(fsfp);

						/*modify superblock*/
						sblock.s_free_block_num++;
					}
				}
			}
			/*direct block and indirect block*/
			else if (need <= (DIRECT_BLOCK_NUM * BLOCK_SIZE + (BLOCK_SIZE / 4)*BLOCK_SIZE))
			{
				/*jump once*/

				fseek(fsfp, inode.i_indirect_block, SEEK_SET);
				fread(ib_addr, sizeof(ib_addr), 1, fsfp);
				for (i = (BLOCK_SIZE / 4 - 1); i >= 0; i--)
				{
					if (ib_addr[i].indirect_block_addr != -1)
					{
						/*modify block_bitmap*/

						unsigned int blocknumber = ((ib_addr[i].indirect_block_addr - Datablock_StartAddr) / BLOCK_SIZE);
						block_bitmap[blocknumber] = 0;
						fseek(fsfp, BlockBitmap_StartAddr + blocknumber, SEEK_SET);
						fwrite(&block_bitmap[blocknumber], sizeof(bool), 1, fsfp);
						fflush(fsfp);

						/*modify superblock*/
						sblock.s_free_block_num++;

						/*modify block group info*/
						fseek(fsfp, Datablock_StartAddr + (blocknumber / BLOCKS_PER_GRP)*BLOCKS_PER_GRP*BLOCK_SIZE, SEEK_SET);
						fread(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);

						blockgroup_info.freeblock_remain++;
						fseek(fsfp, Datablock_StartAddr + (blocknumber / BLOCKS_PER_GRP)*BLOCKS_PER_GRP*BLOCK_SIZE, SEEK_SET);
						fwrite(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);
						fflush(fsfp);
					}
				}

				for (i = DIRECT_BLOCK_NUM - 1; i >= 0; i++)
				{
					/*modify block_bitmap*/

					unsigned int blocknumber = ((inode.i_direct_block[i] - Datablock_StartAddr) / BLOCK_SIZE);
					block_bitmap[blocknumber] = 0;
					fseek(fsfp, BlockBitmap_StartAddr + blocknumber, SEEK_SET);
					fwrite(&block_bitmap[blocknumber], sizeof(bool), 1, fsfp);
					fflush(fsfp);

					/*modify superblock*/
					sblock.s_free_block_num++;

					/*modify block group info*/
					fseek(fsfp, Datablock_StartAddr + (blocknumber / BLOCKS_PER_GRP)*BLOCKS_PER_GRP*BLOCK_SIZE, SEEK_SET);
					fread(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);

					blockgroup_info.freeblock_remain++;
					fseek(fsfp, Datablock_StartAddr + (blocknumber / BLOCKS_PER_GRP)*BLOCKS_PER_GRP*BLOCK_SIZE, SEEK_SET);
					fwrite(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);
					fflush(fsfp);
				}
			}
#ifdef INDIRECT2_BLOCK_NUM
			else if (need <= (DIRECT_BLOCK_NUM * BLOCK_SIZE \
				+ (BLOCK_SIZE / 4)*BLOCK_SIZE)\
				+ (BLOCK_SIZE / 4)*(BLOCK_SIZE / 4)*BLOCK_SIZE)
			{

			}
#endif

#ifdef INDIRECT3_BLOCK_NUM
			else if (1)
			{

			}
#endif
			///*write back inode*/
			//fseek(fsfp, tmpaddr, SEEK_SET);
			//fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
			//fflush(fsfp);

			/*write back inode_bitmap*/
			fseek(fsfp, InodeBitmap_StartAddr + inode_num, SEEK_SET);
			fwrite(&inode_bitmap[inode_num], sizeof(bool), 1, fsfp);
			fflush(fsfp);

			/*write back superblock*/
			sblock.s_free_inode_num++;

			fseek(fsfp, sblock.s_free[0], SEEK_SET);
			fread(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);

			if (sblock.s_free_top_block != blockgroup_info.freeblock_remain)
			{
				sblock.s_free_top_block = blockgroup_info.freeblock_remain;
			}
			fseek(fsfp, Superblock_StartAddr, SEEK_SET);
			fwrite(&sblock, sizeof(SuperBlock), 1, fsfp);
			fflush(fsfp);

			return true;
		}
		else
		{
			//printf("Permission denied\n");
			return false;
		}
	}
	else
	{
		return true;
	}
}

int
balloc()
/*alloc a free block and return the block address*/
/*process include:	*/
/***************** 1. modify block_bitmap*/
/***************** 2. modify blockgroup_info*/
/***************** 3. modify superblock*/
/***************** 4. return alloced block address, if failed return -1*/
{
	///*read inode_bitmap*/
	//fseek(fsfp, InodeBitmap_StartAddr, SEEK_SET);
	//fread(inode_bitmap, sizeof(inode_bitmap), 1, fsfp);

	/*read superblock*/
	fseek(fsfp, Superblock_StartAddr, SEEK_SET);
	fread(&sblock, sizeof(SuperBlock), 1, fsfp);

	/*read block_bitmap*/
	fseek(fsfp, BlockBitmap_StartAddr, SEEK_SET);
	fread(block_bitmap, sizeof(block_bitmap), 1, fsfp);

	/*read blockgroup info*/
	fseek(fsfp, sblock.s_free[0], SEEK_SET);
	fread(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);

	unsigned int blocknumber;
	int i = 0;
	if (sblock.s_free_block_num == 0) /*all allocated*/
	{
		printf("no more block\n");
		return -1;
	}
	/*................*/
	/***********************************unfinsihed in while()*/
	/* if current block group all used*/
	while (sblock.s_free_top_block == 0)
	{
		sblock.s_free_top_block = blockgroup_info.freeblock_remain;

		for (i = 0; i < BLOCKS_PER_GRP; i++)
		{
			sblock.s_free[i] = blockgroup_info.freeblock[i];
		}
	}

	/*if blocks of currnet bock group not all used*/
	blocknumber = (sblock.s_free[0] - Datablock_StartAddr) / BLOCK_SIZE;
	for (i = 0; i < BLOCKS_PER_GRP; i++)
	{
		if (block_bitmap[blocknumber + i] == 0)		/*found a blank block*/
			break;
	}

	///*read current block group info*/
	//fseek(fsfp, sblock.s_free[0], SEEK_SET);
	//fread(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);

	/*modify block info*/
	block_bitmap[blocknumber + i] = 1;
	sblock.s_free_block_num--;

	sblock.s_free_top_block--;
	blockgroup_info.freeblock_remain = sblock.s_free_top_block;

	/*write back blockgreoup info*/
	fseek(fsfp, sblock.s_free[0], SEEK_SET);
	fwrite(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);
	fflush(fsfp);

	/*write back block_bitmap*/
	fseek(fsfp, BlockBitmap_StartAddr + blocknumber + i, SEEK_SET);
	fwrite(&block_bitmap[blocknumber + i], sizeof(bool), 1, fsfp);
	fflush(fsfp);

	/*write back superblock*/
	fseek(fsfp, Superblock_StartAddr, SEEK_SET);
	fwrite(&sblock, sizeof(SuperBlock), 1, fsfp);
	fflush(fsfp);

	return (sblock.s_free[0] + i * BLOCK_SIZE);		/*return block addr*/
}

bool
bfree(unsigned int blockAddr)
{
	/*read superblock*/
	fseek(fsfp, Superblock_StartAddr, SEEK_SET);
	fread(&sblock, sizeof(SuperBlock), 1, fsfp);

	/*read block_bitmap*/
	fseek(fsfp, BlockBitmap_StartAddr, SEEK_SET);
	fread(block_bitmap, sizeof(block_bitmap), 1, fsfp);

	unsigned int blocknumber = (blockAddr - Datablock_StartAddr) / BLOCK_SIZE;


	/*block free*/
	if (block_bitmap[blocknumber] == 1)
	{
		/*modify block bitmap*/
		block_bitmap[blocknumber] = 0;

		fseek(fsfp, BlockBitmap_StartAddr + blocknumber, SEEK_SET);
		fwrite(&block_bitmap[blocknumber], sizeof(bool), 1, fsfp);
		fflush(fsfp);

		/*modify group info*/
		fseek(fsfp, Datablock_StartAddr + (blocknumber / BLOCKS_PER_GRP)*BLOCKS_PER_GRP*BLOCK_SIZE, SEEK_SET);
		fread(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);

		blockgroup_info.freeblock_remain++;
		fseek(fsfp, Datablock_StartAddr + (blocknumber / BLOCKS_PER_GRP)*BLOCKS_PER_GRP*BLOCK_SIZE, SEEK_SET);
		fwrite(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);
		fflush(fsfp);

		/*modify superblock*/
		sblock.s_free_block_num++;

		fseek(fsfp, sblock.s_free[0], SEEK_SET);
		fread(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);

		if (sblock.s_free_top_block != blockgroup_info.freeblock_remain)
		{
			sblock.s_free_top_block = blockgroup_info.freeblock_remain;
		}
		fseek(fsfp, Superblock_StartAddr, SEEK_SET);
		fwrite(&sblock, sizeof(SuperBlock), 1, fsfp);
		fflush(fsfp);

		return true;
	}
	else
	{
		return true;
	}
}

bool
mkdir(unsigned int parentinodeAddr, const char *name)
/*create a new directory */
/*process include:  */
/***************** 1. permission check*/
/***************** 2. create a new diritem and associated information*/
{
	if (strlen(name) >= DIR_NAME_SIZE_LIMIT)
	{
		fprintf(stderr, "dir name too long \n");
		return false;
	}

	/*read curent inode info*/
	fseek(fsfp, parentinodeAddr, SEEK_SET);
	fread(&inode, sizeof(Inode), 1, fsfp);

	int i = 0;
	//int j = 0;
	int cinode = 0;
	int cblock = 0;
	//int found = 0;
	//int cnt = 0;
	int blocknumber = 0;

	/*if (inode.i_cnt < 13)
	{
		for (i = 0; i < DIRECT_BLOCK_NUM; i++)
		{
			if (inode.i_direct_block[i] != 0)
			{
				fseek(fsfp, inode.i_direct_block[i], SEEK_SET);
				fread(dir, sizeof(dir), 1, fsfp);
				if (strcmp(dir[0].dirname, name) == 0)
				{
					printf("this name is already exists\n");
					return false;
				}
			}
			else
				found = i;

		}

		if ((cinode = ialloc()) != -1)
		{
			if ((cblock = balloc()) != -1)
			{
				inode.i_direct_block[found] = cblock;
				fseek(fsfp, parentinodeAddr, SEEK_SET);
				fwrite(&inode, sizeof(Inode), 1, fsfp);
				Inode tmpinode;
				tmpinode.i_ino = (cinode - Inode_StartAddr) / INODE_SIZE;
			}
		}
	}*/

	/*permission check*/
	/*write permission check*/
	if (userinfo.uid != 0)	/*not root user*/
	{
		/*user check*/
		if (inode.i_uid == userinfo.uid)
		{
			if ((inode.i_mode.USR_MODE & USR_WR) != 2)
			{
				printf("Permission denied\n");
				return false;
			}
		}
		/*group check*/
		if (inode.i_gid == userinfo.gid && inode.i_uid != userinfo.uid)
		{
			if ((inode.i_mode.GRP_MODE & GRP_WR) != 2)
			{
				printf("Permission denied\n");
				return false;
			}
		}
		/*other check*/
		if (inode.i_uid != userinfo.uid && inode.i_gid != userinfo.gid)
		{
			if ((inode.i_mode.OTH_MODE & OTH_WR) != 2)
			{
				printf("Permission denied\n");
				return false;
			}
		}
	}

	/*check pass*/

	/**************/
	/*if directory full*/
	if (inode.i_cnt == DIR_ITEM_NUM)
	{
		printf("this directory can't store more file\n");
		return false;
	}

	/*read curent diritem*/
	fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
	fread(dir, sizeof(dir), 1, fsfp);


	for (i = 2; i < inode.i_cnt; i++)
	{
		if (strcmp(dir[i].dirname, name) == 0)
		{
			fprintf(stderr, "this name is already exists\n");
			return false;
		}
	}
	if ((inode.i_cnt + 1) <= DIR_ITEM_NUM)
	{
		if ((cinode = ialloc()) != -1)
		{
			if ((cblock = balloc()) != -1)
			{
				/*creat new dir's inode, diritem*/

				/*create inode info*/
				Inode tmpinode;
				tmpinode.i_ino = (cinode - Inode_StartAddr) / INODE_SIZE;
				tmpinode.i_cnt = 2;
				tmpinode.i_mode.TYPE = DIR_MODE;
				tmpinode.i_mode.USR_MODE = USR_RD | USR_WR | USR_EX;
				tmpinode.i_mode.GRP_MODE = GRP_RD | GRP_EX;
				tmpinode.i_mode.OTH_MODE = OTH_RD | OTH_EX;
				tmpinode.i_uid = userinfo.uid;
				tmpinode.i_gid = userinfo.gid;
				tmpinode.i_size = BLOCK_SIZE;
				tmpinode.i_ctime = time(NULL);
				tmpinode.i_atime = time(NULL);
				tmpinode.i_mtime = time(NULL);
				tmpinode.i_direct_block[0] = cblock;
				for (i = 1; i < DIRECT_BLOCK_NUM; i++)
					tmpinode.i_direct_block[i] = -1;
				tmpinode.i_indirect_block = -1;
				tmpinode.i_indirect2_block = -1;
				tmpinode.i_indirect3_block = -1;

				/*write inode*/
				fseek(fsfp, cinode, SEEK_SET);
				fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
				fflush(fsfp);

				/*creat diritem*/
				DirItem tmpdir[DIR_ITEM_NUM];
				strcpy(tmpdir[0].dirname, name);			/*self*/
				tmpdir[0].inode_addr = cinode;		/*=cinode*/
				strcpy(tmpdir[1].dirname, dir[0].dirname);	/*parent dir*/
				tmpdir[1].inode_addr = dir[0].inode_addr;			/*parent inode addr*/

				/*write diritem*/
				fseek(fsfp, cblock, SEEK_SET);
				fwrite(tmpdir, sizeof(tmpdir), 1, fsfp);
				fflush(fsfp);

				/****************************************/
				/*modify parentinode and diritem*/
				strcpy(dir[inode.i_cnt++].dirname, name);
				dir[inode.i_cnt - 1].inode_addr = cinode;
				inode.i_ctime = time(NULL);
				inode.i_atime = time(NULL);
				inode.i_mtime = time(NULL);

				/*write back parentinode*/
				fseek(fsfp, parentinodeAddr, SEEK_SET);
				fwrite(&inode, sizeof(Inode), 1, fsfp);
				fflush(fsfp);

				/*write back parentdiritem*/
				fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
				fwrite(dir, sizeof(dir), 1, fsfp);
				fflush(fsfp);

				/***************/
				return true;
			}
		}
	}
	else
	{
		printf("unknown error\n");
		return false;
	}
}

int
cd(unsigned int curinodeAddr, const char *path)
/*change directory according to #path*/
/*process include:  */
/***************** 1. permission check*/
/***************** 2. modify the currentdir of struct userinfo*/
{
	/*read current inodeAddr*/
	fseek(fsfp, curinodeAddr, SEEK_SET);
	fread(&inode, sizeof(Inode), 1, fsfp);



	int i = 0;
	int j = 0;

	///*dir "/root" check*/
	//if ((strcmp(path, "/root") == 0) && userinfo.uid != 0)
	//{
	//	printf("Permission denied\n");
	//	exit(1);
	//}


	/*read diritem*/
	fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
	fread(dir, sizeof(dir), 1, fsfp);

	///*home check*/
	//if ((strcmp(dir[1].dirname, "home") == 0) && userinfo.uid != 0 && (strcmp(userinfo.user_home, "path") != 0))
	//{
	//	printf("Permission denied\n");
	//	exit(1);
	//}

	for (i = 1; i < inode.i_cnt; i++)
	{
		if (strcmp(dir[i].dirname, path) == 0 || strcmp(path, "..") == 0)	/*found*/
			break;
	}
	if (i < inode.i_cnt)		/*if found*/
	{
		/*directory permission check*/
		Inode tmpinode;
		if (strcmp(path, "..") == 0)
		{
			i = 1;
		}
		/*read inode info of #path*/
		fseek(fsfp, dir[i].inode_addr, SEEK_SET);
		fread(&tmpinode, sizeof(Inode), 1, fsfp);



		/*file type check*/
		if (tmpinode.i_mode.TYPE != DIR_MODE)
		{
			printf("no directory named %s", path);
			return -1;
		}

		if (userinfo.uid != 0)	/*not root user*/
		{
			/*user check*/
			if (inode.i_uid == userinfo.uid)
			{
				if ((inode.i_mode.USR_MODE & USR_EX) != 1)
				{
					printf("Permission denied\n");
					return -1;
				}
			}
			/*group check*/
			if (inode.i_gid == userinfo.gid && inode.i_uid != userinfo.uid)
			{
				if ((inode.i_mode.GRP_MODE & GRP_EX) != 1)
				{
					printf("Permission denied\n");
					return -1;
				}
			}
			/*other check*/
			if (inode.i_uid != userinfo.uid && inode.i_gid != userinfo.gid)
			{
				if ((inode.i_mode.OTH_MODE & OTH_EX) != 1)
				{
					printf("Permission denied\n");
					return -1;
				}
			}
		}

		/*check pass*/

		/*modify userinfo*/
		if (strcmp(path, "..") == 0)
		{
			/*modify currentdir path of userinfo*/

			for (j = strlen(userinfo.currentdir) - 1; userinfo.currentdir[j] != '/' && j >= 0; j--);		/*delete current dirname*/

			userinfo.currentdir[j + 1] = '\0';

			if (userinfo.currentdir[j] == '/' && strlen(userinfo.currentdir) > 1)
			{
				userinfo.currentdir[j] = '\0';
			}
		}
		else
		{
			if (strlen(userinfo.currentdir) + strlen(path) >= PATH_LEN - 1)/*longer than currentdir buf*/
			{
				printf("path too long\n");
				return -1;
			}
			if (userinfo.currentdir[strlen(userinfo.currentdir) - 1] != '/')
			{
				strcat(userinfo.currentdir, "/");
				strcat(userinfo.currentdir, path);
			}
			else
			{
				strcat(userinfo.currentdir, path);
			}

		}

		/*modify current inode info*/
		inode.i_atime = time(NULL);

		/*write back current inode*/
		fseek(fsfp, curinodeAddr, SEEK_SET);
		fwrite(&inode, sizeof(Inode), 1, fsfp);
		fflush(fsfp);
		/*************************/
		return dir[i].inode_addr;
	}
	else
	{
		printf("no directory named %s\n", path);
		fflush(stdout);
		return -1;
	}
}

bool
writeblock(unsigned int dirinodeAddr, const char buf[], const char *name)
/*write data to datablock */
/*process include:  */
/***************** 1. permission check*/
/***************** 2. write data*/
/***************** 3. write new file inode*/
/***************** 4. modify parent inode and diritem*/
{
	if (strlen(name) >= FILE_NAME_SIZE_LIMIT)
	{
		printf("file name too long\n");
		return false;
	}

	int i = 0;
	int j = 0;
	int cinode = 0;
	int cblock = 0;

	/*read current dir's inode*/
	fseek(fsfp, dirinodeAddr, SEEK_SET);
	fread(&inode, sizeof(Inode), 1, fsfp);


	/*permission check*/
	/*write permission check*/
	if (userinfo.uid != 0)	/*not root user*/
	{
		/*user check*/
		if (inode.i_uid == userinfo.uid)
		{
			if ((inode.i_mode.USR_MODE & USR_WR) != 2)
			{
				printf("Permission denied\n");
				return false;
			}
		}
		/*group check*/
		if (inode.i_gid == userinfo.gid && inode.i_uid != userinfo.uid)
		{
			if ((inode.i_mode.GRP_MODE & GRP_WR) != 2)
			{
				printf("Permission denied\n");
				return false;
			}
		}
		/*other check*/
		if (inode.i_uid != userinfo.uid && inode.i_gid != userinfo.gid)
		{
			if ((inode.i_mode.OTH_MODE & OTH_WR) != 2)
			{
				printf("Permission denied\n");
				return false;
			}
		}
	}

	/*check pass*/

	/************************/
	if (inode.i_cnt == DIR_ITEM_NUM)
	{
		printf("this directory can't store more file\n");
		return false;
	}

	/*read current diritem*/
	fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
	fread(dir, sizeof(dir), 1, fsfp);


	for (i = 2; i < inode.i_cnt; i++)
	{
		if (strcmp(dir[i].dirname, name) == 0)
		{
			fprintf(stderr, "this name is already exists\n");
			return false;
		}
	}

	if ((inode.i_cnt + 1) <= DIR_ITEM_NUM)
	{
		if ((cinode = ialloc()) != -1)
		{
			if ((cblock = balloc()) != -1)
			{
				/*create new file's inode */

				/*create inode info*/
				Inode tmpinode;
				tmpinode.i_ino = (cinode - Inode_StartAddr) / INODE_SIZE;
				tmpinode.i_cnt = 1;
				tmpinode.i_mode.TYPE = FILE_MODE;
				tmpinode.i_mode.USR_MODE = USR_RD | USR_WR;
				tmpinode.i_mode.GRP_MODE = GRP_RD | GRP_WR;
				tmpinode.i_mode.OTH_MODE = OTH_RD;
				tmpinode.i_uid = userinfo.uid;
				tmpinode.i_gid = userinfo.gid;
				tmpinode.i_size = strlen(buf);
				tmpinode.i_ctime = time(NULL);
				tmpinode.i_atime = time(NULL);
				tmpinode.i_mtime = time(NULL);
				tmpinode.i_direct_block[0] = cblock;
				for (j = 1; j < DIRECT_BLOCK_NUM; j++)
					tmpinode.i_direct_block[j] = -1;
				tmpinode.i_indirect_block = -1;
				tmpinode.i_indirect2_block = -1;
				tmpinode.i_indirect3_block = -1;

				/*write buf to block*/
				//memory space needed 
				unsigned int need = (tmpinode.i_size % BLOCK_SIZE == 0) ? ((tmpinode.i_size / BLOCK_SIZE)*BLOCK_SIZE) : ((tmpinode.i_size / BLOCK_SIZE + 1)*BLOCK_SIZE);

				if (need <= DIRECT_BLOCK_NUM * BLOCK_SIZE)
				{
					/*write datablock*/
					for (j = 0; j < need / BLOCK_SIZE; j++)
					{
						fseek(fsfp, cblock, SEEK_SET);
						fwrite(buf + j * BLOCK_SIZE, sizeof(char), BLOCK_SIZE, fsfp);
						fflush(fsfp);
						tmpinode.i_direct_block[j] = cblock;
						if ((j + 1) < need / BLOCK_SIZE)
						{
							cblock = balloc();
						}
					}

					/*write new inode*/
					fseek(fsfp, cinode, SEEK_SET);
					fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
					fflush(fsfp);
				}
				else if (need <= (DIRECT_BLOCK_NUM * BLOCK_SIZE + (BLOCK_SIZE / 4)*BLOCK_SIZE))
				{
					for (j = 0; j < need / BLOCK_SIZE; j++)
					{
						fseek(fsfp, cblock, SEEK_SET);
						fwrite(buf + j * BLOCK_SIZE, sizeof(char), BLOCK_SIZE, fsfp);
						fflush(fsfp);
						tmpinode.i_direct_block[j] = cblock;
						if ((j + 1) < need / BLOCK_SIZE)
						{
							cblock = balloc();
						}
					}
					tmpinode.i_indirect_block = balloc();
					for (j = 0; j < need / BLOCK_SIZE - 12; j++)
					{
						memset(ib_addr, 0, sizeof(ib_addr));
						fseek(fsfp, cblock, SEEK_SET);
						fwrite(buf + (j + 12) * BLOCK_SIZE, sizeof(char), BLOCK_SIZE, fsfp);
						fflush(fsfp);
						ib_addr[j].indirect_block_addr = cblock;
						if ((j + 1) < need / BLOCK_SIZE - 12)
						{
							cblock = balloc();
						}
					}

					/*write new inode*/
					fseek(fsfp, cinode, SEEK_SET);
					fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
					fflush(fsfp);

					/*write indirect_block*/
					fseek(fsfp, tmpinode.i_indirect_block, SEEK_SET);
					fwrite(ib_addr, sizeof(ib_addr), 1, fsfp);
					fflush(fsfp);
				}

#ifdef INDIRECT2_BLOCK_NUM
				else if (need <= (DIRECT_BLOCK_NUM * BLOCK_SIZE \
					+ (BLOCK_SIZE / 4)*BLOCK_SIZE)\
					+ (BLOCK_SIZE / 4)*(BLOCK_SIZE / 4)*BLOCK_SIZE)
				{

				}
#endif

#ifdef INDIRECT3_BLOCK_NUM
				else if (1)
				{

				}
#endif

				/*modify  parent inode diritem*/
				inode.i_mtime = time(NULL);
				inode.i_ctime = time(NULL);
				strcpy(dir[inode.i_cnt++].dirname, name);
				dir[inode.i_cnt - 1].inode_addr = cinode;

				/*write back parent inode*/
				fseek(fsfp, dirinodeAddr, SEEK_SET);
				fwrite(&inode, sizeof(Inode), 1, fsfp);
				fflush(fsfp);

				/*write back parent diritem*/
				fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
				fwrite(dir, sizeof(dir), 1, fsfp);
				fflush(fsfp);

				/***************/
				return true;
			}
		}
	}
	else
	{
		printf("unknown error\n");
		return false;
	}
}

bool
writeblock_exist(unsigned int dirinodeAddr, const char writebuf[], const char *target, int writemode)
{
	if (strlen(target) >= FILE_NAME_SIZE_LIMIT)
	{
		printf("no such file\n");
		return false;
	}

	int i = 0;
	int j = 0;
	int cblock = 0;

	/*read current dir inode*/
	fseek(fsfp, dirinodeAddr, SEEK_SET);
	fread(&inode, sizeof(Inode), 1, fsfp);

	/*permission check*/
	/*read and write permission check*/
	if (userinfo.uid != 0)	/*not root user*/
	{
		/*user check*/
		if (inode.i_uid == userinfo.uid)
		{
			if ((inode.i_mode.USR_MODE & USR_RD) != 4 || (inode.i_mode.USR_MODE & USR_WR) != 2)
			{
				printf("Permission denied\n");
				return false;
			}
		}
		/*group check*/
		if (inode.i_gid == userinfo.gid && inode.i_uid != userinfo.uid)
		{
			if ((inode.i_mode.GRP_MODE & GRP_RD) != 4 || (inode.i_mode.GRP_MODE & GRP_WR) != 2)
			{
				printf("Permission denied\n");
				return false;
			}
		}
		/*other check*/
		if (inode.i_uid != userinfo.uid && inode.i_gid != userinfo.gid)
		{
			if ((inode.i_mode.OTH_MODE & OTH_RD) != 4 || (inode.i_mode.OTH_MODE & OTH_WR) != 2)
			{
				printf("Permission denied\n");
				return false;
			}
		}
	}

	/*check pass*/

	/*read current dir item*/
	fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
	fread(dir, sizeof(dir), 1, fsfp);


	for (i = 2; i < inode.i_cnt; i++)
	{
		if (strcmp(dir[i].dirname, target) == 0)		/*found*/
			break;
	}

	if (i < inode.i_cnt)
	{
		/*read target inode*/
		fseek(fsfp, dir[i].inode_addr, SEEK_SET);
		fread(&inode, sizeof(Inode), 1, fsfp);

		unsigned int inodeaddr = dir[i].inode_addr;

		if (inode.i_mode.TYPE != DIR_MODE)	/*not dir*/
		{
			/*read target block*/
			/*memory size already exists*/
			unsigned int need = (inode.i_size % BLOCK_SIZE == 0) ? ((inode.i_size / BLOCK_SIZE)*BLOCK_SIZE) : ((inode.i_size / BLOCK_SIZE + 1)*BLOCK_SIZE);

			unsigned int newneed = ((strlen(writebuf) + inode.i_size) % BLOCK_SIZE == 0) ? (((strlen(writebuf) + inode.i_size) / BLOCK_SIZE)*BLOCK_SIZE) : (((strlen(writebuf) + inode.i_size) / BLOCK_SIZE + 1)*BLOCK_SIZE);

			char blockbuf[BLOCK_SIZE] = { 0 };
			switch (writemode)
			{
			case FILE_WRITE_OVERRIDE:	/*not finish*/
				if (need <= DIRECT_BLOCK_NUM * BLOCK_SIZE)
				{
				}
				else if (need <= (DIRECT_BLOCK_NUM * BLOCK_SIZE + (BLOCK_SIZE / 4)*BLOCK_SIZE))
				{
				}
#ifdef INDIRECT2_BLOCK_NUM
				else if (need <= (DIRECT_BLOCK_NUM * BLOCK_SIZE \
					+ (BLOCK_SIZE / 4)*BLOCK_SIZE)\
					+ (BLOCK_SIZE / 4)*(BLOCK_SIZE / 4)*BLOCK_SIZE)
				{

				}
#endif

#ifdef INDIRECT3_BLOCK_NUM
				else if (1)
				{

				}
#endif

				/************************/

				break;
			case FILE_WRITE_APPEND:
				if (newneed <= DIRECT_BLOCK_NUM * BLOCK_SIZE)
				{
					memset(blockbuf, 0, BLOCK_SIZE);
					for (i = DIRECT_BLOCK_NUM - 1; i >= 0; i--)
					{
						if (inode.i_direct_block[i] != -1)	/*found*/
							break;
					}
					if (i >= 0)	/*if found*/
					{
						/*read block*/
						fseek(fsfp, inode.i_direct_block[i], SEEK_SET);
						fread(blockbuf, sizeof(blockbuf), 1, fsfp);

						/*write blockappend*/
						fseek(fsfp, inode.i_direct_block[i] + strlen(blockbuf), SEEK_SET);
						fwrite(writebuf, sizeof(char), BLOCK_SIZE - strlen(blockbuf), fsfp);
						fflush(fsfp);
						cblock = balloc();
						for (i++, j = 0; i < newneed / BLOCK_SIZE; i++, j++)
						{
							fseek(fsfp, cblock, SEEK_SET);
							fwrite(writebuf + (BLOCK_SIZE - strlen(blockbuf)) + j * BLOCK_SIZE, sizeof(char), BLOCK_SIZE, fsfp);
							fflush(fsfp);
							inode.i_direct_block[i] = cblock;
							if ((i + 1) < need / BLOCK_SIZE)
							{
								cblock = balloc();
							}
						}
					}
					else
					{
						printf("unknow error\n");
						return false;
					}
				}
				else if (newneed <= (DIRECT_BLOCK_NUM * BLOCK_SIZE + (BLOCK_SIZE / 4)*BLOCK_SIZE))
				{
					memset(blockbuf, 0, BLOCK_SIZE);
					if (need <= DIRECT_BLOCK_NUM * BLOCK_SIZE)
					{
						for (i = DIRECT_BLOCK_NUM - 1; i >= 0; i--)
						{
							if (inode.i_direct_block[i] != -1)	/*found*/
								break;
						}
						if (i >= 0)	/*if found*/
						{
							/*read block*/
							fseek(fsfp, inode.i_direct_block[i], SEEK_SET);
							fread(blockbuf, sizeof(blockbuf), 1, fsfp);

							/*write blockappend*/
							fseek(fsfp, inode.i_direct_block[i] + strlen(blockbuf), SEEK_SET);
							fwrite(writebuf, sizeof(char), BLOCK_SIZE - strlen(blockbuf), fsfp);
							fflush(fsfp);
							cblock = balloc();
							for (i++, j = 0; i < newneed / BLOCK_SIZE; i++, j++)
							{
								fseek(fsfp, cblock, SEEK_SET);
								fwrite(writebuf + (BLOCK_SIZE - strlen(blockbuf)) + j * BLOCK_SIZE, sizeof(char), BLOCK_SIZE, fsfp);
								fflush(fsfp);
								inode.i_direct_block[i] = cblock;
								if ((i + 1) < need / BLOCK_SIZE)
								{
									cblock = balloc();
								}
							}
						}
						else
						{
							printf("unknow error\n");
							return false;
						}
						/*indirect block*/
						inode.i_indirect_block = balloc();
						for (i = 0; i < newneed / BLOCK_SIZE - 12; i++, j++)
						{
							memset(ib_addr, 0, sizeof(ib_addr));
							fseek(fsfp, cblock, SEEK_SET);
							fwrite(writebuf + (BLOCK_SIZE - strlen(blockbuf)) + j * BLOCK_SIZE, sizeof(char), BLOCK_SIZE, fsfp);
							fflush(fsfp);
							ib_addr[i].indirect_block_addr = cblock;
							if ((i + 1) < need / BLOCK_SIZE - 12)
							{
								cblock = balloc();
							}
						}

						/*write indirect_block*/
						fseek(fsfp, inode.i_indirect_block, SEEK_SET);
						fwrite(ib_addr, sizeof(ib_addr), 1, fsfp);
						fflush(fsfp);
					}
					else
					{
						/*read indirect_block*/
						fseek(fsfp, inode.i_indirect_block, SEEK_SET);
						fread(ib_addr, sizeof(ib_addr), 1, fsfp);
						fflush(fsfp);

						for (i = newneed / BLOCK_SIZE - 12; i >= 0; i--)
						{
							if (ib_addr[i].indirect_block_addr != -1)	/*found*/
								break;
						}
						if (i >= 0)	/*if found*/
						{
							/*read block*/
							fseek(fsfp, ib_addr[i].indirect_block_addr, SEEK_SET);
							fread(blockbuf, sizeof(blockbuf), 1, fsfp);

							/*write blockappend*/
							fseek(fsfp, ib_addr[i].indirect_block_addr + strlen(blockbuf), SEEK_SET);
							fwrite(writebuf, sizeof(char), BLOCK_SIZE - strlen(blockbuf), fsfp);
							fflush(fsfp);
							cblock = balloc();
							for (i++, j = 0; i < newneed / BLOCK_SIZE - 12; i++, j++)
							{
								fseek(fsfp, cblock, SEEK_SET);
								fwrite(writebuf + (BLOCK_SIZE - strlen(blockbuf)) + j * BLOCK_SIZE, sizeof(char), BLOCK_SIZE, fsfp);
								fflush(fsfp);
								ib_addr[i].indirect_block_addr = cblock;
								if ((i + 1) < need / BLOCK_SIZE)
								{
									cblock = balloc();
								}
							}
						}
						else
						{
							printf("unknow error\n");
							return false;
						}

						/*write indirect_block*/
						fseek(fsfp, inode.i_indirect_block, SEEK_SET);
						fwrite(ib_addr, sizeof(ib_addr), 1, fsfp);
						fflush(fsfp);
					}
				}
#ifdef INDIRECT2_BLOCK_NUM
				else if (need <= (DIRECT_BLOCK_NUM * BLOCK_SIZE \
					+ (BLOCK_SIZE / 4)*BLOCK_SIZE)\
					+ (BLOCK_SIZE / 4)*(BLOCK_SIZE / 4)*BLOCK_SIZE)
				{

				}
#endif

#ifdef INDIRECT3_BLOCK_NUM
				else if (1)
				{

				}
#endif

				/************************/
				/*modify inode and write back*/
				inode.i_ctime = time(NULL);
				inode.i_mtime = time(NULL);

				fseek(fsfp, inodeaddr, SEEK_SET);
				fwrite(&inode, sizeof(Inode), 1, fsfp);
				fflush(fsfp);

				return true;
				break;
			default:
				printf("write mode error\n");
				return false;
			}

		}
		else
		{
			printf("%s is a directory\n", target);
			return false;
		}
	}
	else
	{
		printf("no such file\n");
		return false;
	}
}

char *getnamebyID(char *namebuf, unsigned int id, const char *type)
/*need optimization*/
{
	int i = 0;
	int j = 0;
	if (strcmp(type, "uid") == 0)
	{
		/*first block is dir "/" diitem*/
		DirItem tmpdir[DIR_ITEM_NUM];
		fseek(fsfp, Datablock_StartAddr + BLOCK_SIZE, SEEK_SET);
		fread(tmpdir, sizeof(tmpdir), 1, fsfp);


		/*read dir "/" inode*/
		Inode tmpinode;
		fseek(fsfp, tmpdir[0].inode_addr, SEEK_SET);
		fread(&tmpinode, sizeof(Inode), 1, fsfp);

		unsigned int cnt = tmpinode.i_cnt;		/*dir "/" */
		for (i = 2; i < cnt; i++)
		{
			if (strcmp(tmpdir[i].dirname, "etc") == 0)
			{
				/*read "/etc" inode*/
				fseek(fsfp, tmpdir[i].inode_addr, SEEK_SET);
				fread(&tmpinode, sizeof(Inode), 1, fsfp);


				/*read "/etc" diritem*/
				fseek(fsfp, tmpinode.i_direct_block[0], SEEK_SET);
				fread(tmpdir, sizeof(tmpdir), 1, fsfp);

				unsigned int cnt1 = tmpinode.i_cnt;
				for (i = 2; i < cnt1; i++)
				{
					if (strcmp(tmpdir[i].dirname, "passwd") == 0)
					{
						/*read "/etc/passwd" inode*/
						fseek(fsfp, tmpdir[i].inode_addr, SEEK_SET);
						fread(&tmpinode, sizeof(Inode), 1, fsfp);


						/*read file "passwd"*/
						char tmpbuf[BLOCK_SIZE];
						fseek(fsfp, tmpinode.i_direct_block[0], SEEK_SET);
						fread(tmpbuf, sizeof(tmpbuf), 1, fsfp);


						/*find name matches uid*/
						char uidbuf1[10] = { 0 };
						char uidbuf2[10] = { 0 };
						char name[USR_NAME_SIZE_LIMIT] = { 0 };
						char linbuf[BLOCK_SIZE / 4] = { 0 };
						int j = 0;
						int k = 0;
						for (j = 0; tmpbuf != '\0'; j++)
						{
							memset(linbuf, 0, BLOCK_SIZE / 4);
							for (k = 0; tmpbuf[j] != '\n'; j++, k++)
							{
								linbuf[k] = tmpbuf[j];
							}
							for (k = 0; linbuf[k] != '\0'; k++)
							{
								if (k >= 1 && (linbuf[k - 1] == ':') && (linbuf[k + 1] == ':') && (linbuf[k] == 'x'))
								{
									int m = 0;
									int n = 0;

									memset(uidbuf1, 0, 10);
									memset(uidbuf2, 0, 10);
									for (m = k + 2, n = 0; linbuf[m] != ':'; m++, n++)
									{
										uidbuf1[n] = linbuf[m];
									}
									sprintf(uidbuf2, "%d", id);
									if (strcmp(uidbuf2, uidbuf1) == 0)
									{
										memset(name, 0, USR_NAME_SIZE_LIMIT);

										for (m = 0; m < k - 1; m++)
										{
											name[m] = linbuf[m];
										}
										strcpy(namebuf, name);
										return name;
									}
								}
							}

							if (tmpbuf[j] == '\n')
								j++;
						}
						return namebuf;
					}
				}
			}
		}
	}
	else if (strcmp(type, "gid") == 0)
	{
		/*first block is dir "/" diitem*/
		DirItem tmpdir[DIR_ITEM_NUM];
		fseek(fsfp, Datablock_StartAddr, SEEK_SET);
		fread(tmpdir, sizeof(tmpdir), 1, fsfp);


		/*read dir "/" inode*/
		Inode tmpinode;
		fseek(fsfp, tmpdir[0].inode_addr, SEEK_SET);
		fread(&tmpinode, sizeof(Inode), 1, fsfp);

		unsigned int cnt = tmpinode.i_cnt;
		for (i = 2; i < cnt; i++)
		{
			if (strcmp(tmpdir[i].dirname, "etc") == 0)
			{
				/*read "/etc" inode*/
				fseek(fsfp, tmpdir[i].inode_addr, SEEK_SET);
				fread(&tmpinode, sizeof(Inode), 1, fsfp);


				/*read "/etc" diritem*/
				fseek(fsfp, tmpinode.i_direct_block[0], SEEK_SET);
				fread(tmpdir, sizeof(tmpdir), 1, fsfp);

				unsigned int cnt1 = tmpinode.i_cnt;
				for (i = 2; i < cnt1; i++)
				{
					if (strcmp(tmpdir[i].dirname, "passwd") == 0)
					{
						/*read "/etc/passwd" inode*/
						fseek(fsfp, tmpdir[i].inode_addr, SEEK_SET);
						fread(&tmpinode, sizeof(Inode), 1, fsfp);


						/*read file "passwd"*/
						char tmpbuf[BLOCK_SIZE];
						fseek(fsfp, tmpinode.i_direct_block[0], SEEK_SET);
						fread(tmpbuf, sizeof(tmpbuf), 1, fsfp);


						/*find name matches uid*/
						char gidbuf1[10] = { 0 };
						char gidbuf2[10] = { 0 };
						char name[USR_NAME_SIZE_LIMIT] = { 0 };
						char linbuf[BLOCK_SIZE / 4] = { 0 };
						int j = 0;
						int k = 0;
						for (j = 0; tmpbuf != '\0'; j++)
						{
							memset(linbuf, 0, BLOCK_SIZE / 4);
							for (k = 0; tmpbuf[j] != '\n'; j++, k++)
							{
								linbuf[k] = tmpbuf[j];
							}
							for (k = 0; linbuf[k] != '\0'; k++)
							{
								if (k >= 1 && (linbuf[k - 1] == ':') && (linbuf[k + 1] == ':') && (linbuf[k] == 'x'))
								{
									int m = 0;
									int n = 0;
									memset(gidbuf1, 0, 10);
									memset(gidbuf2, 0, 10);
									for (m = k + 2; linbuf[m] != ':'; m++);
									for (m++, n = 0; linbuf[m] != ':'; m++, n++)
									{
										gidbuf1[n] = linbuf[m];
									}
									sprintf(gidbuf2, "%d", id);
									if (strcmp(gidbuf2, gidbuf1) == 0)
									{
										memset(name, 0, USR_NAME_SIZE_LIMIT);
										for (m = 0; m < k - 1; m++)
										{
											name[m] = linbuf[m];
										}
										strcpy(namebuf, name);
										return name;
									}
								}
							}

							if (tmpbuf[j] == '\n')
								j++;
						}
						return namebuf;
					}
				}
			}
		}
	}
}

void
ls(unsigned int curinodeAddr)
{
	/*read inode info*/
	fseek(fsfp, curinodeAddr, SEEK_SET);
	fread(&inode, sizeof(Inode), 1, fsfp);


	/*permission check*/
	/*read permission check*/
	if (userinfo.uid != 0)	/*not root user*/
	{
		/*user check*/
		if (inode.i_uid == userinfo.uid)
		{
			if ((inode.i_mode.USR_MODE & USR_RD) != 4)
			{
				printf("Permission denied\n");
				exit(1);
			}
		}
		/*group check*/
		if (inode.i_gid == userinfo.gid && inode.i_uid != userinfo.uid)
		{
			if ((inode.i_mode.GRP_MODE & GRP_RD) != 4)
			{
				printf("Permission denied\n");
				exit(1);
			}
		}
		/*other check*/
		if (inode.i_uid != userinfo.uid && inode.i_gid != userinfo.gid)
		{
			if ((inode.i_mode.OTH_MODE & OTH_RD) != 4)
			{
				printf("Permission denied\n");
				exit(1);
			}
		}
	}/*check pass*/

	/*read diritem info*/
	fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
	fread(dir, sizeof(dir), 1, fsfp);


	int i = 0;
	char dirname[DIR_NAME_SIZE_LIMIT] = { 0 };
	Inode tmpinode;

	/*list file or directory*/
	for (i = 0; i < inode.i_cnt; i++)
	{
		memset(dirname, 0, DIR_NAME_SIZE_LIMIT);
		if (i == 0)
		{
			strcpy(dirname, ".");

		}
		else if (i == 1)
		{
			strcpy(dirname, "..");
		}
		else
		{
			strcpy(dirname, dir[i].dirname);
		}

		/*read child inode info*/
		fseek(fsfp, dir[i].inode_addr, SEEK_SET);
		fread(&tmpinode, sizeof(Inode), 1, fsfp);


		/*TYPE: file or directory*/
		if ((tmpinode.i_mode.TYPE ^ DIR_MODE) == 0)			/*compare different by using XOR*/
		{
			printf("d");
		}
		else if ((tmpinode.i_mode.TYPE ^ FILE_MODE) == 0)	/*compare different by using XOR*/
		{
			printf("-");
		}
		/*permission: user group other*/
		/*user*/
		if ((tmpinode.i_mode.USR_MODE & USR_RD) == 4)
		{
			printf("r");
		}
		else
		{
			printf("-");
		}
		if ((tmpinode.i_mode.USR_MODE & USR_WR) == 2)
		{
			printf("w");
		}
		else
		{
			printf("-");
		}
		if ((tmpinode.i_mode.USR_MODE & USR_EX) == 1)
		{
			printf("x");
		}
		else
		{
			printf("-");
		}

		/*group*/
		if ((tmpinode.i_mode.GRP_MODE & GRP_RD) == 4)
		{
			printf("r");
		}
		else
		{
			printf("-");
		}
		if ((tmpinode.i_mode.GRP_MODE & GRP_WR) == 2)
		{
			printf("w");
		}
		else
		{
			printf("-");
		}
		if ((tmpinode.i_mode.GRP_MODE & GRP_EX) == 1)
		{
			printf("x");
		}
		else
		{
			printf("-");
		}
		/*other*/
		if ((tmpinode.i_mode.OTH_MODE & OTH_RD) == 4)
		{
			printf("r");
		}
		else
		{
			printf("-");
		}
		if ((tmpinode.i_mode.OTH_MODE & OTH_WR) == 2)
		{
			printf("w");
		}
		else
		{
			printf("-");
		}
		if ((tmpinode.i_mode.OTH_MODE & OTH_EX) == 1)
		{
			printf("x");
		}
		else
		{
			printf("-");
		}

		printf("\t");
		/*link number*/
		printf("%d\t", tmpinode.i_cnt);
		/*username*/
		char name[USR_NAME_SIZE_LIMIT] = { 0 };
		getnamebyID(name, tmpinode.i_uid, "uid");
		printf("%s\t", &name);/*not finish*/
							  /*group*/
		getnamebyID(name, tmpinode.i_gid, "gid");
		printf("%s\t", &name);/*not finish*/
							  /*size*/
		printf("%d\t", tmpinode.i_size);
		/*mtime*/
		tm *timep;
		timep = gmtime(&tmpinode.i_mtime);
		printf("%d-%d-%d %02d:%02d:%02d\t", timep->tm_mon + 1, timep->tm_mday, \
			1900 + timep->tm_year, (8 + timep->tm_hour) % 24, timep->tm_min, timep->tm_sec);
		/*name*/
		printf("%s\n", dirname);
	}

}

bool
rm(unsigned int parentinodeAddr, const char *name)
{
	if (strlen(name) >= DIR_NAME_SIZE_LIMIT)
	{
		printf("no such file\n");
		return false;
	}

	/*permission check*/
	/*write permission check*/
	if (userinfo.uid != 0)	/*not root user*/
	{
		if (inode.i_uid != userinfo.uid && inode.i_gid != userinfo.gid)
		{
			printf("Permission denied\n");
			return false;
		}
	}

	/*check pass*/

	/*read parent inode*/
	fseek(fsfp, parentinodeAddr, SEEK_SET);
	fread(&inode, sizeof(Inode), 1, fsfp);


	/*read diritem*/
	fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
	fread(dir, sizeof(dir), 1, fsfp);

	int i = 0;
	for (i = 2; i < inode.i_cnt; i++)
	{
		if (strcmp(dir[i].dirname, name) == 0)	/*found*/
			break;
	}
	if (i < inode.i_cnt)	/*if found*/
	{
		if (ifree((dir[i].inode_addr - Inode_StartAddr) / INODE_SIZE))
		{
			/*read parent inode*/
			fseek(fsfp, parentinodeAddr, SEEK_SET);
			fread(&inode, sizeof(Inode), 1, fsfp);

			/*read diritem*/
			fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
			fread(dir, sizeof(dir), 1, fsfp);

			/*remove file info from parent directory's diritem */
			for (i++; i < DIR_ITEM_NUM; i++)
			{
				if (strcmp(dir[i].dirname, "") != 0)
				{
					dir[i - 1].inode_addr = dir[i].inode_addr;
					strcpy(dir[i - 1].dirname, dir[i].dirname);
				}
				else
				{
					strcpy(dir[i - 1].dirname, "");
					dir[i - 1].inode_addr = 0x0;
					break;
				}
			}

			/*modify inode info*/
			inode.i_cnt--;
			inode.i_ctime = time(NULL);
			inode.i_mtime = time(NULL);

			/*write back inode*/
			fseek(fsfp, parentinodeAddr, SEEK_SET);
			fwrite(&inode, sizeof(Inode), 1, fsfp);
			fflush(fsfp);

			/*write back diritem*/
			fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
			fwrite(dir, sizeof(dir), 1, fsfp);
			fflush(fsfp);

			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		printf("no such file\n");
		return false;
	}
}

bool
rmdir(unsigned int parentinodeAddr, const char *name)
{
	if (strlen(name) >= DIR_NAME_SIZE_LIMIT)
	{
		printf("no such file\n");
		return false;
	}

	/*permission check*/
	/*write permission check*/
	if (userinfo.uid != 0)	/*not root user*/
	{
		if (inode.i_uid != userinfo.uid && inode.i_gid != userinfo.gid)
		{
			printf("Permission denied\n");
			return false;
		}
	}

	/*check pass*/

	/*read parent inode*/
	fseek(fsfp, parentinodeAddr, SEEK_SET);
	fread(&inode, sizeof(Inode), 1, fsfp);

	/*read diritem*/
	fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
	fread(dir, sizeof(dir), 1, fsfp);

	int i = 0;
	for (i = 2; i < inode.i_cnt; i++)
	{
		if (strcmp(dir[i].dirname, name) == 0)	/*found*/
			break;
	}
	if (i < inode.i_cnt)	/*if found*/
	{
		/*read inode of directory will be delete*/
		fseek(fsfp, dir[i].inode_addr, SEEK_SET);
		fread(&inode, sizeof(Inode), 1, fsfp);

		/*read diritem of directory will be delete*/
		fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
		fread(dir, sizeof(dir), 1, fsfp);

		for (i = 2; i < inode.i_cnt; i++)
		{

		}

		/**********************/
		if (ifree((dir[i].inode_addr - Inode_StartAddr) / INODE_SIZE))
		{
			/*read parent inode*/
			fseek(fsfp, parentinodeAddr, SEEK_SET);
			fread(&inode, sizeof(Inode), 1, fsfp);

			/*read diritem*/
			fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
			fread(dir, sizeof(dir), 1, fsfp);

			/*remove file info from parent directory's diritem */
			strcpy(dir[i].dirname, "");
			dir[i].inode_addr = 0x0;

			/*modify inode info*/
			inode.i_cnt--;
			inode.i_ctime = time(NULL);
			inode.i_mtime = time(NULL);

			/*write back inode*/
			fseek(fsfp, parentinodeAddr, SEEK_SET);
			fwrite(&inode, sizeof(Inode), 1, fsfp);
			fflush(fsfp);

			/*write back diritem*/
			fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
			fwrite(dir, sizeof(dir), 1, fsfp);
			fflush(fsfp);

			return true;
		}
		else
		{
			return false;
		}
	}
	else
	{
		printf("no such file\n");
		return false;
	}
}

bool
readblock(unsigned int dirinodeAddr, char readbuf[], const char *target)
{
	if (strlen(target) >= FILE_NAME_SIZE_LIMIT)
	{
		printf("no such file\n");
		return false;
	}

	/*read current dir inode*/
	fseek(fsfp, dirinodeAddr, SEEK_SET);
	fread(&inode, sizeof(Inode), 1, fsfp);

	/*permission check*/
	/*read permission check*/
	if (userinfo.uid != 0)	/*not root user*/
	{
		/*user check*/
		if (inode.i_uid == userinfo.uid)
		{
			if ((inode.i_mode.USR_MODE & USR_RD) != 4)
			{
				printf("Permission denied\n");
				return false;
			}
		}
		/*group check*/
		if (inode.i_gid == userinfo.gid)
		{
			if ((inode.i_mode.GRP_MODE & GRP_RD) != 4)
			{
				printf("Permission denied\n");
				return false;
			}
		}
		/*other check*/
		if (inode.i_uid != userinfo.uid && inode.i_gid != userinfo.gid)
		{
			if ((inode.i_mode.OTH_MODE & OTH_RD) != 4)
			{
				printf("Permission denied\n");
				return false;
			}
		}
	}

	/*check pass*/

	/*read current dir item*/
	fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
	fread(dir, sizeof(dir), 1, fsfp);

	int i = 0;
	for (i = 2; i < inode.i_cnt; i++)
	{
		if (strcmp(dir[i].dirname, target) == 0)	/*found*/
			break;
	}
	if (i < inode.i_cnt)	/*if found*/
	{
		/*read target inode*/
		fseek(fsfp, dir[i].inode_addr, SEEK_SET);
		fread(&inode, sizeof(Inode), 1, fsfp);

		if (inode.i_mode.TYPE != DIR_MODE)	/*not dir*/
		{
			/*read target block*/
			/*memory size to be read*/
			unsigned int need = (inode.i_size % BLOCK_SIZE == 0) ? ((inode.i_size / BLOCK_SIZE)*BLOCK_SIZE) : ((inode.i_size / BLOCK_SIZE + 1)*BLOCK_SIZE);
			char blockbuf[BLOCK_SIZE] = { 0 };
			if (need <= DIRECT_BLOCK_NUM * BLOCK_SIZE)
			{
				for (i = 0; i < DIRECT_BLOCK_NUM; i++)
				{
					memset(blockbuf, 0, BLOCK_SIZE);
					if (inode.i_direct_block[i] != -1)
					{
						/*read block*/
						fseek(fsfp, inode.i_direct_block[i], SEEK_SET);
						fread(blockbuf, sizeof(blockbuf), 1, fsfp);
						if (strlen(readbuf) + strlen(blockbuf) < FS_READ_BUF)
						{
							strcat(readbuf, blockbuf);
						}
						else
						{
							printf("readbuf is full\n");
							break;
						}
					}
				}
			}
			else if (need <= (DIRECT_BLOCK_NUM * BLOCK_SIZE + (BLOCK_SIZE / 4)*BLOCK_SIZE))
			{
				int fullflag = 0;
				for (i = 0; i < DIRECT_BLOCK_NUM; i++)
				{
					memset(blockbuf, 0, BLOCK_SIZE);
					if (inode.i_direct_block[i] != -1)
					{
						/*read block*/
						fseek(fsfp, inode.i_direct_block[i], SEEK_SET);
						fread(blockbuf, sizeof(blockbuf), 1, fsfp);
						if (strlen(readbuf) + strlen(blockbuf) < FS_READ_BUF)
						{
							strcat(readbuf, blockbuf);
						}
						else
						{
							printf("readbuf is full\n");
							fullflag = 1;
							break;
						}
					}
				}
				if (fullflag == 0)
				{
					/*read indirect addr block*/
					fseek(fsfp, inode.i_indirect_block, SEEK_SET);
					fread(ib_addr, sizeof(ib_addr), 1, fsfp);

					for (i = 0; i < BLOCK_SIZE / 4; i++)
					{
						memset(blockbuf, 0, BLOCK_SIZE);
						if (ib_addr[i].indirect_block_addr != -1)
						{
							/*read block*/
							fseek(fsfp, ib_addr[i].indirect_block_addr, SEEK_SET);
							fread(blockbuf, sizeof(blockbuf), 1, fsfp);
							if (strlen(readbuf) + strlen(blockbuf) < FS_READ_BUF)
							{
								strcat(readbuf, blockbuf);
							}
							else
							{
								printf("readbuf is full\n");
								break;
							}
						}

					}
				}

			}
#ifdef INDIRECT2_BLOCK_NUM
			else if (need <= (DIRECT_BLOCK_NUM * BLOCK_SIZE \
				+ (BLOCK_SIZE / 4)*BLOCK_SIZE)\
				+ (BLOCK_SIZE / 4)*(BLOCK_SIZE / 4)*BLOCK_SIZE)
			{

			}
#endif

#ifdef INDIRECT3_BLOCK_NUM
			else if (1)
			{

			}
#endif

			/*modify inode of read file*/
			inode.i_atime = time(NULL);

			/*write back inode of read file*/
			fseek(fsfp, dir[i].inode_addr, SEEK_SET);
			fwrite(&inode, sizeof(Inode), 1, fsfp);
			fflush(fsfp);

			return true;

		}
		else
		{
			printf("%s is a directory\n", target);
			return false;
		}
	}
	else
	{
		printf("no such file\n");
		return false;
	}
}

bool
chmod(unsigned int currentInodeAddr, const char mode[], const char *target)
{
	if (strlen(target) >= DIR_NAME_SIZE_LIMIT)
	{
		printf("no such file\n");
		return false;
	}
	/*read current inode info*/
	fseek(fsfp, currentInodeAddr, SEEK_SET);
	fread(&inode, sizeof(Inode), 1, fsfp);

	/*read current diritem*/
	fseek(fsfp, inode.i_direct_block[0], SEEK_SET);
	fread(dir, sizeof(dir), 1, fsfp);

	int i = 0;
	for (i = 2; i < inode.i_cnt; i++)
	{
		if (strcmp(dir[i].dirname, target) == 0)	/*found*/
			break;
	}

	if (i < inode.i_cnt)
	{
		Inode tmpinode;
		/*read target inode*/
		fseek(fsfp, dir[i].inode_addr, SEEK_SET);
		fread(&tmpinode, sizeof(Inode), 1, fsfp);

		if (strlen(mode) == 3)
		{
			if (isdigit(mode[0]))
			{
				if (isdigit(mode[1]))
				{
					if (isdigit(mode[2]))
					{
						tmpinode.i_mode.USR_MODE = atoi(&mode[0]);
						tmpinode.i_mode.GRP_MODE = atoi(&mode[1]);
						tmpinode.i_mode.OTH_MODE = atoi(&mode[2]);
						tmpinode.i_ctime = time(NULL);

						/*write back target inode*/
						fseek(fsfp, dir[i].inode_addr, SEEK_SET);
						fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
						fflush(fsfp);

						return true;
					}
					else
					{
						printf("mode error\n");
						return false;
					}
				}
				else
				{
					printf("mode error\n");
					return false;
				}
			}
			else
			{
				switch (mode[0])
				{
				case 'u':
					if (mode[1] == '+')
					{
						switch (mode[2])
						{
						case 'r':
							/*modify target inode*/
							tmpinode.i_mode.USR_MODE |= USR_RD;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'w':
							tmpinode.i_mode.USR_MODE |= USR_WR;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'x':
							tmpinode.i_mode.USR_MODE |= USR_EX;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						default:
							printf("mode error\n");
							return false;
						}
					}
					else if (mode[1] == '-')
					{
						switch (mode[2])
						{
						case 'r':
							/*modify target inode*/
							tmpinode.i_mode.USR_MODE -= USR_RD;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'w':
							tmpinode.i_mode.USR_MODE -= USR_WR;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'x':
							tmpinode.i_mode.USR_MODE -= USR_EX;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						default:
							printf("mode error\n");
							return false;
						}
					}
					else
					{
						printf("mode error\n");
						return false;
					}
					break;
				case 'g':
					if (mode[1] == '+')
					{
						switch (mode[2])
						{
						case 'r':
							/*modify target inode*/
							tmpinode.i_mode.GRP_MODE |= GRP_RD;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'w':
							tmpinode.i_mode.GRP_MODE |= GRP_WR;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'x':
							tmpinode.i_mode.GRP_MODE |= GRP_EX;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						default:
							printf("mode error\n");
							return false;
						}
					}
					else if (mode[1] == '-')
					{
						switch (mode[2])
						{
						case 'r':
							/*modify target inode*/
							tmpinode.i_mode.GRP_MODE -= GRP_RD;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'w':
							tmpinode.i_mode.GRP_MODE -= GRP_WR;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'x':
							tmpinode.i_mode.GRP_MODE -= GRP_EX;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						default:
							printf("mode error\n");
							return false;
						}
					}
					else
					{
						printf("mode error\n");
						return false;
					}
					break;
				case 'o':
					if (mode[1] == '+')
					{
						switch (mode[2])
						{
						case 'r':
							/*modify target inode*/
							tmpinode.i_mode.OTH_MODE |= OTH_RD;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'w':
							tmpinode.i_mode.OTH_MODE |= OTH_WR;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'x':
							tmpinode.i_mode.OTH_MODE |= OTH_EX;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						default:
							printf("mode error\n");
							return false;
						}
					}
					else if (mode[1] == '-')
					{
						switch (mode[2])
						{
						case 'r':
							/*modify target inode*/
							tmpinode.i_mode.OTH_MODE -= OTH_RD;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'w':
							tmpinode.i_mode.OTH_MODE -= OTH_WR;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						case 'x':
							tmpinode.i_mode.OTH_MODE -= OTH_EX;
							tmpinode.i_ctime = time(NULL);

							/*write back target inode*/
							fseek(fsfp, dir[i].inode_addr, SEEK_SET);
							fwrite(&tmpinode, sizeof(Inode), 1, fsfp);
							fflush(fsfp);

							return true;
							break;
						default:
							printf("mode error\n");
							return false;
						}
					}
					else
					{
						printf("mode error\n");
						return false;
					}

					break;
				default:
					printf("mode error\n");
					return false;
				}
			}
		}
		else if (strlen(mode) > 3)
		{
			/*not finish*/

		}
		else
		{
			printf("mode error\n");
			return false;
		}
	}
	else
	{
		printf("no found a file named %s\n", target);
		return false;
	}
}

bool
check(const char *username, const char *userpasswd)
{
	int i = 0;
	int j = 0;
	int foundflag = 0;

	/*first block is dir "/" diritem*/
	DirItem tmpdir[DIR_ITEM_NUM];
	fseek(fsfp, Datablock_StartAddr + BLOCK_SIZE, SEEK_SET);
	fread(tmpdir, sizeof(tmpdir), 1, fsfp);

	/*read dir "/" inode*/
	Inode tmpinode;
	fseek(fsfp, tmpdir[0].inode_addr, SEEK_SET);
	fread(&tmpinode, sizeof(Inode), 1, fsfp);

	Inode ipasswd;
	Inode ishadow;
	Inode igroup;

	unsigned int cnt = tmpinode.i_cnt;		/*dir "/"*/
	for (i = 2; i < cnt; i++)
	{
		if (strcmp(tmpdir[i].dirname, "etc") == 0)
		{
			/*read "/etc" inode*/
			fseek(fsfp, tmpdir[i].inode_addr, SEEK_SET);
			fread(&tmpinode, sizeof(Inode), 1, fsfp);

			/*read "/etc" diritem*/
			DirItem tmpdir2[DIR_ITEM_NUM];
			fseek(fsfp, tmpinode.i_direct_block[0], SEEK_SET);
			fread(tmpdir2, sizeof(tmpdir2), 1, fsfp);

			unsigned int cnt1 = tmpinode.i_cnt;		/*dir "/etc"*/
			for (j = 2; j < cnt1; j++)
			{
				if (strcmp(tmpdir2[j].dirname, "passwd") == 0)
				{
					/*read "/etc/passwd" inode*/
					fseek(fsfp, tmpdir2[j].inode_addr, SEEK_SET);
					fread(&ipasswd, sizeof(Inode), 1, fsfp);

					foundflag++;
				}
				else if (strcmp(tmpdir2[j].dirname, "group") == 0)
				{
					/*read "/etc/group" inode*/
					fseek(fsfp, tmpdir2[j].inode_addr, SEEK_SET);
					fread(&igroup, sizeof(Inode), 1, fsfp);

					foundflag++;
				}
				else if (strcmp(tmpdir2[j].dirname, "shadow") == 0)
				{
					/*read "/etc/shadow" inode*/
					fseek(fsfp, tmpdir2[j].inode_addr, SEEK_SET);
					fread(&ishadow, sizeof(Inode), 1, fsfp);

					foundflag++;
				}
			}
		}
		if (strcmp(tmpdir[i].dirname, "root") == 0 && strcmp(username, "root") == 0)
		{
			userinfo.currentInode = tmpdir[i].inode_addr;
		}
	}
	if (foundflag == 3)		/*three file all exists*/
	{
		foundflag = 0;

		/*read file "passwd"*/
		char tmpbuf[BLOCK_SIZE];
		fseek(fsfp, ipasswd.i_direct_block[0], SEEK_SET);
		fread(tmpbuf, sizeof(tmpbuf), 1, fsfp);


		/*find id matches name*/
		char uidbuf[10] = { 0 };
		char gidbuf[10] = { 0 };
		char name[USR_NAME_SIZE_LIMIT] = { 0 };
		char passwd[100] = { 0 };
		char linbuf[BLOCK_SIZE / 4] = { 0 };
		char homebuf[DIR_NAME_SIZE_LIMIT * 2] = { 0 };
		int j = 0;
		int k = 0;
		for (j = 0; tmpbuf[j] != '\0'; j++)
		{
			memset(linbuf, 0, BLOCK_SIZE / 4);
			/*get a line*/
			for (k = 0; tmpbuf[j] != '\n'; j++, k++)
			{
				linbuf[k] = tmpbuf[j];
			}

			for (k = 0; linbuf[k] != '\0'; k++)
			{
				if (k >= 1 && (linbuf[k - 1] == ':') && (linbuf[k + 1] == ':') && (linbuf[k] == 'x'))
				{
					int m = 0;
					int n = 0;
					memset(name, 0, USR_NAME_SIZE_LIMIT);
					for (m = 0, n = 0; m < k - 1; m++, n++)
					{
						/*get user name*/
						name[n] = linbuf[m];
					}

					if (strcmp(name, username) == 0)	/*user found*/
					{
						foundflag = 1;
						memset(uidbuf, 0, 10);
						memset(gidbuf, 0, 10);
						memset(homebuf, 0, 64);
						for (m = k + 2, n = 0; linbuf[m] != ':'; m++, n++)
						{
							uidbuf[n] = linbuf[m];
						}
						uidbuf[n + 1] = '\0';
						for (m = k + 3 + strlen(uidbuf), n = 0; linbuf[m] != ':'; m++, n++)
						{
							gidbuf[n] = linbuf[m];
						}
						gidbuf[n + 1] = '\0';
						for (m++; linbuf[m] != ':'; m++);
						for (m++, n = 0; linbuf[m] != ':'; m++, n++)
						{
							homebuf[n] = linbuf[m];
						}
						homebuf[n + 1] = '\0';

						strcpy(userinfo.user_name, name);
						sscanf(uidbuf, "%d", &userinfo.uid);
						sscanf(gidbuf, "%d", &userinfo.gid);
						strcpy(userinfo.user_home, homebuf);
						strcpy(userinfo.currentdir, homebuf);

						/*read file "shadow"*/
						fseek(fsfp, ishadow.i_direct_block[0], SEEK_SET);
						fread(tmpbuf, sizeof(tmpbuf), 1, fsfp);

						for (j = 0; tmpbuf[j] != '\0'; j++)
						{
							memset(linbuf, 0, BLOCK_SIZE / 4);
							memset(name, 0, USR_NAME_SIZE_LIMIT);
							for (k = 0; tmpbuf[j] != '\n'; j++, k++)
							{
								linbuf[k] = tmpbuf[j];
							}

							for (k = 0; linbuf[k] != ':'; k++)
							{
								name[k] = linbuf[k];
							}
							if (strcmp(name, userinfo.user_name) == 0)	/*passwd found*/
							{
								int m = 0;
								memset(passwd, 0, 100);
								for (m = 0, k++; linbuf[k] != '\0'; m++, k++)
								{
									passwd[m] = linbuf[k];
								}
								if (strcmp(passwd, userpasswd) == 0)	/*passwd right*/
								{
									return true;
								}
								else
								{
									return false;
								}
							}
							if (tmpbuf[j] == '\n')
								j++;
						}
					}
					break;
				}
			}

			if (tmpbuf[j] == '\n')
				j++;
		}
		if (foundflag == 0)			/*not found user record matches inputname*/
		{
			return false;
		}
	}
	else			/*not found all of three files*/
	{
		return false;
	}
}

bool
login()
{
	char username[USR_NAME_SIZE_LIMIT] = { 0 };
	char userpasswd[50] = { 0 };
	char ch;
	int len = 0;

	printf("username:");
	fflush(stdout);
	scanf("%s", username);
	printf("password:");
	fflush(stdout);
	getchar();
	while (ch = getchar())
	{
		if (ch == '\r' || ch == '\n')
		{
			break;
		}
		else if (len >= 50)
		{
			break;
		}
		else
		{
			putchar('*');
			userpasswd[len++] = ch;
		}
	}

	/*login check*/
	if (check(username, userpasswd))
	{
		return true;
	}
	else
	{
#ifdef WINDOWS
		system("cls");
#endif // WINDOWS

#ifdef UNIX
		system("clear");
#endif // UNIX
		return false;
	}

	/***/

}


void
do_command(const char *command)
{
	char c1[100] = { 0 };
	char c2[100] = { 0 };
	char c3[100] = { 0 };

	sscanf(command, "%s", c1);
	if (strcmp(c1, "ls") == 0)					/*ls*/
	{
		ls(userinfo.currentInode);
	}
	else if (strcmp(c1, "cd") == 0)				/*cd*/
	{
		int cdinode;
		sscanf(command, "%s%s", c1, c2);
		cdinode = cd(userinfo.currentInode, c2);
		if (cdinode == -1)
		{
			printf("change directory failed\n");
		}
		else
		{
			userinfo.currentInode = cdinode;
		}
	}
	else if (strcmp(c1, "mkdir") == 0)			/*mkdir*/
	{
		sscanf(command, "%s%s", c1, c2);
		if (!mkdir(userinfo.currentInode, c2))
		{
			printf("can't creat directory \"%s\" \n", c2);
		}
	}
	else if (strcmp(c1, "clear") == 0)			/*clear*/
	{
#ifdef WINDOWS
		system("cls");
#endif // WINDOWS

#ifdef UNIX
		system("clear");
#endif // UNIX
	}
	else if (strcmp(c1, "logout") == 0)			/*logout*/
	{
#ifdef WINDOWS
		system("cls");
#endif // WINDOWS

#ifdef UNIX
		system("clear");
#endif // UNIX
		userinfo.isLogin = false;
		printf("logout success\n");
	}
	else if (strcmp(c1, "chmod") == 0)			/*chmod*/
	{
		sscanf(command, "%s%s%s", c1, c2, c3);
		if (!chmod(userinfo.currentInode, c2, c3))
		{
			printf("change mode failed\n");
		}
	}
	else if (strcmp(c1, "touch") == 0)			/*touch*/
	{
		char tmpbuf[BLOCK_SIZE] = { 0 };
		sscanf(command, "%s%s", c1, c2);
		if (!writeblock(userinfo.currentInode, tmpbuf, c2))
		{
			printf("create new file failed\n");
		}
	}
	else if (strcmp(c1, "write") == 0)			/*write*/
	{
		memset(writebuf, 0, FS_WRITE_BUF);
		sscanf(command, "%s%s%s", c1, writebuf, c2);
		if (!writeblock(userinfo.currentInode, writebuf, c2))
		{
			printf("create new file failed\n");
		}
	}
	else if (strcmp(c1, "rm") == 0)				/*rm*/
	{
		sscanf(command, "%s%s", c1, c2);
		if (!rm(userinfo.currentInode, c2))
		{
			printf("delete file %s failed\n", c2);
		}
	}
	else if (strcmp(c1, "cat") == 0)			/*cat*/
	{
		sscanf(command, "%s%s", c1, c2);
		memset(readbuf, 0, strlen(readbuf));
		if (readblock(userinfo.currentInode, readbuf, c2))
		{
			printf("%s", readbuf);
		}
	}
	else if (strcmp(c1, "append") == 0)			/*append*/
	{
		memset(writebuf, 0, FS_WRITE_BUF);
		sscanf(command, "%s%s%s", c1, writebuf, c2);
		if (!writeblock_exist(userinfo.currentInode, writebuf, c2, FILE_WRITE_APPEND))
		{
			printf("add to file failed\n");
		}
	}
	else
	{
		printf("unknow command!\n");
	}

}

bool
init()
{
	int i = 0;
	int j = 0;
	int datablock_amount = 0;
	/*init buf*/
	memset(inode_bitmap, 0, sizeof(inode_bitmap));
	memset(block_bitmap, 0, sizeof(block_bitmap));

	datablock_amount = (BLCOK_NUM - 3 - (INODE_NUM / (BLOCK_SIZE / INODE_SIZE) + 1));
	/*init superblock*/
	sblock.s_inode_num = datablock_amount;
	sblock.s_block_num = datablock_amount;

	sblock.s_free_inode_num = datablock_amount;
	sblock.s_free_block_num = datablock_amount;
	sblock.s_block_per_group = BLOCKS_PER_GRP;

	sblock.s_first_data_block = Datablock_StartAddr;
	sblock.s_superblock_addr = Superblock_StartAddr;
	sblock.s_inode_addr = Inode_StartAddr;
	sblock.s_inode_bitmap = InodeBitmap_StartAddr;
	sblock.s_block_bitmap = BlockBitmap_StartAddr;

	/*link blocks*/
	sblock.s_free_top_block = BLOCKS_PER_GRP - 1;
	for (i = 0; i < BLOCKS_PER_GRP; i++)
	{
		sblock.s_free[i] = Datablock_StartAddr + i * BLOCK_SIZE;
	}
	/*write superblock*/
	fseek(fsfp, Superblock_StartAddr, SEEK_SET);
	fwrite(&sblock, sizeof(SuperBlock), 1, fsfp);
	fflush(fsfp);

	/*link remaining blocks*/
	for (i = datablock_amount / BLOCK_GROUP_NUM - 1; i >= 0; i--)
	{
		if (i == (datablock_amount / BLOCKS_PER_GRP - 1))
		{
			blockgroup_info.freeblock_remain = BLOCKS_PER_GRP - 1;
			for (j = 0; j < BLOCKS_PER_GRP; j++)
				blockgroup_info.freeblock[j] = -1;

			/*write blockgroup info to the first block of every blockgroup*/
			fseek(fsfp, Datablock_StartAddr + i * BLOCKS_PER_GRP*BLOCK_SIZE, SEEK_SET);
			fwrite(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);
			fflush(fsfp);

			/*modify block bitmap*/
			block_bitmap[i*BLOCKS_PER_GRP] = 1;
			fseek(fsfp, BlockBitmap_StartAddr + i * BLOCKS_PER_GRP, SEEK_SET);
			fwrite(&block_bitmap[i * BLOCKS_PER_GRP], sizeof(bool), 1, fsfp);
			fflush(fsfp);

		}
		else
		{
			blockgroup_info.freeblock_remain = BLOCKS_PER_GRP - 1;

			for (j = 0; j < BLOCKS_PER_GRP; j++)
			{
				blockgroup_info.freeblock[j] = Datablock_StartAddr + ((i + 1)*BLOCKS_PER_GRP + j)*BLOCK_SIZE;
			}

			fseek(fsfp, Datablock_StartAddr + i * BLOCKS_PER_GRP*BLOCK_SIZE, SEEK_SET);
			fwrite(&blockgroup_info, sizeof(BlockGroupInfo), 1, fsfp);
			fflush(fsfp);

			/*modify block bitmap*/
			block_bitmap[i*BLOCKS_PER_GRP] = 1;
			fseek(fsfp, BlockBitmap_StartAddr + i * BLOCKS_PER_GRP, SEEK_SET);
			fwrite(&block_bitmap[i * BLOCKS_PER_GRP], sizeof(bool), 1, fsfp);
			fflush(fsfp);
		}
	}

	/*init dir "/" */
	Inode iroot;
	strcpy(userinfo.currentdir, "/");

	int rootinode = ialloc();
	int rootblock = balloc();

	/*root diritem parentinode equs itself's inode*/
	strcpy(dir[0].dirname, "/");
	dir[0].inode_addr = rootinode;
	strcpy(dir[1].dirname, "/");
	dir[1].inode_addr = rootinode;

	/*init root inode info*/
	iroot.i_ino = (rootinode - Inode_StartAddr) / INODE_SIZE;
	iroot.i_cnt = 2;		/* ".." and "." */
	iroot.i_mode.TYPE = DIR_MODE;
	iroot.i_mode.USR_MODE = USR_RD | USR_WR | USR_EX;
	iroot.i_mode.GRP_MODE = GRP_RD | GRP_EX;
	iroot.i_mode.OTH_MODE = OTH_RD | OTH_EX;
	iroot.i_uid = 0; /*root uid*/
	iroot.i_gid = 0; /*root gid*/
	iroot.i_size = BLOCK_SIZE;
	iroot.i_ctime = time(NULL);
	iroot.i_atime = time(NULL);
	iroot.i_mtime = time(NULL);
	iroot.i_direct_block[0] = rootblock;
	for (i = 1; i < DIRECT_BLOCK_NUM; i++)
		iroot.i_direct_block[i] = -1;
	iroot.i_indirect_block = -1;
	iroot.i_indirect2_block = -1;
	iroot.i_indirect3_block = -1;

	/*write inode*/
	fseek(fsfp, rootinode, SEEK_SET);
	fwrite(&iroot, sizeof(Inode), 1, fsfp);
	fflush(fsfp);

	/*write diritem*/
	fseek(fsfp, rootblock, SEEK_SET);
	fwrite(dir, sizeof(dir), 1, fsfp);
	fflush(fsfp);


	/*create directory "/home", "/etc", "/root" in directory "/" */
	mkdir(rootinode, "home");
	mkdir(rootinode, "etc");
	mkdir(rootinode, "root");

	/*create passwd shadow group*/
	unsigned int cdInodeAddr;
	char tmpbuf[BLOCK_SIZE] = { 0 };
	cdInodeAddr = cd(rootinode, "etc");

	sprintf(tmpbuf, "root:x:0:0:root:/root:ext2sh\n");
	writeblock(cdInodeAddr, tmpbuf, "passwd");

	memset(tmpbuf, 0, BLOCK_SIZE);
	sprintf(tmpbuf, "root:root\n");
	writeblock(cdInodeAddr, tmpbuf, "shadow");

	memset(tmpbuf, 0, BLOCK_SIZE);
	sprintf(tmpbuf, "root::0:root,\n");
	writeblock(cdInodeAddr, tmpbuf, "group");

	/***************/
	return true;
}

int
main()
{
	char fsbuf[FILESYSTEM_SIZE] = { 0 };
	userinfo.isLogin = false;

	if ((fsfp = fopen("ext2.fs", "w+")) == NULL)
	{
		fprintf(stderr, "open fs file failed!");
		exit(1);
	}
	fwrite(fsbuf, sizeof(fsbuf), 1, fsfp);
	fflush(fsfp);

	userinfo.uid = 0x0;
	userinfo.gid = 0x0;
	strcpy(userinfo.user_name, "root");
	strcpy(userinfo.user_group, "root");
	strcpy(userinfo.user_home, "/root");
	strcpy(userinfo.currentdir, "/root");

	/*read first datablock*/
	fseek(fsfp, Datablock_StartAddr, SEEK_SET);
	fread(dir, sizeof(dir), 1, fsfp);
	fflush(fsfp);

	if (strcmp(dir[0].dirname, "/") == 0)
	{
		printf("welcome to ext2\n");
	}
	else
	{
		if (init())
		{
			printf("init finished\n");
			system("pause");
#ifdef WINDOWS
			system("cls");
#endif // WINDOWS

#ifdef UNIX
			system("clear");
#endif // UNIX


			printf("welcome to ext2\n");
		}
		else
		{
			printf("init failed\n");
			exit(1);
		}
	}
	while (!userinfo.isLogin)
	{
		userinfo.isLogin = login();
	}
	char cmd[100] = { 0 };

#ifdef WINDOWS
	system("cls");
#endif // WINDOWS

#ifdef UNIX
	system("clear");
#endif // UNIX
	while (1)
	{
		if (userinfo.isLogin)
		{
			memset(cmd, 0, 100);
			/*login shell*/
			if (strcmp(userinfo.user_home, userinfo.currentdir) == 0)
			{
				if (strcmp(userinfo.currentdir, "/root") == 0)
				{
					printf("%s@fs: %s#", userinfo.user_name, userinfo.currentdir);
				}
				else
				{
					printf("%s@fs: ~#", userinfo.user_name);
				}
			}
			else
			{
				printf("%s@fs: %s#", userinfo.user_name, userinfo.currentdir);
			}
#ifdef WINDOWS
			gets_s(cmd);	/*complier£ºmsvc2017*/
#endif // WINDOWS

#ifdef UNIX
			gets(cmd);	/*complier: gcc*/
#endif // UNIX


			do_command(cmd);
		}
		else
		{
			userinfo.isLogin = login();
		}
	}
	printf("%d \n", INODE_NUM);
	return 0;
}