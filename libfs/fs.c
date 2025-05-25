#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "disk.h"
#include "fs.h"

struct superblock{
	char signature[9]; //8 bytes for the signature, plus extra byte for null terminator
	uint16_t numTotalBlocks;
	uint16_t rootIndex;
	uint16_t dataIndex;
	uint16_t numDataBlocks;
	uint8_t numFATBlocks;
};

struct openFile{
	int fileDescriptor;
	int offset;
};

struct superblock Block;

int fs_mount(const char *diskname)
{
	/* TODO */
	uint8_t buffer[BLOCK_SIZE];

	block_disk_open(diskname);

	if(block_read(0, buffer) != 0){
		return 0;
	}

	//"Mounting" the disk by reading all the info we need
	memcpy(Block.signature, buffer, 8);
	memcpy(&Block.numTotalBlocks, buffer + 8, 2);
	memcpy(&Block.rootIndex, buffer + 10, 2);
	memcpy(&Block.dataIndex, buffer + 12, 2);
	memcpy(&Block.numDataBlocks, buffer + 14, 2);
	memcpy(&Block.numFATBlocks, buffer + 16, 1);

	//Making sure that the superblock contains the correct information and format
	Block.signature[8] = '\0';
	int num_blocks = block_disk_count();
	if(strcmp(Block.signature, "ECS150FS") != 0){
		return -1;
	}
	if(Block.numFATBlocks < Block.numDataBlocks * 2 / 4096){
		return -1;
	}
	if(Block.numTotalBlocks != Block.numDataBlocks + Block.numFATBlocks + 2 || Block.numTotalBlocks != num_blocks){
		return -1;
	}
	if(Block.rootIndex != Block.numFATBlocks + 1 || Block.dataIndex != Block.rootIndex + 1){
		return -1;
	}
	return 0;
}

int fs_umount(void)
{
	/* TODO */
	if(block_disk_close() != 0){
		return -1;
	}
	
	return 0;
}

int fs_info(void)
{
	/* TODO */

	uint8_t buffer[BLOCK_SIZE];
	int offset;
	int numOccupied = 0; //First FAT entry is always reserved from the start
	char FATEntry[2];

	//Reading the data blocks to see how many are free
	for(int numFAT = 1; numFAT <= Block.numFATBlocks; numFAT++){
		offset = 0;
		if(block_read(numFAT, buffer) != 0){
			return 0;
		}
		while(offset < BLOCK_SIZE){
			memcpy(FATEntry, buffer + offset, 2);
			if(*FATEntry != 0){
				numOccupied++;
			}
			offset += 2; //offset to the next 2 bytes since each FAT entry is 2 bytes
		}
	}
	
	//Reading root directory block into buffer, and seeing how many root directory entries are free
	if(block_read(Block.rootIndex, buffer) != 0){
		return 0;
	}

	offset = 0;
	uint8_t rootEntry[32];
	int numFreeEntries = FS_FILE_MAX_COUNT;

	while(offset < BLOCK_SIZE){
		memcpy(rootEntry, buffer + offset, 32); //Read the root directory entry
		if(*rootEntry != 0){
			numFreeEntries--;
		}
		offset += 32; //offset to the next 32 bytes, since each root directory entry is 32 bytes
	}

	printf("FS Info:\n");
	printf("total_blk_count=%d\n", Block.numTotalBlocks);
	printf("fat_blk_count=%d\n", Block.numFATBlocks);
	printf("rdir_blk=%d\n", Block.rootIndex);
	printf("data_blk=%d\n", Block.dataIndex);
	printf("data_blk_count=%d\n", Block.numDataBlocks);
	printf("fat_free_ratio=%d/%d\n", Block.numDataBlocks - numOccupied, Block.numDataBlocks);
	printf("rdir_free_ratio=%d/%d\n", numFreeEntries, FS_FILE_MAX_COUNT);

	return 0;
}

int fs_create(const char *filename)
{
	/* TODO: Phase 2 */
}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
}

int fs_ls(void)
{
	/* TODO: Phase 2 */
}

int fs_open(const char *filename)
{
	/* TODO: Phase 3 */

}

int fs_close(int fd)
{
	/* TODO: Phase 3 */
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}
