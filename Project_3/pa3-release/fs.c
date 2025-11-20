#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h> // Required for bool type
#include "fs.h"
#include "fs_util.h"
#include "disk.h"

// Declare external function from fs_sim.c
extern bool command(char *comm, char *comm2);

char inodeMap[MAX_INODE / 8];
char blockMap[MAX_BLOCK / 8];
Inode inode[MAX_INODE];
SuperBlock superBlock;
Dentry curDir;
int curDirBlock;

int fs_mount(char *name)
{
    int numInodeBlock =  (sizeof(Inode)*MAX_INODE)/ BLOCK_SIZE;
    int i, index, inode_index = 0;

    // load superblock, inodeMap, blockMap and inodes into the memory
    if(disk_mount(name) == 1) {
        disk_read(0, (char*) &superBlock);
        if(superBlock.magicNumber != MAGIC_NUMBER) {
            printf("Invalid disk!\n");
            exit(0);
        }
        disk_read(1, inodeMap);
        disk_read(2, blockMap);
        for(i = 0; i < numInodeBlock; i++)
        {
            index = i+3;
            disk_read(index, (char*) (inode+inode_index));
            inode_index += (BLOCK_SIZE / sizeof(Inode));
        }
        // root directory
        curDirBlock = inode[0].directBlock[0];
        disk_read(curDirBlock, (char*)&curDir);

    } else {
        // Init file system superblock, inodeMap and blockMap
        superBlock.magicNumber = MAGIC_NUMBER;
        superBlock.freeBlockCount = MAX_BLOCK - (1+1+1+numInodeBlock);
        superBlock.freeInodeCount = MAX_INODE;

        //Init inodeMap
        for(i = 0; i < MAX_INODE / 8; i++)
        {
            set_bit(inodeMap, i, 0);
        }
        //Init blockMap
        for(i = 0; i < MAX_BLOCK / 8; i++)
        {
            if(i < (1+1+1+numInodeBlock)) set_bit(blockMap, i, 1);
            else set_bit(blockMap, i, 0);
        }
        //Init root dir
        int rootInode = get_free_inode();
        curDirBlock = get_free_block();

        inode[rootInode].type = directory;
        inode[rootInode].owner = 0;
        inode[rootInode].group = 0;
        gettimeofday(&(inode[rootInode].created), NULL);
        gettimeofday(&(inode[rootInode].lastAccess), NULL);
        inode[rootInode].size = 1;
        inode[rootInode].blockCount = 1;
        inode[rootInode].directBlock[0] = curDirBlock;

        curDir.numEntry = 1;
        strncpy(curDir.dentry[0].name, ".", 1);
        curDir.dentry[0].name[1] = '\0';
        curDir.dentry[0].inode = rootInode;
        disk_write(curDirBlock, (char*)&curDir);
    }
    return 0;
}

int fs_umount(char *name)
{
    int numInodeBlock =  (sizeof(Inode)*MAX_INODE )/ BLOCK_SIZE;
    int i, index, inode_index = 0;
    disk_write(0, (char*) &superBlock);
    disk_write(1, inodeMap);
    disk_write(2, blockMap);

    for(i = 0; i < numInodeBlock; i++)
    {
        index = i+3;
        disk_write(index, (char*) (inode+inode_index));
        inode_index += (BLOCK_SIZE / sizeof(Inode));
    }
    // current directory
    disk_write(curDirBlock, (char*)&curDir);

    disk_umount(name);  
    return 0;
}

int search_cur_dir(char *name)
{
    // return inode. If not exist, return -1
    int i;

    for(i = 0; i < curDir.numEntry; i++)
    {
        if(command(name, curDir.dentry[i].name)) return curDir.dentry[i].inode;
    }
    return -1;
}

int file_create(char *name, int size)
{
    int i;

    if(size > SMALL_FILE) {
        printf("Do not support files larger than %d bytes.\n", SMALL_FILE);
        return -1;
    }

    if(size < 0){
        printf("File create failed: cannot have negative size\n");
        return -1;
    }

    int inodeNum = search_cur_dir(name); 
    if(inodeNum >= 0) {
        printf("File create failed:  %s exist.\n", name);
        return -1;
    }

    if(curDir.numEntry + 1 > MAX_DIR_ENTRY) {
        printf("File create failed: directory is full!\n");
        return -1;
    }

    int numBlock = size / BLOCK_SIZE;
    if(size % BLOCK_SIZE > 0) numBlock++;

    if(numBlock > superBlock.freeBlockCount) {
        printf("File create failed: data block is full!\n");
        return -1;
    }

    if(superBlock.freeInodeCount < 1) {
        printf("File create failed: inode is full!\n");
        return -1;
    }

    char *tmp = (char*) malloc(sizeof(int) * size + 1);

    rand_string(tmp, size);
    printf("New File: %s\n", tmp);

    // get inode and fill it
    inodeNum = get_free_inode();
    if(inodeNum < 0) {
        printf("File_create error: not enough inode.\n");
        free(tmp);
        return -1;
    }

    inode[inodeNum].type = file;
    inode[inodeNum].owner = 1;  // pre-defined
    inode[inodeNum].group = 2;  // pre-defined
    gettimeofday(&(inode[inodeNum].created), NULL);
    gettimeofday(&(inode[inodeNum].lastAccess), NULL);
    inode[inodeNum].size = size;
    inode[inodeNum].blockCount = numBlock;
    inode[inodeNum].link_count = 1;

    // add a new file into the current directory entry
    strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
    curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
    curDir.dentry[curDir.numEntry].inode = inodeNum;
    printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, name);
    curDir.numEntry++;

    // get data blocks
    for(i = 0; i < numBlock; i++)
    {
        int block = get_free_block();
        if(block == -1) {
            printf("File_create error: get_free_block failed\n");
            free(tmp);
            return -1;
        }
        //set direct block
        inode[inodeNum].directBlock[i] = block;

        disk_write(block, tmp+(i*BLOCK_SIZE));
    }

    //update last access of current directory
    gettimeofday(&(inode[curDir.dentry[0].inode].lastAccess), NULL);        

    printf("file created: %s, inode %d, size %d\n", name, inodeNum, size);

    free(tmp);
    return 0;
}

int file_cat(char *name)
{
    int inodeNum, i, size;
    char str_buffer[BLOCK_SIZE];
    char * str;

    //get inode
    inodeNum = search_cur_dir(name);
    if(inodeNum < 0)
    {
        printf("cat error: file not found\n");
        return -1;
    }
    
    size = inode[inodeNum].size;

    if(inode[inodeNum].type == directory)
    {
        printf("cat error: cannot read directory\n");
        return -1;
    }

    //allocate str
    str = (char *) malloc( sizeof(char) * (size+1) );
    str[ size ] = '\0';

    for( i = 0; i < inode[inodeNum].blockCount; i++ ){
        int block;
        block = inode[inodeNum].directBlock[i];

        disk_read( block, str_buffer );

        if( size >= BLOCK_SIZE )
        {
            memcpy( str+i*BLOCK_SIZE, str_buffer, BLOCK_SIZE );
            size -= BLOCK_SIZE;
        }
        else
        {
            memcpy( str+i*BLOCK_SIZE, str_buffer, size );
        }
    }
    printf("%s\n", str);

    //update lastAccess
    gettimeofday( &(inode[inodeNum].lastAccess), NULL );

    free(str);

    //return success
    return 0;
}

int file_read(char *name, int offset, int size)
{
    int inodeNum = search_cur_dir(name);
    if(inodeNum < 0) {
        printf("read error: file not found.\n");
        return -1;
    }
    if(inode[inodeNum].type == directory) {
        printf("read error: is a directory.\n");
        return -1;
    }

    // Validate offset/size
    if (offset > inode[inodeNum].size) {
        printf("read error: offset > file size.\n");
        return -1;
    }
    if (offset + size > inode[inodeNum].size) {
        size = inode[inodeNum].size - offset; // Truncate if exceeds file size
    }

    char *buf = (char*)malloc(size + 1);
    int bytesRead = 0;

    // Calculate start block and offset within that block
    int startBlockIdx = offset / BLOCK_SIZE;
    int startBlockOffset = offset % BLOCK_SIZE;

    // Loop through blocks
    for (int i = startBlockIdx; i < inode[inodeNum].blockCount && bytesRead < size; i++) {
        int diskBlock = inode[inodeNum].directBlock[i];
        char blockBuf[BLOCK_SIZE];
        disk_read(diskBlock, blockBuf);

        int copyStart = (i == startBlockIdx) ? startBlockOffset : 0;
        int copySize = BLOCK_SIZE - copyStart;
        if (bytesRead + copySize > size) {
            copySize = size - bytesRead;
        }

        memcpy(buf + bytesRead, blockBuf + copyStart, copySize);
        bytesRead += copySize;
    }

    buf[size] = '\0';
    printf("%s\n", buf);
    free(buf);

    gettimeofday(&(inode[inodeNum].lastAccess), NULL);
    return 0;
}

int file_stat(char *name)
{
    char timebuf[28];
    int inodeNum = search_cur_dir(name);
    if(inodeNum < 0) {
        printf("file cat error: file is not exist.\n");
        return -1;
    }

    printf("Inode\t\t= %d\n", inodeNum);
    if(inode[inodeNum].type == file) printf("type\t\t= File\n");
    else printf("type\t\t= Directory\n");
    printf("owner\t\t= %d\n", inode[inodeNum].owner);
    printf("group\t\t= %d\n", inode[inodeNum].group);
    printf("size\t\t= %d\n", inode[inodeNum].size);
    printf("link_count\t= %d\n", inode[inodeNum].link_count);
    printf("num of block\t= %d\n", inode[inodeNum].blockCount);
    format_timeval(&(inode[inodeNum].created), timebuf, 28);
    printf("Created time\t= %s\n", timebuf);
    format_timeval(&(inode[inodeNum].lastAccess), timebuf, 28);
    printf("Last acc. time\t= %s\n", timebuf);
    return 0;
}

int file_remove(char *name)
{
    int inodeNum = search_cur_dir(name);
    if(inodeNum < 0) {
        printf("rm error: file not found.\n");
        return -1;
    }
    if (inode[inodeNum].type == directory) {
        printf("rm error: is a directory, use rmdir.\n");
        return -1;
    }

    // 1. Decrement Link Count
    inode[inodeNum].link_count--;

    if (inode[inodeNum].link_count > 0) {
        printf("File %s link count decremented to %d\n", name, inode[inodeNum].link_count);
    } 
    else {
        // Free Data Blocks
        for(int i = 0; i < inode[inodeNum].blockCount; i++) {
            int b = inode[inodeNum].directBlock[i];
            set_bit(blockMap, b, 0); 
            superBlock.freeBlockCount++;
        }
        // Free Inode
        set_bit(inodeMap, inodeNum, 0); 
        superBlock.freeInodeCount++;
    }

    // 4. Remove from Directory Entry (Shift remaining entries left)
    int entryIdx = -1;
    for(int i=0; i<curDir.numEntry; i++) {
        if (strcmp(curDir.dentry[i].name, name) == 0) {
            entryIdx = i;
            break;
        }
    }
    
    if(entryIdx != -1) {
        // Replace target with the last entry to fill the hole (O(1) removal)
        int lastIdx = curDir.numEntry - 1;
        curDir.dentry[entryIdx] = curDir.dentry[lastIdx];
        curDir.numEntry--;
        
        disk_write(curDirBlock, (char*)&curDir);
    }

    return 0;
}

int dir_make(char* name)
{
    int inodeNum = search_cur_dir(name); 
    if(inodeNum >= 0) {
        printf("mkdir failed: %s exists.\n", name);
        return -1;
    }

    if(curDir.numEntry + 1 >= MAX_DIR_ENTRY) {
        printf("mkdir failed: directory is full!\n");
        return -1;
    }
    if(superBlock.freeInodeCount < 1) {
        printf("mkdir failed: not enough inode\n");
        return -1;
    }
    if(superBlock.freeBlockCount < 1) {
        printf("mkdir failed: not enough space\n");
        return -1;
    }
    
    int newSubDirInode = get_free_inode();
    if(newSubDirInode < 0) return -1;

    int newDirBlock = get_free_block();
    if(newDirBlock == -1) return -1;

    // Initialize new Inode
    inode[newSubDirInode].type = directory;
    inode[newSubDirInode].owner = 1;
    inode[newSubDirInode].group = 2;
    gettimeofday(&(inode[newSubDirInode].created), NULL);
    gettimeofday(&(inode[newSubDirInode].lastAccess), NULL);
    inode[newSubDirInode].size = 1;
    inode[newSubDirInode].blockCount = 1;
    inode[newSubDirInode].directBlock[0] = newDirBlock;
    inode[newSubDirInode].link_count = 1;

    // Add to current directory
    strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
    curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
    curDir.dentry[curDir.numEntry].inode = newSubDirInode;
    curDir.numEntry++;

    printf("Directory %s create success! \n", name);

    // Initialize . and .. in the new directory block
    Dentry subDir;
    subDir.numEntry = 2;
    strncpy(subDir.dentry[0].name, ".", 1); 
    subDir.dentry[0].name[1] = '\0';
    subDir.dentry[0].inode = newSubDirInode;

    strncpy(subDir.dentry[1].name, "..", 2);
    subDir.dentry[1].name[2] = '\0';
    subDir.dentry[1].inode = curDir.dentry[0].inode; // Parent inode
    
    // Write new directory block
    disk_write(newDirBlock, (char *)&subDir);
    
    // Write updated current directory
    disk_write(curDirBlock, (char *)&curDir);

    return 0;
}

int dir_remove(char *name)
{
    int inodeNum = search_cur_dir(name);
    if(inodeNum < 0) {
        printf("rmdir error: directory not found.\n");
        return -1;
    }
    if(inode[inodeNum].type == file) {
        printf("rmdir error: input is a file.\n");
        return -1;
    }

    // Check if empty (read the directory block)
    Dentry targetDir;
    disk_read(inode[inodeNum].directBlock[0], (char*)&targetDir);
    if(targetDir.numEntry > 2) {
        printf("rmdir error: directory not empty.\n");
        return -1;
    }

    // Free Resources
    set_bit(inodeMap, inodeNum, 0);
    superBlock.freeInodeCount++;
    
    set_bit(blockMap, inode[inodeNum].directBlock[0], 0);
    superBlock.freeBlockCount++;

    // Remove entry from current directory
    int entryIdx = -1;
    for(int i=0; i<curDir.numEntry; i++) {
        if (strcmp(curDir.dentry[i].name, name) == 0) {
            entryIdx = i;
            break;
        }
    }
    if(entryIdx != -1) {
        int lastIdx = curDir.numEntry - 1;
        curDir.dentry[entryIdx] = curDir.dentry[lastIdx];
        curDir.numEntry--;
        disk_write(curDirBlock, (char*)&curDir);
    }
    
    return 0;
}

int dir_change(char* name)
{
    int inodeNum, i;

    //get inode number
    inodeNum = search_cur_dir(name);
    if (inodeNum < 0) 
    {
        printf("cd error: %s does not exist\n", name);
        return -1;
    }
    if (inode[inodeNum].type != directory)
    {
        printf("cd error: %s is not a directory\n", name);
        return -1;
    }

    //write parent directory (curDir) to disk
    disk_write(curDirBlock, (char*)&curDir);

    //read new directory from disk into curDir
    curDirBlock = inode[inodeNum].directBlock[0];
    disk_read(curDirBlock, (char*)&curDir);

    //update last access of directory we are changing to
    gettimeofday(&(inode[inodeNum].lastAccess), NULL);      

    return 0;
}

int ls()
{
    int i;
    for(i = 0; i < curDir.numEntry; i++)
    {
        int n = curDir.dentry[i].inode;
        if(inode[n].type == file) printf("type: file, ");
        else printf("type: dir, ");
        printf("name \"%s\", inode %d, size %d byte\n", curDir.dentry[i].name, curDir.dentry[i].inode, inode[n].size);
    }

    return 0;
}

int fs_stat()
{
    printf("File System Status: \n");
    printf("# of free blocks: %d (%d bytes), # of free inodes: %d\n", superBlock.freeBlockCount, superBlock.freeBlockCount*512, superBlock.freeInodeCount);
    return 0;
}

int hard_link(char *src, char *dest)
{
    int srcInode = search_cur_dir(src);
    if (srcInode < 0) {
        printf("ln error: source file does not exist.\n");
        return -1;
    }
    
    int destInode = search_cur_dir(dest);
    if (destInode >= 0) {
        printf("ln error: destination file already exists.\n");
        return -1;
    }
    
    if (curDir.numEntry >= MAX_DIR_ENTRY) {
         printf("ln error: directory full.\n");
         return -1;
    }

    // Create new entry pointing to SAME inode
    strncpy(curDir.dentry[curDir.numEntry].name, dest, strlen(dest));
    curDir.dentry[curDir.numEntry].name[strlen(dest)] = '\0';
    curDir.dentry[curDir.numEntry].inode = srcInode;
    curDir.numEntry++;

    // Increment link count
    inode[srcInode].link_count++;
    
    // Write changes to disk
    disk_write(curDirBlock, (char*)&curDir);
    
    return 0;
}

int execute_command(char *comm, char *arg1, char *arg2, char *arg3, char *arg4, int numArg)
{
    printf ("\n");
    if(command(comm, "df")) {
        return fs_stat();

    // file command start    
    } else if(command(comm, "create")) {
        if(numArg < 2) {
            printf("error: create <filename> <size>\n");
            return -1;
        }
        return file_create(arg1, atoi(arg2)); 

    } else if(command(comm, "stat")) {
        if(numArg < 1) {
            printf("error: stat <filename>\n");
            return -1;
        }
        return file_stat(arg1); 

    } else if(command(comm, "cat")) {
        if(numArg < 1) {
            printf("error: cat <filename>\n");
            return -1;
        }
        return file_cat(arg1); 

    } else if(command(comm, "read")) {
        if(numArg < 3) {
            printf("error: read <filename> <offset> <size>\n");
            return -1;
        }
        return file_read(arg1, atoi(arg2), atoi(arg3)); 

    } else if(command(comm, "rm")) {
        if(numArg < 1) {
            printf("error: rm <filename>\n");
            return -1;
        }
        return file_remove(arg1); 

    } else if(command(comm, "ln")) {
        if(numArg < 2) {
            printf("error: ln <src> <dest>\n");
            return -1;
        }
        return hard_link(arg1, arg2); 

    // directory command start
    } else if(command(comm, "ls"))  {
        return ls();

    } else if(command(comm, "mkdir")) {
        if(numArg < 1) {
            printf("error: mkdir <dirname>\n");
            return -1;
        }
        return dir_make(arg1); 

    } else if(command(comm, "rmdir")) {
        if(numArg < 1) {
            printf("error: rmdir <dirname>\n");
            return -1;
        }
        return dir_remove(arg1); 

    } else if(command(comm, "cd")) {
        if(numArg < 1) {
            printf("error: cd <dirname>\n");
            return -1;
        }
        return dir_change(arg1); 

    } else {
        fprintf(stderr, "%s: command not found.\n", comm);
        return -1;
    }
    return 0;
}