#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF

bool mounted = false;

struct superblock{
	char signature[9]; //8 bytes for the signature, plus extra byte for null terminator
	uint16_t numTotalBlocks;
	uint16_t rootIndex;
	uint16_t dataIndex;
	uint16_t numDataBlocks;
	uint8_t numFATBlocks;
};

struct openFile{
	char filename[16];
	int32_t offset;
	int32_t fileSize;
};

struct openFile fdArray[FS_OPEN_MAX_COUNT];

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
	mounted = true;

	//initialize the file descriptor array for later use when opening and closing files
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
		fdArray[i].fileSize = -1;
	}
	return 0;
}

int fs_umount(void)
{
	/* TODO */
	if(block_disk_close() != 0){
		return -1;
	}
	mounted = false;
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
		return -1;
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
	// Check if FS is mounted
	if(!mounted){
		return -1;
	}
	// Check if filename is null-terminated
	// Check if filename exceeds FS_FILENAME_LEN
	// Check if filename already in root directory
	// Check if there is empty entry. If there is, update that entry with filename
}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
}

int fs_ls(void)
{
	// Check if FS is mounted
	if(!mounted){
		return -1;
	}

	// loop through root directory and if filename != '\0', print file info
	uint8_t buffer[BLOCK_SIZE];
	uint8_t rootEntry[32];
	char filename[16];
	uint32_t filesize;
	uint16_t fileindex;
	int offset = 0;

	if(block_read(Block.rootIndex, buffer) != 0){
		return -1;
	}
	
	printf("FS Ls:\n");
	while(offset < BLOCK_SIZE){
		memcpy(rootEntry, buffer + offset, 32);
		if(rootEntry[0] != '\0'){
			memcpy(filename, rootEntry, 16);
			memcpy(&filesize, rootEntry + 16, 4);
			memcpy(&fileindex, rootEntry + 20, 2);
			printf("file: %s, size: %u, data_blk: %u\n", filename, filesize, fileindex);
		}
		offset += 32; //offset to the next 32 bytes, since each root directory entry is 32 bytes
	}
	return 0;
}

int fs_open(const char *filename)
{
	/* TODO: Phase 3 */
	uint8_t buffer[BLOCK_SIZE];
	uint8_t rootEntry[32];
	int offset = 0;

	if(block_read(Block.rootIndex, buffer) != 0){
		return -1;
	}

	while(offset < BLOCK_SIZE){
		memcpy(rootEntry, buffer + offset, 16); //Read the root directory entry
		if(strcmp(rootEntry, filename) == 0){ //find the directory entry that corresponds to th file that we are trying to open
			struct openFile newFile;
			memcpy(newFile.fileSize, buffer + offset + 16, 4); //copy the "file size" section of the entry into the struct
			strcpy(newFile.filename, filename);  
			newFile.offset = 0; //initial offset for a newly opened filed is 0

			for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){  //find the first open entry in the fd array to assign fd
				if(fdArray[i].fileSize == -1){ //check to see if the entry is unused, if so then fill it in with the corresponding info
					fdArray[i] = newFile;
					return i; //return the fd value
				}
			}
		}
		offset += 32; //offset to the next 32 bytes, since each root directory entry is 32 bytes
	}
	return -1;  //file was never found or fd array was already full
}

int fs_close(int fd)
{
	if(fd > 31 || fd < 0){ //invalid fd value
		return -1;
	}
	if(fdArray[fd].fileSize == -1){ //fd entry has no corresponding file
		return -1;
	}
	else if(fdArray[fd].fileSize != -1){
		struct openFile emptyFile;
		fdArray[fd] = emptyFile;	//reseting the entry
		fdArray[fd].fileSize = -1; //making sure the values are resetted
		fdArray[fd].offset = 0;
	}
	
	return 0;
	/* TODO: Phase 3 */
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
	if(fd > 31 || fd < 0){ //invalid fd value
		return -1;
	}
	if(fdArray[fd].fileSize == -1){ //fd entry has no corresponding file
		return -1;
	}
	
	return fdArray[fd].fileSize;

}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
	if(fd > 31 || fd < 0){ //invalid fd value
		return -1;
	}
	if(fdArray[fd].fileSize == -1){ //fd entry has no corresponding file
		return -1;
	}
	if(offset > fdArray[fd].fileSize){
		return -1;
	}

	fdArray[fd].offset += offset;
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}

int fs_read(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
}
