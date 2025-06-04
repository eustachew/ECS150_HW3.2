#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "disk.h"
#include "fs.h"

#define FAT_EOC 0xFFFF
#define FILE_ENTRY_SIZE 32

bool mounted = false;

struct superblock{
	char signature[9]; //8 bytes for the signature, plus extra byte for null terminator
	uint16_t numTotalBlocks;
	uint16_t rootIndex;
	uint16_t dataIndex;
	uint16_t numDataBlocks;
	uint8_t numFATBlocks;
};

struct __attribute__((packed)) file_entry
{
	uint8_t filename[16];
	uint32_t file_size;
	uint16_t first_data_block_idx;
	uint8_t padding[10];
};

struct openFile{
	bool isOpen;
	char filename[16];
	uint32_t offset;
	uint32_t fileSize;
	uint16_t dataIndex;
};

uint16_t *FATArray;
struct file_entry root_dir[FS_FILE_MAX_COUNT];
struct openFile fdArray[FS_OPEN_MAX_COUNT];

struct superblock Block;

int fs_mount(const char *diskname)
{
	/* TODO */
	uint8_t buffer[BLOCK_SIZE];

	if (block_disk_open(diskname) == -1) {
		return -1;
	}

	if (block_read(0, buffer) == -1) {
		return -1;
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
		block_disk_close();
		return -1;
	}
	if(Block.numFATBlocks < Block.numDataBlocks * 2 / 4096){
		block_disk_close();
		return -1;
	}
	if(Block.numTotalBlocks != Block.numDataBlocks + Block.numFATBlocks + 2 || Block.numTotalBlocks != num_blocks){
		block_disk_close();
		return -1;
	}
	if(Block.rootIndex != Block.numFATBlocks + 1 || Block.dataIndex != Block.rootIndex + 1){
		block_disk_close();
		return -1;
	}
	// Copy FAT from disk (FATArray contains more entries than data blocks so we can copy write whole pages back into disk later)
	FATArray = calloc(Block.numFATBlocks * (BLOCK_SIZE / 2), sizeof(uint16_t));
	for(int i = 0; i < Block.numFATBlocks; i++){ // loop through each FAT block
		if(block_read(i+1, buffer) != 0){
			free(FATArray);
			block_disk_close();
			return -1;
		}
		for(int j = 0; j < BLOCK_SIZE; j+=2){ // loop through each entry in FAT block
			if(((i*BLOCK_SIZE + j) / 2) >= Block.numDataBlocks){
				break;
			}
			memcpy(&FATArray[(i*BLOCK_SIZE + j) / 2], buffer + j, 2);
		}
	}

	// copy root directory info from disk
	if(block_read(Block.rootIndex, buffer) != 0){
		free(FATArray);
		block_disk_close();
		return -1;
	}
	for (int i = 0; i < (FILE_ENTRY_SIZE*FS_FILE_MAX_COUNT); i+=FILE_ENTRY_SIZE) {
		memcpy(&root_dir[i/FILE_ENTRY_SIZE], &buffer[i], FILE_ENTRY_SIZE);
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
	// write root dir back into disk
	if(block_write(Block.rootIndex, root_dir) != 0){
		return -1;
	}
	if(block_disk_close() != 0){
		return -1;
	}
	mounted = false;
	return 0;
}

int fs_info(void)
{
	/* TODO */
	uint16_t fat_free_count = 0;
	for (int i = 0; i < Block.numDataBlocks; i++) {	
		if (FATArray[i] == 0) {
			fat_free_count++;
		}
	}
	
	//Reading root directory block into buffer, and seeing how many root directory entries are free
	int rdir_free_count = 0;
	for (int i = 0; i < FS_FILE_MAX_COUNT; i++) {
		if (root_dir[i].filename[0] == '\0') {
			rdir_free_count++;
		}
	}

	printf("FS Info:\n");
	printf("total_blk_count=%d\n", Block.numTotalBlocks);
	printf("fat_blk_count=%d\n", Block.numFATBlocks);
	printf("rdir_blk=%d\n", Block.rootIndex);
	printf("data_blk=%d\n", Block.dataIndex);
	printf("data_blk_count=%d\n", Block.numDataBlocks);
	printf("fat_free_ratio=%d/%d\n", fat_free_count, Block.numDataBlocks);
	printf("rdir_free_ratio=%d/%d\n", rdir_free_count, FS_FILE_MAX_COUNT);

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
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(strcmp((char*)root_dir[i].filename, filename) == 0){
			return -1;
		}
	}

	uint32_t newfilesize = 0;
	uint16_t newfileindex = FAT_EOC;
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(root_dir[i].filename[0] == '\0'){
			strncpy((char*)root_dir[i].filename, filename, 16);
			root_dir[i].file_size = newfilesize;
			root_dir[i].first_data_block_idx = newfileindex;
			return 0;
		}
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
	int index = -1;
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(strcmp((char*)root_dir[i].filename, filename) == 0){
			index = i;
			break;
		}
	}
	if(index == -1){
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

	root_dir[index].filename[0] = '\0';
	// Remove file from root directory (Set FAT entries of file to 0, set filename at file entry to '\0')
	// offset and rootEntry is from matching entry
	uint16_t curIndex = root_dir[index].first_data_block_idx;

	uint16_t nextIndex = curIndex;
	while(curIndex < Block.numDataBlocks && FATArray[curIndex] != FAT_EOC){
		nextIndex = FATArray[curIndex];
		FATArray[curIndex] = 0;
		curIndex = nextIndex;
	}
	
	if(curIndex < Block.numDataBlocks && FATArray[curIndex] == FAT_EOC){
		FATArray[curIndex] = 0;
	}

	return 0;
}

int fs_ls(void)
{
	// Check if FS is mounted
	if(!mounted){
		return -1;
	}	
	printf("FS Ls:\n");
	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(root_dir[i].filename[0] != '\0'){
			printf("file: %s, size: %u, data_blk: %u\n", root_dir[i].filename, root_dir[i].file_size, root_dir[i].first_data_block_idx);
		}
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
	if(filename == NULL){
		return -1;
	}
	if(strlen(filename) >= FS_FILENAME_LEN){
		return -1;
	}

	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(strcmp((char*)root_dir[i].filename, filename) == 0){
			struct openFile newFile;
			strncpy(newFile.filename, filename, 16);
			newFile.fileSize = root_dir[i].file_size;
			newFile.dataIndex = root_dir[i].first_data_block_idx;
			newFile.offset = 0;

			for(int i = 0; i < FS_OPEN_MAX_COUNT; i++){  //find the first open entry in the fd array to assign fd
				if(fdArray[i].isOpen == false){ //check to see if the entry is unused, if so then fill it in with the corresponding info
					fdArray[i] = newFile;
					fdArray[i].isOpen = true;
					return i; //return the fd value
				}
			}
			return -1; //No open fd entry was found
		}
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

uint16_t getEmptyFATIndex(){
	uint16_t index = 0;
	while(index < Block.numDataBlocks){
		if(FATArray[index] == 0){
			return index;
		}
		index++;
	}
	return 0;
}

int fs_write(int fd, void *buf, size_t count)
{
	/* TODO: Phase 4 */
	if(!mounted){
		return -1;
	}
	if(fd > 31 || fd < 0){
		return -1;
	}
	if(fdArray[fd].isOpen == false){
		return -1;
	}
	if(buf == NULL){
		return -1;
	}
	if(count == 0){
		return 0;
	}

	uint8_t bounce_buffer[BLOCK_SIZE];

	uint16_t bytesWritten = 0;
	uint16_t bytesLeftToWrite = count;
	int bytesToWriteInBlock = 0;
	int blockOffset = fdArray[fd].offset / BLOCK_SIZE; //Offset within the array of data blocks that make up the file
	int internalOffset = fdArray[fd].offset % BLOCK_SIZE; //Offset within the current block
	uint16_t dataBlockStartIndex = Block.dataIndex;
	uint16_t blockIndex = fdArray[fd].dataIndex;
	uint16_t emptyIndex = 0;
	int index = -1;

	for(int i = 0; i < FS_FILE_MAX_COUNT; i++){
		if(strcmp((char*)root_dir[i].filename, fdArray[fd].filename) == 0){
			index = i;
		}
	}

	if(index == -1){
		return -1;
	}

	// for writing into empty file
	if(blockIndex == FAT_EOC){
		emptyIndex = getEmptyFATIndex();
		if(emptyIndex == 0){
			return 0;
		}
		fdArray[fd].dataIndex = emptyIndex;
		blockIndex = emptyIndex;
		FATArray[emptyIndex] = FAT_EOC;
		root_dir[index].first_data_block_idx = emptyIndex;
	}


	//Iterate through the FAT array until we are positioned at the right index 
	for(int i = 0; i < blockOffset; i++){
		if(FATArray[blockIndex] == FAT_EOC){ // in the case of when offset is at beginning of new block (4096 for example)
			emptyIndex = getEmptyFATIndex();
			if(emptyIndex == 0){
				return 0;
			}
			FATArray[blockIndex] = emptyIndex;
		}
		blockIndex = FATArray[blockIndex];
	}

	while(bytesLeftToWrite > 0){
		block_read(dataBlockStartIndex + blockIndex, bounce_buffer);
		if(BLOCK_SIZE - internalOffset >= bytesLeftToWrite){ // last block to write
			bytesToWriteInBlock = bytesLeftToWrite;
			memcpy(buf + bytesWritten, bounce_buffer + internalOffset, bytesToWriteInBlock);
			block_write(dataBlockStartIndex + blockIndex, bounce_buffer);
			bytesWritten += bytesToWriteInBlock;
			bytesLeftToWrite -= bytesToWriteInBlock;
		}
		else{ // more bytes to write after this
			bytesToWriteInBlock = BLOCK_SIZE - internalOffset;
			memcpy(buf + bytesWritten, bounce_buffer + internalOffset, bytesToWriteInBlock);
			block_write(dataBlockStartIndex + blockIndex, bounce_buffer);
			bytesWritten += bytesToWriteInBlock;
			bytesLeftToWrite -= bytesToWriteInBlock;
			internalOffset = 0;
			if(FATArray[blockIndex] == FAT_EOC){ // in the case of when offset is at beginning of new block (4096 for example)
				uint16_t emptyIndex = getEmptyFATIndex();
				if(emptyIndex == 0){ // unable to extend bc no empty blocks available
					break;
				}
				FATArray[blockIndex] = emptyIndex;
			}
			blockIndex = FATArray[blockIndex];
		}
	}

	fdArray[fd].offset += bytesWritten;
	fdArray[fd].fileSize = (fdArray[fd].offset > fdArray[fd].fileSize) ? fdArray[fd].offset : fdArray[fd].fileSize; // max(fdArray[fd].offset, fdArray[fd].fileSize). offset will be greater than orignal fileSize if extended.
	root_dir[index].file_size = fdArray[fd].fileSize;
	return bytesWritten;
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
		block_read(blockIndex + Block.dataIndex, bounce_buffer);
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