/*
 * wytar.c
 * Author: Cole Sigmon
 * Date : March 28, 2023
 *
 * COSC 3750, Homework 6
 *
 * This is wytar.c it handles all methods
 * used in the wytar utility.
 */

#include "wytar.h"


 // Creation
int wytar_create(const int fileDescriptor, struct tar_header** archive, const size_t filecount, const char* files[], const char verbos) {
    if (fileDescriptor < 0)
    {
        perror("Bad File Descriptor");
    }

    if (!archive)
    {
        perror("Not an Archive");
    }

    int fileDescriptorOffset = 0;

    // If there's already data
    struct tar_header** wytar = archive;
    if (*wytar)
    {
        // Go to last entry
        while (*wytar && (*wytar)->next)
        {
            wytar = &((*wytar)->next);
        }

        // Get offset for last entry
        unsigned int jump = 512 + oct2uint((*wytar)->size, 11);
        if (jump % 512)
        {
            jump += 512 - (jump % 512);
        }

        // Move File Descriptor
        fileDescriptorOffset = (*wytar)->begin + jump;
        if (lseek(fileDescriptor, fileDescriptorOffset, SEEK_SET) == (off_t)(-1))
        {
            perror("Unable to seek file");
        }
        wytar = &((*wytar)->next);
    }

    // Write Entries
    if (create_entry(fileDescriptor, wytar, archive, filecount, files, &fileDescriptorOffset, verbos) < 0)
    {
        perror("Cannot write Entries");
    }

    // Write end data
    if (create_end_data(fileDescriptor, fileDescriptorOffset, verbos) < 0)
    {
        perror("Cannot write End Data");
    }

    // Clear original names
    wytar = archive;
    while (*wytar)
    {
        memset((*wytar)->name, 0, 100);
        wytar = &((*wytar)->next);
    }
    return fileDescriptorOffset;
}


// Extraction
int wytar_extract(const int fileDescriptor, struct tar_header* archive, const size_t filecount, const char* files[], const char verbos) {
    int ret = 0;

    // Extract specific files
    if (filecount) {
        if (!files) {
            perror("Error: File list empty, expected ");
            perror(filecount);
            perror(" files.");
        }

        while (archive)
        {
            for (size_t i = 0; i < filecount; i++)
            {
                if (!strncmp(archive->name, files[i], MAX(strlen(archive->name), strlen(files[i]))))
                {
                    if (lseek(fileDescriptor, archive->begin, SEEK_SET) == (off_t)(-1))
                    {
                        perror("Unable to find file");
                    }

                    if (extract_entry(fileDescriptor, archive, verbos) < 0)
                    {
                        ret = -1;
                    }
                    break;
                }
            }
            archive = archive->next;
        }
    }
    // Extract
    else
    {
        // Move Offset
        if (lseek(fileDescriptor, 0, SEEK_SET) == (off_t)(-1))
        {
            perror("Unable to find file");
        }

        // Extract Entries
        while (archive) {
            if (extract_entry(fileDescriptor, archive, verbos) < 0)
            {
                ret = -1;
            }
            archive = archive->next;
        }
    }

    return ret;
}


// Create Entries
int create_entry(const int fileDescriptor, struct tar_header** archive, struct tar_header** head, const size_t filecount, const char* files[], int* fileDescriptorOffset, const char verbos)
{
    if (fileDescriptor < 0)
    {
        perror("Error: File Descriptor not Acceptable");
    }

    if (!archive || *archive)
    {
        perror("Error: Archive not Acceptable");
    }

    if (filecount && !files)
    {
        perror("Error: File list empty, expected ");
        perror(filecount);
        perror(" files.");
    }

    // Add new entry
    struct tar_header** tar = archive;
    for (unsigned int i = 0; i < filecount; i++)
    {
        *tar = malloc(sizeof(struct tar_header));

        // Stat file
        if (format_data(*tar, files[i], verbos) < 0)
        {
            perror("Failed to stat ");
            perror(files[i]);
        }

        (*tar)->begin = *fileDescriptorOffset;

        // Directories are entered differently
        if ((*tar)->typeflag == DIRTYPE)
        {
            // Get parent directory name
            const size_t len = strlen((*tar)->name);
            char* parent = calloc(len + 1, sizeof(char));
            strncpy(parent, (*tar)->name, len);

            // Add / to the end
            if ((len < 99) && ((*tar)->name[len - 1] != '/'))
            {
                (*tar)->name[len] = '/';
                (*tar)->name[len + 1] = '\0';
                calculate_chksum(*tar);
            }

            fprintf(stdout, "writing %s", (*tar)->name);

            // Write data to *tar file
            if (write_size(fileDescriptor, (*tar)->block, 512) != 512)
            {
                perror("Failed to write metadata to archive");
            }

            // Go through Directory
            DIR* direct = opendir(parent);
            if (!direct)
            {
                perror("Cannot open directory ");
                perror(parent);
            }

            struct dirent* dire;
            while ((dire = readdir(direct)))
            {
                // If not . or ..
                const size_t sublen = strlen(dire->d_name);
                if (strncmp(dire->d_name, ".", sublen) && strncmp(dire->d_name, "..", sublen))
                {
                    char* path = calloc(len + sublen + 2, sizeof(char));
                    sprintf(path, "%s/%s", parent, dire->d_name);

                    // Write Subdirectories
                    if (create_entry(fileDescriptor, &((*tar)->next), head, 1, (const char**)&path, fileDescriptorOffset, verbos) < 0)
                    {
                        perror("Recursion Error");
                    }

                    while ((*tar)->next)
                    {
                        tar = &((*tar)->next);
                    }

                    free(path);
                }
            }
            closedir(direct);

            free(parent);

            tar = &((*tar)->next);
        }
        else
        {
            fprintf(stdout, "Writing %s", (*tar)->name);

            char tarred = 0;
            if (((*tar)->typeflag == REGTYPE) || ((*tar)->typeflag == SYMTYPE))
            {
                struct tar_header* found = exists(*head, files[i], 1);
                tarred = (found != (*tar));

                // if file is already in tar, change header
                if (tarred)
                {
                    // change link name to (*tar)red file name (both are the same)
                    strncpy((*tar)->linkname, (*tar)->name, 100);

                    // change size to 0
                    memset((*tar)->size, '0', sizeof((*tar)->size) - 1);

                    // recalculate checksum
                    calculate_chksum(*tar);
                }
            }

            // Write data to *tar file
            if (write_size(fileDescriptor, (*tar)->block, 512) != 512)
            {
                perror("Error: Failed to write data to archive");
            }

            if ((*tar)->typeflag == REGTYPE)
            {
                // If file isn't in the tar file, copy data in
                if (!tarred)
                {
                    int file = open((*tar)->name, O_RDONLY);
                    if (file < 0)
                    {
                        perror("Could not open ");
                        perror(files[i]);
                    }

                    int read = 0;
                    char buf[512];
                    while ((read = read_size(file, buf, 512)) > 0)
                    {
                        if (write_size(fileDescriptor, buf, read) != read)
                        {
                            perror("Could not write to archive");
                        }
                    }
                    close(file);
                }
            }

            // Padding Data
            const unsigned int size = oct2uint((*tar)->size, 11);
            const unsigned int pad = 512 - size % 512;

            if (pad != 512)
            {
                for (unsigned int j = 0; j < pad; j++)
                {
                    if (write_size(fileDescriptor, "\0", 1) != 1)
                    {
                        perror("Error: Could not write data padding");
                    }
                }
                *fileDescriptorOffset += pad;
            }
            *fileDescriptorOffset += size;
            tar = &((*tar)->next);
        }
        // Data Size
        *fileDescriptorOffset += 512;
    }

    return 0;
}


// Extract Entries
int extract_entry(const int fileDescriptor, struct tar_header* entry, const char verbos)
{
    fprintf(stdout, "%s", entry->name);

    if (entry->typeflag == REGTYPE)
    {
        // create intermediate directories
        size_t len = strlen(entry->name);
        if (!len)
        {
            perror("Error: Entries must have a Name to extract");
        }

        char* path = calloc(len + 1, sizeof(char));
        strncpy(path, entry->name, len);

        // Remove File
        while (--len && (path[len] != '/'));
        // If not found, terminate path
        path[len] = '\0';

        if (recurse_mkdir(path, oct2uint(entry->mode, 7) & 0777) < 0)
        {
            fprintf(stderr, "Could not make directory %s", path);
            free(path);
            return -1;
        }
        free(path);

        // Create file
        const unsigned int size = oct2uint(entry->size, 11);
        int file = open(entry->name, O_WRONLY | O_CREAT | O_TRUNC, oct2uint(entry->mode, 7) & 0777);
        if (file < 0)
        {
            perror("Error: Unable to open file ");
            perror(entry->name);
        }

        // Point to data location
        if (lseek(fileDescriptor, 512 + entry->begin, SEEK_SET) == (off_t)(-1))
        {
            perror("Error: Bad index");
        }

        // Copy Data
        char buf[512];
        int got = 0;
        while (got < size)
        {
            int read;
            if ((read = read_size(fileDescriptor, buf, MIN(size - got, 512))) < 0)
            {
                perror("Error: Unable to read from archive");
            }

            if (write(file, buf, read) != read)
            {
                perror("Unable to write to ");
                perror(entry->name);
            }

            got += read;
        }

        close(file);
    }
    else if (entry->typeflag == SYMTYPE)
    {
        if (symlink(entry->linkname, entry->name) < 0)
        {
            perror("Error: Unable to make symlink");
            perror(entry->name);
        }
    }
    else if (entry->typeflag == DIRTYPE)
    {
        if (recurse_mkdir(entry->name, oct2uint(entry->mode, 7) & 0777) < 0)
        {
            perror("Error: Unable to create directory");
            perror(entry->name);
        }
    }
    return 0;
}

// Write End Data
int create_end_data(const int fileDescriptor, int size, const char verbos)
{
    if (fileDescriptor < 0)
    {
        return -1;
    }

    // complete current record
    const int pad = RECORDSIZE - (size % RECORDSIZE);
    for (int i = 0; i < pad; i++)
    {
        if (write(fileDescriptor, "\0", 1) != 1)
        {
            perror("Error: Tar file unable to close");
            return -1;
        }
    }

    // if the current record does not have 2 blocks of zeros, add a whole other record
    if (pad < (2 * BLOCKSIZE))
    {
        for (int i = 0; i < RECORDSIZE; i++)
        {
            if (write(fileDescriptor, "\0", 1) != 1)
            {
                perror("Error: Unable to close tar file");
                return -1;
            }
        }
        return pad + RECORDSIZE;
    }

    return pad;
}

int format_data(struct tar_header* entry, const char* name, const char verbos) {
    if (!entry)
    {
        perror("Bad destination entry");
    }

    struct stat st;
    if (lstat(name, &st))
    {
        perror("Cannot stat ");
        perror(name);
    }

    // remove relative path
    int move = 0;
    if (!strncmp(name, "/", 1))
    {
        move = 1;
    }
    else if (!strncmp(name, "./", 2))
    {
        move = 2;
    }
    else if (!strncmp(name, "../", 3))
    {
        move = 3;
    }

    // start putting in new data (all fields are NULL terminated ASCII strings)
    memset(entry, 0, sizeof(struct tar_header));
    strncpy(entry->name, name + move, 100);
    snprintf(entry->mode, sizeof(entry->mode), "%07o", st.st_mode & 0777);
    snprintf(entry->uid, sizeof(entry->uid), "%07o", st.st_uid);
    snprintf(entry->gid, sizeof(entry->gid), "%07o", st.st_gid);
    snprintf(entry->size, sizeof(entry->size), "%011o", (int)st.st_size);
    snprintf(entry->mtime, sizeof(entry->mtime), "%011o", (int)st.st_mtime);
    strncpy(entry->gname, "None", 5);
    memcpy(entry->uname, "ustar  \x00", 8);

    // figure out name type and fill in type-specific fields
    switch (st.st_mode & S_IFMT)
    {
    case S_IFLNK:
        entry->typeflag = SYMTYPE;

        // file size is 0, but will print link size
        memset(entry->size, '0', sizeof(entry->size) - 1);

        // get link name
        if (readlink(name, entry->linkname, 100) < 0)
        {
            perror("Could not read link ");
            perror(name);
        }

        break;
    case S_IFDIR:
        memset(entry->size, '0', 11);
        entry->typeflag = DIRTYPE;
        break;
    default:
        entry->typeflag = -1;
        perror("Error: Unknown filetype");
    }

    // get username
    struct passwd pwd;
    char buffer[4096];
    struct passwd* result = NULL;
    if (getpwuid_r(st.st_uid, &pwd, buffer, sizeof(buffer), &result))
    {
        const int err = errno;
        perror("Warning: Unable to get username of uid ");
        perror(st.st_uid);
        perror(" for entry '");
        perror(name);
        perror("': ");
        perror(strerror(err);
    }

    strncpy(entry->uname, buffer, sizeof(entry->uname) - 1);

    // get group name
    struct group* grp = getgrgid(st.st_gid);
    if (grp)
    {
        strncpy(entry->gname, grp->gr_name, sizeof(entry->gname) - 1);
    }

    // get the checksum
    calculate_chksum(entry);

    return 0;
}

int recurse_mkdir(const char* dir, const unsigned int mode)
{
    const size_t len = strlen(dir);

    if (!len)
    {
        return 0;
    }

    char* path = calloc(len + 1, sizeof(char));
    strncpy(path, dir, len);

    // Remove '/'
    if (path[len - 1] == '/')
    {
        path[len - 1] = 0;
    }

    // Subdirectories
    for (char* p = path + 1; *p; p++)
    {
        if (*p == '/')
        {
            *p = '\0';

            mkdir(path, mode);

            *p = '/';
        }
    }

    mkdir(path, mode);

    free(path);
    return 0;
}

unsigned int calculate_chksum(struct tar_header* entry)
{
    // Chksum is spaces until fully calculated
    memset(entry->chksum, ' ', 8);

    // Sum of data
    unsigned int check = 0;
    for (int i = 0; i < 512; i++)
    {
        check += (unsigned char)entry->block[i];
    }

    fprintf(stdout, entry->chksum, sizeof(entry->chksum), "%06o0", check);

    entry->chksum[6] = '\0';
    entry->chksum[7] = ' ';
    return check;
}