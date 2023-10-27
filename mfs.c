/***********************************
HOW TO COMPILE mfs.c:

gcc -g -Wall -Werror --std=c99 mfs.c
************************************/
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <stdint.h>

#define BLOCK_SIZE 1024
#define NUM_BLOCKS 65536
#define BLOCKS_PER_FILE 1024
#define NUM_FILES 256
#define FIRST_DATA_BLOCK 790 
#define MAX_FILE_SIZE 1048576

uint8_t data[NUM_BLOCKS][BLOCK_SIZE];
uint8_t *free_blocks; 
uint8_t *free_inodes;

//directory structure
struct directoryEntry
{
    char    filename[64];
    short   in_use;
    int32_t inode;
    uint8_t hidden;
    uint8_t readOnly;
};

struct directoryEntry *directory;

//inode structure
struct inode
{
    int32_t  blocks[BLOCKS_PER_FILE];
    short    in_use;
	uint8_t  attribute;
	uint32_t file_size;
};

struct inode *inodes;

FILE 	*fp;
char 	image_name[64];
uint8_t image_open;
uint8_t is_saved;

#define WHITESPACE " \t\n"      // We want to split our command line up into tokens
                                // so we need to define what delimits our tokens.
                                // In this case  white space
                                // will separate the tokens on our command line

#define MAX_COMMAND_SIZE 255    // The maximum command-line size

#define MAX_NUM_ARGUMENTS 5     // Mav shell only supports four arguments


/*************************************** FILE COMMAND FUNCTIONS ********************************************/

// Helper function that returns the index of a free block on success and -1 on failure
int32_t findFreeBlock()
{
	int i;
	for(i = 0; i < NUM_BLOCKS; i++)
	{
		if(free_blocks[i])
		{
			return i + FIRST_DATA_BLOCK;
		}
	}
	return -1;
}

// Helper function that returns the index of a free inode on success and -1 on failure
int32_t findFreeInode()
{
	int i;

	for(i = 0; i < NUM_FILES; i++)
	{
		if(free_inodes[i])
		{
			return i;
		}
	}
	return -1;
}

// Helper function that returns a free inode block on success and -1 on failure
int32_t findFreeInodeBlock(int32_t inode)
{
	int i;

	for(i = 0; i < BLOCKS_PER_FILE; i++)
	{
		if(inodes[inode].blocks[i] == -1)
		{
			return i;
		}
	}

	return -1;
}

/* 
   The delete function takes a filename, searches the global directory, and if the file is found, sets its directory/inode 
   in_use flags to 0, effectively deleting it from our disk image. Deletes the file from the filesystem image, If the file 
   does exist in the file system it shall be deleted and all the space available for additional files.
*/
void delete(char *filename)
{
    //declares the delete function and specifies that it takes a char * argument called filename.
    is_saved = 0;
    //This line sets the global is_saved flag to 0, 
    //indicating that the disk image is not in a saved state after the file deletion
    if (image_open == 0)
    {
        printf("ERROR: Disk image is not open\n");
        return;
    }

    int found = 0;
    int i;
    for (i = 0; i < NUM_FILES; i++)
    {
        if (directory[i].in_use && strcmp(directory[i].filename, filename) == 0)
        {
            //If the file is marked as readOnly, it prints an error message 
            //and returns without performing any deletion.
            if(directory[i].readOnly)
            {
                printf("ERROR: File is read only -- cannot delete\n");
                return;
            }

            //sets the in_use flag of the directory and inode entries to 0, 
            //indicating that they are no longer being used.
            found = 1;
            directory[i].in_use = 0;
            inodes[directory[i].inode].in_use = 0;            

            int j;
            for(j = inodes[directory[i].inode].blocks[0]; j < BLOCKS_PER_FILE; j++)
            {
                //marks all the blocks corresponding to the file 
                //as free by setting the free_blocks array elements to 1.
                free_blocks[j] = 1;
            }
            break;
        }
    }

    if (!found)
    {
        printf("ERROR: File not found\n");
        return;
    }

    printf("File %s deleted successfully\n", filename);
}

/* The undel function takes a filename, searches the global directory, and if the file is found, 
   sets its directory/inode in_use flags to 1, effectively undeleting it from our disk image. 
   Undeletes the file from the filesystem imageThe undelete command allows the user to undelete
   a file that has been deleted from the file system .
*/
void undel(char *filename)
{
    //sets the global is_saved flag to 0, indicating that the disk image is 
    //not in a saved state after the file undeletion.
    is_saved = 0;
    if (image_open == 0)
    {
        printf("ERROR: Disk image is not open\n");
        return;
    }

    //checks if the global image_open flag is 0, indicating that the disk image is not currently open. 
    //if so, it prints an error message and returns without performing any undeletion.
    int file_found = 0;
    int i;
    for (i = 0; i < NUM_FILES; i++)
    {
        //sets the file_found flag to 1, indicating that the file was found and can be undeleted.
        if (!directory[i].in_use && strcmp(directory[i].filename, filename) == 0)
        {
            file_found = 1;

            if (inodes[directory[i].inode].in_use == 0)
            {
                directory[i].in_use = 1;
                inodes[directory[i].inode].in_use = 1;
                printf("File %s has been undeleted\n", filename);

                int j;
                for(j = inodes[directory[i].inode].blocks[0]; j < BLOCKS_PER_FILE; j++)
                {
                    free_blocks[j] = 0;
                }

            }
            //if inode is already in use, it prints a message indicating that the file has not been deleted.
            else
            {
                printf("File %s has not been deleted\n", filename);
            }

            break;
        }
    }

    //if the filename is not found in the directory.
    if (!file_found)
    {
        printf("File not found in the directory\n");
    }

}

/* The read function takes in a filename, a starting byte and a total number of bytes and prints to stdout the number
   of bytes specified from the file in hex. Print <number of bytes>
   bytes from the file, in hexadecimal, starting at <starting byte> 
*/
void read_file(char *filename, int starting_byte, int number_of_bytes)
{
    if (image_open == 0)
    {
        printf("ERROR: Disk image is not open\n");
        return;
    }

    ////searches for the filename in the directory array by iterating over all the entries in the array,.
    //checks if the global image_open flag is 0, indicating that the disk image is not currently open. 
    //if so, it prints an error message and returns without performing any read operation.
    int found = 0;
    int i;
    for (i = 0; i < NUM_FILES; i++)
    {
        //if the filename is found and its corresponding directory entry is marked as in_use, the function
        //sets the found flag to 1 and exits the loop.
        if (directory[i].in_use && strcmp(directory[i].filename, filename) == 0)
        {
            found = 1;
            break;
        }
    }

    //if the filename is not found,the function 
    //prints an error message and returns without performing any read operation.
    if (!found)
    {
        printf("ERROR: File not found\n");
        return;
    }
    
    //retrieves a pointer to the inode entry corresponding to the file in the directory array.
    struct inode *inode_ptr = &inodes[directory[i].inode];

    //checks if the starting_byte is within the valid range of 0 to the size of the file in bytes. 
    //if not, it prints an error message and returns without performing any read operation.
    if (starting_byte < 0 || starting_byte >= BLOCK_SIZE * BLOCKS_PER_FILE)
    {
        printf("ERROR: Invalid starting byte\n");
        return;
    }

    //checks if the number_of_bytes is within the valid range of 1 to the remaining bytes in the file starting from the starting_byte offset. 
    //if not, it prints an error message and returns without performing any read operation.
    if (number_of_bytes <= 0 || number_of_bytes > BLOCK_SIZE * BLOCKS_PER_FILE - starting_byte)
    {
        printf("ERROR: Invalid number of bytes\n");
        return;
    }

    //sets up the necessary variables to read the specified bytes from the file:
    int block_index = starting_byte / BLOCK_SIZE;
    int block_offset = starting_byte % BLOCK_SIZE;
    int bytes_remaining = number_of_bytes;

    while (bytes_remaining > 0)
    {
        int bytes_to_read = (BLOCK_SIZE - block_offset < bytes_remaining) ? BLOCK_SIZE - block_offset : bytes_remaining;

        for (i = 0; i < bytes_to_read; i++)
        {
            printf("%02x ", data[inode_ptr->blocks[block_index]][block_offset + i]);
        }

        bytes_remaining -= bytes_to_read;
        block_index++;
        block_offset = 0;
    }
    printf("\n");
}

/* The list function simply lists the files in the directory of the current open disk image.
   It can accept 0, 1, or 2 parameters from the user.
   List the files in the filesystem image. If the -h parameter is given it will also list hidden files. 
   If the -a parameter is provided the attributes will also be listed with the file and displayed as an 8-bit binary value
   If -a is specified, each attribute will be printed beside the requested file. If -h is specified, hidden files will be shown.
   list command shall display all the files in the file system,their size in bytes and the time they were added to the file system
   this function takes two character pointers "flag1" and "flag2" as parameters. The function lists the files in a directory along with their attributes (if specified by the flags).
*/
void list(char *flag1, char *flag2)
{
    int i;
    int not_found = 1;
    int show_hidden = 0;
    int show_attrib = 0;

    //verifying flags are not null and that they are valid
    //loops through each entry in a directory and checks if it is in use.
    if(flag1)
    {
        int no_match = 1;
        //if the show_hidden flag is set or if the entry is not hidden, the function prints the filename.
        if( strcmp(flag1, "-h") == 0)
        {
            show_hidden = 1;
            no_match = 0;
        }
        
        //show_attrib flag is set and the entry is hidden, the function prints the "[h]" attribute. 
        //If the entry is read-only, the function prints the "[r]" attribute.
        if( strcmp(flag1, "-a") == 0)
        {
            show_attrib = 1;
            no_match = 0;
        }

        if(no_match)
        {
            printf("ERROR: Invalid list flag. Must be -h or -a\n");
            return;
        }
    }

    if(flag2)
    {
        int no_match = 1;

        if( strcmp(flag2, "-h") == 0)
        {
            show_hidden = 1;
            no_match = 0;
        }
        
        if( strcmp(flag2, "-a") == 0)
        {
            show_attrib = 1;
            no_match = 0;
        }

        //if no files are found, the function prints 
        if(no_match)
        {
            printf("ERROR: Invalid list flag. Must be -h or -a\n");
            return;
        }
    }

    for(i = 0; i < NUM_FILES; i++)
    {
        if(directory[i].in_use)
        {

            if( show_hidden || !directory[i].hidden)
            {
                if(not_found == 1)
                {
                    not_found = 0;
                }

                char filename[65];
                memset(filename, 0, 65);
                strncpy(filename, directory[i].filename, strlen(directory[i].filename));
                printf("%s", filename);

                if(show_attrib)
                {
                    if(directory[i].hidden)
                    {
                        printf(" [h]");
                    }
                    if(directory[i].readOnly)
                    {
                        printf(" [r]");
                    }
                }

                printf("\n");
            }  
        }
    }

    if(not_found)
    {
        printf("Directory is empty\n");
    }
}

/* file_insert function takes a file from the current working directory 
   //and inserts it into the currently open disk image. 
   command insert allows the user to put a new file into the file system
*/
void file_insert(char *src_filename)
{
    is_saved = 0;
    if (image_open == 0)
    {
        printf("ERROR: Disk image is not open\n");
        return;
    }

    //If there is not enough disk space for the file an error will be returned
    if (strlen(src_filename) > 63)
    {
        printf("insert error: File name too long.\n");
        return;
    }

    int directory_index = -1;
    int inode_ix = -1;
    int i;
    for (i = 0; i < NUM_FILES; i++)
    {
        if (!directory[i].in_use)
        {
            if (directory_index == -1)
                directory_index = i;
        }
        if (!inodes[i].in_use)
        {
            if (inode_ix == -1)
                inode_ix = i;
        }
        if (directory_index != -1 && inode_ix != -1)
            break;
    }

    if (directory_index == -1)
    {
        printf("ERROR: No available directory entry\n");
        return;
    }

    if (inode_ix == -1)
    {
        printf("ERROR: No available inode\n");
        return;
    }

    FILE *src_file = fopen(src_filename, "rb");
    if (!src_file)
    {
        printf("ERROR: Cannot open source file\n");
        return;
    }

    fseek(src_file, 0, SEEK_END);
    long file_size = ftell(src_file);
    fseek(src_file, 0, SEEK_SET);

    int required_blocks = (file_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

    int free_count_block = 0;
    for (i = FIRST_DATA_BLOCK; i < NUM_BLOCKS; i++)
    {
        if (free_blocks[i])
            free_count_block++;
    }

    if (free_count_block < required_blocks)
    {
        printf("insert error: Not enough disk space.\n");
        fclose(src_file);
        return;
    }

    strncpy(directory[directory_index].filename, src_filename, 64);
    directory[directory_index].inode = inode_ix;
    directory[directory_index].in_use = 1;
    directory[directory_index].readOnly = 0;
    directory[directory_index].hidden = 0;

    inodes[inode_ix].in_use = 1;

    int block_index = 0;
    for (i = FIRST_DATA_BLOCK; i < NUM_BLOCKS && block_index < required_blocks; i++)
    {
        if (free_blocks[i])
        {
            fread(data[i], 1, BLOCK_SIZE, src_file);
            inodes[inode_ix].blocks[block_index++] = i;
            free_blocks[i] = 0;
        }
    }

    fclose(src_file);

    printf("File %s inserted successfully\n", src_filename);
}

/* The init function is setup code that runs at the beginning of the program's life. It initializes our data structures
   to the appropriate values.*/
void init()
{
    is_saved = 0;

	directory = (struct directoryEntry*)&data[0][0];
	inodes 	  = (struct inode*)&data[20][0];
	free_blocks = (uint8_t *)&data[277][0];
	free_inodes = (uint8_t *)&data[19][0];

	memset( image_name, 0, 64);
	image_open = 0;

	int i;
	for(i = 0; i < NUM_FILES; i++)
	{
		directory[i].in_use = 0;
		directory[i].inode  = -1;
		free_inodes[i] 		= 1;

		memset(directory[i].filename, 0, 64);

		int j;
		for(j = 0; j < NUM_BLOCKS; j++)
		{
			inodes[i].blocks[j] = -1;
			inodes[i].in_use 	= 0;
			inodes[i].attribute = 0;
			inodes[i].file_size = 0;
		}
	}
	int j;
	for(j = 0; j < NUM_BLOCKS; j++)
	{
		free_blocks[j] = 1;
	}

}

/* 
   df command displays the amount of free space in the file system in bytes.
   the df function lists the amount of available disk space for the currently open disk image by iterating 
   through the number of free blocks and multiplying each one by the BLOCK_SIZE. 
    df() returns the amount of free disk space in the virtual file system contained within a disk image, 
    measured in bytes. The function iterates over all data blocks in the virtual file system and counts the 
    number of free blocks.It then multiplies the count by the block size to calculate the total amount of free space in bytes. 
    */
uint32_t df()
{
	int i;
	int count = 0;

	for(i = FIRST_DATA_BLOCK; i < NUM_BLOCKS; i++)
	{	
		if(free_blocks[i])
		{
			count++;
		}
	}
	count = count * BLOCK_SIZE;
	return count;
}

/* creates a file system image file with the named provided by the user. 
   The createfs function creates a new disk image and initializes its structures to the appropriate values. No changes made to 
   the disk image will be saved unless save is specifically called by the user. 
*/
void createfs(char *filename)
{
    // first opens the file in write mode and saves the filename to a global variable. The function then initializes 
    //all data blocks to 0 and sets the image_open flag to 1, indicating that a disk image is open.
    is_saved = 0;
	fp = fopen(filename, "w");
	strncpy(image_name, filename, strlen(filename));
	memset( data, 0, NUM_BLOCKS * BLOCK_SIZE);
	image_open = 1;

    //All inode blocks are also set to -1, indicating that they are not being used.
	int i;
	for(i = 0; i < NUM_FILES; i++)
	{
		directory[i].in_use     = 0;
		directory[i].inode      = -1;
        directory[i].hidden     = 0;
        directory[i].readOnly   = 0;
		free_inodes[i] 		    = 1;

		memset(directory[i].filename, 0, 64);

		int j;
		for(j = 0; j < NUM_BLOCKS; j++)
		{
			inodes[i].blocks[j] = -1;
			inodes[i].in_use 	= 0;
			inodes[i].attribute = 0;
			inodes[i].file_size = 0;
		}
	}

    // sets all data blocks to be free by setting the corresponding flags in the free_blocks array. This function creates a blank virtual file system in the disk image, 
    // ready to have files inserted into it using other functions.
	int j;
	for(j = 0; j < NUM_BLOCKS; j++)
	{
		free_blocks[j] = 1;
	}
}

/* savefs command writes the file system to disk.
   The savefs function saves the currently open disk image. This includes any inserts, deletes, 
   undeletes, attributes, etc. 
*/
void savefs()
{
    is_saved = 1;

	if(image_open == 0)
	{
		printf("Error: Disk image is not open\n");
	}

    //indicates that the current state of the virtual file system has been saved to the disk image file. 
    //This function is used to save changes made to the virtual file system so that they can be loaded and used in the future.
	else
	{
		fp = fopen( image_name, "w");

		fwrite( &data[0][0], BLOCK_SIZE, NUM_BLOCKS, fp);

		memset(image_name, 0, 64);
	}
}

/* open command opens a file system image file with the name and path given by the user.
   The openfs function will open the specified disk image. Changes will not be saved unless savefs is 
   called 
*/
void openfs(char *filename)
{
    is_saved = 0;

	fp = fopen( filename, "r");

	strncpy(image_name, filename, strlen( filename));

	fread(&data[0][0], BLOCK_SIZE, NUM_BLOCKS, fp);

	image_open = 1;
}

/* close command closes a file system image file with the name and path given by the user. 
   The close function simply closes the global file pointer. The user is required to close any open files 
   before exiting to prevent data corruption. 
*/
void closefs()
{
    //changes have been made to the disk image file that have not been saved, it sets is_saved to true.
    is_saved = 1;
    //indicates that no disk image file is currently open
	if(image_open == 0)
	{
		printf("close: File not open\n");
		return;
	}
	fclose( fp );

	memset(image_name, 0, 64);
	image_open = 0;
}

/* attrib command sets or removes an attribute from the file. 
   The attrib function can update the attribute flags of a file. Namely, +r and +h will make the file read only or hidden respectively.
   Specifying -r or -h will remove the associated attributes from the file. Read only files cannot be deleted. 
*/
void attrib(char *filename, char *attribute)
{
    uint8_t hidden_plus_flag = 0;
    uint8_t hidden_minus_flag = 0;

    uint8_t readOnly_plus_flag = 0;
    uint8_t readOnly_minus_flag = 0;

    //checks if the attribute provided is valid and sets the appropriate flag accordingly. 
    //if the attribute provided is not valid, it prints an error message and returns.
    //error checking and Setting the attribute flags
    if( strcmp(attribute,"+h") == 0)
    {
        hidden_plus_flag = 1;
    }
    else if( strcmp(attribute, "-h") == 0)
    {
        hidden_minus_flag = 1;
    }
    else if( strcmp(attribute, "-r") == 0)
    {
        readOnly_minus_flag = 1;
    }
    else if( strcmp(attribute, "+r") == 0)
    {
        readOnly_plus_flag = 1;
    }
    else
    {
        printf("USAGE ERROR: attrib [+attribute] [-attribute] <filename>\nAttributes: h (hidden), r (read only)\n");
        return;
    }

    //searches for the file with the given filename and sets the attribute based on the flag that was set earlier. 
    //if the file is not found, it prints an error message and returns.
    // Finding the file and setting its attributes accordingly
    int i;
    for(i = 0; i < NUM_FILES; i++)
    {
        //if attribute is successfully set, the function prints a message indicating whether the attribute was added or removed. Finally, it sets the is_saved flag to 0, 
        //indicating that changes have been made to the file system and need to be saved
        if(directory[i].in_use && strcmp(directory[i].filename, filename) == 0)
        {
            //found the file requested
            if(hidden_plus_flag)
            {
                directory[i].hidden = 1;
                printf("Adding the \"h\" attribute to %s\n", filename);
            }
            else if(hidden_minus_flag)
            {
                directory[i].hidden = 0;
                printf("Removing the \"h\" attribute from %s\n", filename);
            }
            else if(readOnly_minus_flag)
            {
                directory[i].readOnly = 0;
                printf("Removing the \"r\" attribute from %s\n", filename);
            }
            else if(readOnly_plus_flag)
            {
                directory[i].readOnly = 1;
                printf("Adding the \"r\" attribute to %s\n", filename);
            }
            else
            {
                printf("ERROR: Something went wrong while setting the attributes\n");
                return;
            }
            
            // leaving the for loop once we've found what we're looking for
            break;
        }
        else
        {
            printf("attrib: File %s not found\n", filename);
            return;
        }
    }
    is_saved = 0;
}

/* The retrieve function takes a file from the disk image and places it in the current working directory. If
    an additional file has been specified, it will create a new copy of the source file and place it into the
    current working directory. */
void retrieve(char *src_filename, char *new_filename)
{
    if (image_open == 0)
    {
        printf("ERROR: Disk image is not open\n");
        return;
    }

    int found = 0;
    int i;
    for (i = 0; i < NUM_FILES; i++)
    {
        if (directory[i].in_use && strcmp(directory[i].filename, src_filename) == 0)
        {
            found = 1;
            break;
        }
    }

    if (!found)
    {
        printf("ERROR: File not found\n");
        return;
    }

    int status;
    struct stat buf;

    // Getting file info using <sys/stat.h>
    status = stat(src_filename, &buf);

    if(status == -1)
    {
        printf("ERROR: Could not retrieve file status\n");
        return;
    }

    // Determining which version of retrieve to use
    if (!new_filename)
    {
        FILE *ofp;
        ofp = fopen(src_filename, "w");

        int block_index = inodes[directory[i].inode].blocks[directory[i].inode];
        int copy_size = buf.st_size;
        int block_offset = 0;

        while (copy_size > 0)
        {
            int num_bytes;

            if( copy_size < BLOCK_SIZE )
            {
                num_bytes = copy_size; 
            } 
            else 
            {
                num_bytes = BLOCK_SIZE;
            }
            // Write num_bytes number of bytes from our data array into our output file.
            fwrite( data[block_index], num_bytes, 1, ofp ); 

            copy_size -= BLOCK_SIZE;
            block_offset += BLOCK_SIZE;
            block_index++;

            fseek( ofp, block_offset, SEEK_SET );
        }

        fclose(ofp);
    }
    else 
    {
        FILE *ofp;
        ofp = fopen(new_filename, "w");

        int block_index = inodes[directory[i].inode].blocks[directory[i].inode];
        int copy_size = buf.st_size;
        int block_offset = 0;

        while (copy_size > 0)
        {
            int num_bytes;

            if( copy_size < BLOCK_SIZE )
            {
                num_bytes = copy_size; 
            } 
            else 
            {
                num_bytes = BLOCK_SIZE;
            }
            // Write num_bytes number of bytes from our data array into our output file.
            fwrite( data[block_index], num_bytes, 1, ofp ); 

            copy_size -= BLOCK_SIZE;
            block_offset += BLOCK_SIZE;
            block_index++;

            fseek( ofp, block_offset, SEEK_SET );
        }

        fclose(ofp);
    }
}


/********************************************* MAIN *****************************************************/

int main()
{
  char * command_string = (char*) malloc( MAX_COMMAND_SIZE );
  fp = NULL;
  init();
  while( 1 )
  {
    printf ("mfs> ");
    // Read the command from the commandline.  The
    // maximum command that will be read is MAX_COMMAND_SIZE
    // This while command will wait here until the user
    // inputs something since fgets returns NULL when there
    // is no input
    while( !fgets (command_string, MAX_COMMAND_SIZE, stdin) );

    /* Parse input */
    char *token[MAX_NUM_ARGUMENTS];

    for( int i = 0; i < MAX_NUM_ARGUMENTS; i++ )
    {
      token[i] = NULL;
    }

    int   token_count = 0;                                 
                                                           
    // Pointer to point to the token
    // parsed by strsep
    char *argument_ptr = NULL;                                                                                     
    char *working_string  = strdup( command_string );                

    // we are going to move the working_string pointer so
    // keep track of its original value so we can deallocate
    // the correct amount at the end
    char *head_ptr = working_string;

    // Tokenize the input strings with whitespace used as the delimiter
    while ( ( (argument_ptr = strsep(&working_string, WHITESPACE ) ) != NULL) && 
              (token_count<MAX_NUM_ARGUMENTS))
    {

      token[token_count] = strndup( argument_ptr, MAX_COMMAND_SIZE );
      if( strlen( token[token_count] ) == 0 )
      {
        token[token_count] = NULL;
      }
      token_count++;
    }

	// Allowing "blank" entries
	if(token[0] == NULL)
	{
		continue;
	}

    // Processing filesystem commands
    if( strcmp("createfs", token[0]) == 0 )
    {
        if(token[1] == NULL)
        {
            printf("createfs: Filename not provided\n");
            continue;
        }

        createfs(token[1]);
    }

	else if( strcmp("savefs", token[0]) == 0 )
	{
        if(is_saved)
        {
            printf("ERROR: Disk image is already saved\n");
            continue;
        }

		savefs();
	}

	else if( strcmp("quit", token[0]) == 0 )
	{
		if(image_open == 1)
		{
			printf("Please close your file before exiting by typing \"close\"\n");
		}
		else
		{
			printf("Thanks for using Mav File System!\n");
			exit(0);
		}
	}

	else if( strcmp("open", token[0]) == 0)
	{
		if(token[1] == NULL)
		{
			printf("open: File not found\n");
			continue;
		}

		openfs(token[1]);
	}

	else if( strcmp("close", token[0]) == 0)
	{
		closefs();
	}

	else if( strcmp("list", token[0]) == 0)
	{
		if(image_open == 0)
		{
			printf("ERROR: Disk image is not open\n");
			continue;
        }
        list(token[1], token[2]);
	}

	else if( strcmp("df", token[0]) == 0 )
	{
		if( image_open == 0)
		{
			printf("ERROR: Disk image is not open\n");
			continue;
		}
		uint32_t freeBytes = df();
		printf("%d bytes free\n", freeBytes);
	}

	else if( strcmp("insert", token[0]) == 0 )
	{
		if(image_open == 0)
		{
			printf("ERROR: Disk image is not open\n");
			continue;
		}

		if(token[1] == NULL)
		{
			printf("ERROR: No filename specified\n");
			continue;
		}
	    file_insert(token[1]);

	}

	else if( strcmp("retrieve", token[0]) == 0 )
	{
        if(token[1] == NULL)
		{
			printf("ERROR: No filename specified\n");
			continue;
		}

		retrieve(token[1], token[2]);
	}

	else if( strcmp("delete", token[0]) == 0 )
	{
		if(token[1] == NULL)
		{
			printf("ERROR: No filename specified\n");
			continue;
		}
		delete(token[1]);
	}

	else if( strcmp("undel", token[0]) == 0 )
	{
		if (token[1] == NULL)
		{
			printf("ERROR: Filename is required\n");
			continue;
		}
		undel(token[1]);
	}

	else if( strcmp("attrib", token[0]) == 0 )
	{
		if(token[1] == NULL || token[2] == NULL)
        {
            printf("USAGE ERROR:\nattrib [+attribute] [-attribute] <filename>\nAttributes: h (hidden), r (read only)\n");
            continue;
        }
        attrib(token[2], token[1]);
	}

	else if( strcmp("read", token[0]) == 0 )
	{
		if (token[1] == NULL || token[2] == NULL || token[3] == NULL)
        {
            printf("ERROR: Filename, starting byte, and number of bytes are required\n");
            continue;
        }
        int starting_byte = atoi(token[2]);
        int number_of_bytes = atoi(token[3]);
        read_file(token[1], starting_byte, number_of_bytes);
	}

	else
	{
		printf("ERROR: '%s' is an unrecognized command\n", token[0]);
	}

    // Cleanup allocated memory
    for( int i = 0; i < MAX_NUM_ARGUMENTS; i++ )
    {
      if( token[i] != NULL )
      {
        free( token[i] );
      }
    }
    free( head_ptr );
  }
  free( command_string );
  return 0;
}