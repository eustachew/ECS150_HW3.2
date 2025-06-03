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
	bool isOpen;
	char filename[16];
	uint32_t offset;
	uint32_t fileSize;
	uint16_t dataIndex;
};

uint16_t *FATArray;
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
	// Copy FAT from disk (FATArray contains more entries than data blocks so we can copy write whole pages back into disk later)
	FATArray = calloc(Block.numFATBlocks * (BLOCK_SIZE / 2), sizeof(uint16_t));
	for(int i = 0; i < Block.numFATBlocks; i++){ // loop through each FAT block
		if(block_read(i+1, buffer) != 0){
			return -1;
		}
		for(int j = 0; j < BLOCK_SIZE; j+=2){ // loop through each entry in FAT block
			if(((i*BLOCK_SIZE + j) / 2) >= Block.numDataBlocks){
				break;
			}
			memcpy(&FATArray[(i*BLOCK_SIZE + j) / 2], buffer + j, 2);
		}
	}

	//initialize the file descriptor array for later use when opening and closing files
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
		fdArray[i].isOpen = false;
	}
	mounted = true;
	return 0;
}

int fs_umount(void)
{
	/* TODO */
	// copy FAT array back into disk
	uint8_t buffer[BLOCK_SIZE];
	for(int i = 0; i < Block.numFATBlocks; i++){
		memcpy(buffer, FATArray+(i * (BLOCK_SIZE / 2)), BLOCK_SIZE);
		if(block_write(i+1, buffer) != 0){
			return -1;
		}
	}
	free(FATArray);
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
			return -1;
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
	// Check if filename is valid
	if(filename == NULL){
		return -1;
	}
	if(strlen(filename) >= FS_FILENAME_LEN){
		return -1;
	}
	// Check if filename already in root directory
	uint8_t buffer[BLOCK_SIZE];
	uint8_t rootEntry[32];
	char file[16];
	int offset = 0;

	if(block_read(Block.rootIndex, buffer) != 0){
		return -1;
	}

	while(offset < BLOCK_SIZE){
		memcpy(rootEntry, buffer + offset, 32);
		if(rootEntry[0] != '\0'){
			memcpy(file, rootEntry, 16);
			if(strcmp(file, filename) == 0){ // filename already exists in root directory
				return -1;
			}
		}
		offset += 32; //offset to the next 32 bytes, since each root directory entry is 32 bytes
	}
	// Check if there is empty entry. If there is, update that entry with filename
	offset = 0;
	uint32_t newfilesize = 0;
	uint16_t newfileindex = FAT_EOC;

	while(offset < BLOCK_SIZE){
		memcpy(rootEntry, buffer + offset, 32);
		if(rootEntry[0] == '\0'){
			memcpy(rootEntry, filename, strlen(filename)+1);
			memcpy(rootEntry + 16, &newfilesize, 4);
			memcpy(rootEntry + 20, &newfileindex, 2);

			memcpy(buffer + offset, rootEntry, 32);
			if(block_write(Block.rootIndex, buffer) != 0){
				return -1;
			}
			return 0;
		}
		offset += 32; //offset to the next 32 bytes, since each root directory entry is 32 bytes
	}
	return -1; // root directory full
}

int fs_delete(const char *filename)
{
	/* TODO: Phase 2 */
	// Check if FS is mounted
	if(!mounted){
		return -1;
	}
	// Check if filename is valid
	if(filename == NULL){
		return -1;
	}
	if(strlen(filename) >= FS_FILENAME_LEN){
		return -1;
	}
	// Check if filename is in root directory
	uint8_t buffer[BLOCK_SIZE];
	uint8_t rootEntry[32];
	char file[16];
	int offset = 0;
	bool fileInRoot = false;

	if(block_read(Block.rootIndex, buffer) != 0){
		return -1;
	}

	while(offset < BLOCK_SIZE){
		memcpy(rootEntry, buffer + offset, 32);
		if(rootEntry[0] != '\0'){
			memcpy(file, rootEntry, 16);
			if(strcmp(file, filename) == 0){ // filename in root directory
				fileInRoot = true;
				break;
			}
		}
		offset += 32; //offset to the next 32 bytes, since each root directory entry is 32 bytes
	}
	if(!fileInRoot){
		return -1;
	}
	// Check if file is currently open
	for (int i = 0; i < FS_OPEN_MAX_COUNT; i++) {
		if(fdArray[i].isOpen == false){ //fd entry has no corresponding file
			continue;
		}
		if(strcmp(fdArray[i].filename, filename) == 0){
			return -1;
		}
	}
	// Remove file from root directory (Set FAT entries of file to 0, set filename at file entry to '\0')
	// offset and rootEntry is from matching entry
	uint16_t curIndex;
	memcpy(&curIndex, rootEntry + 20, 2);

	uint16_t nextIndex = curIndex;
	while(curIndex < Block.numDataBlocks && FATArray[curIndex] != FAT_EOC){
		nextIndex = FATArray[curIndex];
		FATArray[curIndex] = 0;
		curIndex = nextIndex;
	}
	
	if(curIndex < Block.numDataBlocks && FATArray[curIndex] == FAT_EOC){
		FATArray[curIndex] = 0;
	}
	
	
	char nullChar = '\0';
	memcpy(buffer + offset, &nullChar, 1);
	if(block_write(Block.rootIndex, buffer) != 0){
		return -1;
	}
	return 0;
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
	//Exit if filestystem not mounted
	if(!mounted){
		return -1;
	}

	uint8_t buffer[BLOCK_SIZE];
	uint8_t rootEntry[32];
	int offset = 0;

	if(block_read(Block.rootIndex, buffer) != 0){
		return -1;
	}

	while(offset < BLOCK_SIZE){
		memcpy(&rootEntry, buffer + offset, 16); //Read the root directory entry
		if(strcmp((const char*)rootEntry, filename) == 0){ //find the directory entry that corresponds to th file that we are trying to open
			struct openFile newFile;
			memcpy(&newFile.fileSize, buffer + offset + 16, 4); //copy the "file size" section of the entry into the struct
			memcpy(&newFile.dataIndex, buffer + offset + 20, 2); //copy the index of the first data block into the struct
			strcpy(newFile.filename, filename);  
			newFile.offset = 0; //initial offset for a newly opened filed is 0

			for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){  //find the first open entry in the fd array to assign fd
				if(fdArray[i].isOpen == false){ //check to see if the entry is unused, if so then fill it in with the corresponding info
					fdArray[i] = newFile;
					fdArray[i].isOpen = true;
					return i; //return the fd value
				}
			}
			return -1; //No open fd entry was found
		}
		offset += 32; //offset to the next 32 bytes, since each root directory entry is 32 bytes
	}
	return -1;  //file was never found or fd array was already full
}

int fs_close(int fd)
{
	//Exit if filestystem not mounted
	if(!mounted){
		return -1;
	}
	if(fd > 31 || fd < 0){ //invalid fd value
		return -1;
	}
	if(fdArray[fd].isOpen == false){ //fd entry has no corresponding file
		return -1;
	}
	else if(fdArray[fd].isOpen == true){
		struct openFile emptyFile = {false, "\0", 0, 0, 0};
		fdArray[fd] = emptyFile;	//reseting the entry
		fdArray[fd].fileSize = 0; //making sure the values are resetted
		fdArray[fd].isOpen = false;
		fdArray[fd].offset = 0;
		fdArray[fd].dataIndex = 0;
	}
	
	return 0;
	/* TODO: Phase 3 */
}

int fs_stat(int fd)
{
	/* TODO: Phase 3 */
	//Exit if filestystem not mounted
	if(!mounted){
		return -1;
	}
	if(fd > 31 || fd < 0){ //invalid fd value
		return -1;
	}
	if(fdArray[fd].isOpen == false){ //fd entry has no corresponding file
		return -1;
	}
	
	return fdArray[fd].fileSize;

}

int fs_lseek(int fd, size_t offset)
{
	/* TODO: Phase 3 */
	//Exit if filestystem not mounted
	if(!mounted){
		return -1;
	}
	if(fd > 31 || fd < 0){ //invalid fd value
		return -1;
	}
	if(fdArray[fd].isOpen == false){ //fd entry has no corresponding file
		return -1;
	}
	if(offset > (size_t)fdArray[fd].fileSize){
		return -1;
	}
	if(offset + fdArray[fd].offset > (size_t)fdArray[fd].fileSize){
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
	if(fdArray[fd].isOpen == false){ //fd entry has no corresponding file
		return -1;
	}
	if(!mounted){ //Exit if filestystem not mounted
		return -1;
	}
	
	uint8_t bounce_buffer[BLOCK_SIZE];

	//If the requested amount to read is more than is left in the file, update the bytes to read to be whatever is left
	if(fdArray[fd].offset + count > fdArray[fd].fileSize){ 
		count = fdArray[fd].fileSize - fdArray[fd].offset;
	}

	int bytesToRead = count;
	int blockOffset = fdArray[fd].offset / 4096; //Offset within the array of data blocks that make up the file
	int internalOffset = fdArray[fd].offset % 4096; //Offset within the current block
	int bytesRead = 0; //Tracking how many bytes to read so we can use as offset into buf when using memcpy
	int blockIndex = fdArray[fd].dataIndex; //index of the block we are going to read from

	//Iterate through the FAT array until we are positioned at the right index 
	for(int i = 0; i < blockOffset; i++){
		blockIndex = FATArray[blockIndex];
	}


	while(bytesToRead > 0){
		block_read(blockIndex, bounce_buffer);
		if(4096 - internalOffset - bytesToRead > 0){ //If segment to read won't go to next block, read everything
			memcpy(buf + bytesRead, bounce_buffer + internalOffset, bytesToRead); 
			break;	//Exit loop since we finished reading the segment to read
		}
		else{ //Otherwise, if segment to read will go to next block, read however much is left of current block
			memcpy(buf + bytesRead, bounce_buffer + internalOffset, 4096 - internalOffset);
			bytesRead += 4096 - internalOffset;
			bytesToRead -= bytesRead;
			internalOffset = 0;
			blockIndex = FATArray[blockIndex];
		}
	}

	fdArray[fd].offset += count;
	return count;

}