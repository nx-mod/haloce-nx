/* MCC download staging only. Remote paths are validated before reaching this
 * boundary; existing links and special files are refused again on the host.
 * Exclusive ownership protects against remote senders, not a local process
 * with the same user's authority concurrently replacing the data directory. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "posix.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/syscall.h>
#include <dirent.h>
#include <stdlib.h>
#endif
#endif

/* The folders the MCC code manages or reads packages from: links and special
 * files are refused from the first of them down. The folders above it (the
 * data root, the home folder, a Steam root such as ~/.steam/steam, which is a
 * link on Linux) are the user's own layout and are not checked. A path that
 * names none of them is checked from its root. */
static int managed_folder(const char *component, size_t length)
{
    static const char *const names[]={ "mcc_maps", "mcc_staging", "steamapps" };
    size_t i,j;
    for (i=0;i<sizeof(names)/sizeof(names[0]);i++) {
        if (strlen(names[i])!=length) continue;
        for (j=0;j<length;j++) {
            char c=component[j];
            if (c>='A' && c<='Z') c=(char)(c-'A'+'a');
            if (c!=names[i][j]) break;
        }
        if (j==length) return 1;
    }
    return 0;
}

int posix_mcc_share_path_safe(const char *name)
{
    char path[2048];
    size_t i,start,length,checked;
    int missing=0;
    if (!name || !*name || strlen(name)>=sizeof(path)) return 0;
#ifdef _WIN32
    length=GetFullPathNameA(name,sizeof(path),path,NULL);
    if (!length || length>=sizeof(path)) return 0;
    /* Only ordinary drive-root paths; device namespaces and UNC roots are not
     * destinations for managed downloads. Installed package reads are separate. */
    if (length<3 || path[1]!=':' || (path[2]!='\\' && path[2]!='/') || strchr(path+2,':')) return 0;
    start=3;
#else
    strcpy(path,name); length=strlen(path); start=path[0]=='/' ? 1 : 0;
#endif
    /* the first managed folder's offset, or the path's first component */
    checked=start;
    for (i=start;i<=length;i++) {
        if (path[i] && path[i]!='/' && path[i]!='\\') continue;
        if (i>checked && managed_folder(path+checked,i-checked)) break;
        checked=i+1;
    }
    if (i>length) checked=start;
    for (i=start;i<=length;i++) {
        char saved;
        if (path[i] && path[i]!='/' && path[i]!='\\') continue;
        if (i==start) { start=i+1; continue; }
        if ((i-start==1 && path[start]=='.') ||
            (i-start==2 && path[start]=='.' && path[start+1]=='.')) return 0;
        saved=path[i]; path[i]=0;
        if (!missing && start>=checked) {
#ifdef _WIN32
            DWORD attributes=GetFileAttributesA(path);
            if (attributes==INVALID_FILE_ATTRIBUTES) {
                DWORD error=GetLastError();
                if (error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND) return 0;
                missing=1;
            } else if ((attributes&FILE_ATTRIBUTE_REPARSE_POINT) ||
                (saved && !(attributes&FILE_ATTRIBUTE_DIRECTORY)) || (attributes&FILE_ATTRIBUTE_DEVICE)) return 0;
#else
            struct stat status;
            if (lstat(path,&status)) {
                if (errno!=ENOENT) return 0;
                missing=1;
            } else if (S_ISLNK(status.st_mode) ||
                (saved ? !S_ISDIR(status.st_mode) : !S_ISDIR(status.st_mode) && !S_ISREG(status.st_mode))) return 0;
#endif
        }
        path[i]=saved; start=i+1;
    }
    return 1;
}

#if defined(__linux__) && defined(SYS_renameat2)
/* a tree removed (what copy_tree made, or the source it moved): regular
 * files and folders alone, no link followed */
static void remove_tree(const char *path)
{
    struct stat status;
    DIR *directory;
    struct dirent *entry;
    char child[2048];
    if (lstat(path,&status)) return;
    if (!S_ISDIR(status.st_mode)) { unlink(path); return; }
    directory=opendir(path);
    while (directory && (entry=readdir(directory))!=NULL) {
        if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
        if (snprintf(child,sizeof(child),"%s/%s",path,entry->d_name)>=(int)sizeof(child)) continue;
        remove_tree(child);
    }
    if (directory) closedir(directory);
    rmdir(path);
}

/* a tree copied (regular files and folders; a link or a special file
 * refuses the copy), each file synced: 1, else 0 */
static int copy_tree(const char *from, const char *to)
{
    struct stat status;
    if (lstat(from,&status)) return 0;
    if (S_ISDIR(status.st_mode)) {
        DIR *directory;
        struct dirent *entry;
        int copied=1;
        if (mkdir(to,0777) && errno!=EEXIST) return 0;
        directory=opendir(from);
        if (!directory) return 0;
        while (copied && (entry=readdir(directory))!=NULL) {
            char source[2048],target[2048];
            if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
            if (snprintf(source,sizeof(source),"%s/%s",from,entry->d_name)>=(int)sizeof(source) ||
                snprintf(target,sizeof(target),"%s/%s",to,entry->d_name)>=(int)sizeof(target)) { copied=0; break; }
            copied=copy_tree(source,target);
        }
        closedir(directory);
        return copied;
    }
    if (!S_ISREG(status.st_mode)) return 0;
    {
        int in=open(from,O_RDONLY|O_NOFOLLOW|O_CLOEXEC),out;
        char *buffer;
        ssize_t got=0;
        int copied;
        if (in<0) return 0;
        out=open(to,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0666);
        buffer=out>=0 ? malloc(1<<20) : NULL;
        copied=buffer!=NULL;
        while (copied && (got=read(in,buffer,1<<20))>0) {
            char *at=buffer;
            while (got>0) {
                ssize_t put=write(out,at,(size_t)got);
                if (put<=0) { copied=0; break; }
                at+=put; got-=put;
            }
        }
        copied=copied && got==0 && fsync(out)==0;
        free(buffer);
        if (out>=0 && close(out)) copied=0;
        close(in);
        return copied;
    }
}

/* across storages (mcc_staging in the data folder, mcc_maps moved to a
 * memory card: game.mcc.path), where a rename cannot go: copied beside the
 * target, renamed into place (still refusing one there), then the source
 * removed */
static int publish_by_copy(const char *from, const char *to)
{
    char part[2048];
    struct stat status;
    if (snprintf(part,sizeof(part),"%s.copying",to)>=(int)sizeof(part)) return 0;
    remove_tree(part);
    if (!lstat(to,&status) || !copy_tree(from,part)) { remove_tree(part); return 0; }
    if (syscall(SYS_renameat2,AT_FDCWD,part,AT_FDCWD,to,1 /* RENAME_NOREPLACE */)) {
        remove_tree(part);
        return 0;
    }
    remove_tree(from);
    return 1;
}
#endif

int posix_mcc_share_publish(const char *from, const char *to)
{
    if (!posix_mcc_share_path_safe(from) || !posix_mcc_share_path_safe(to)) return 0;
#ifdef _WIN32
    /* MoveFile fails for an existing target; no REPLACE_EXISTING flag. */
    return MoveFileA(from,to)!=0;
#elif defined(__linux__) && defined(SYS_renameat2)
    if (syscall(SYS_renameat2,AT_FDCWD,from,AT_FDCWD,to,1 /* RENAME_NOREPLACE */)==0) return 1;
    return errno==EXDEV && publish_by_copy(from,to);
#elif defined(__SWITCH__) || defined(__FreeBSD__) || defined(__PROSPERO__)
    /* (the Switch's and the PS5's file systems have no rename that refuses
    an existing target: one checked first, which no remote sender can race,
    as only this process writes the console's data folder) */
    {
        struct stat status;
        if (!lstat(to,&status) || errno!=ENOENT) return 0;
        return rename(from,to)==0;
    }
#else
    /* OPFS uses the MCC direct-stage commit strategy instead. Never weaken
     * native directory publication to a check followed by replacing rename. */
    errno=ENOTSUP;
    return 0;
#endif
}

int posix_mcc_share_open(const char *path)
{
    int descriptor;
#ifdef _WIN32
    HANDLE handle;
    BY_HANDLE_FILE_INFORMATION info;
#else
    struct stat status;
#endif
    if (!posix_mcc_share_path_safe(path)) return -1;
#ifdef _WIN32
    handle=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,NULL);
    if (handle==INVALID_HANDLE_VALUE) return -1;
    if (GetFileType(handle)!=FILE_TYPE_DISK || !GetFileInformationByHandle(handle,&info) ||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE))) {
        CloseHandle(handle); return -1;
    }
    descriptor=_open_osfhandle((intptr_t)handle,_O_RDONLY|_O_BINARY);
    if (descriptor<0) CloseHandle(handle);
#else
    descriptor=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (descriptor>=0 && (fstat(descriptor,&status) || !S_ISREG(status.st_mode))) {
        close(descriptor); descriptor=-1;
    }
#endif
    return descriptor;
}

void posix_mcc_share_close(int descriptor)
{
    if (descriptor<0) return;
#ifdef _WIN32
    _close(descriptor);
#else
    close(descriptor);
#endif
}

int posix_mcc_share_remove(const char *path, int directory)
{
    if ((directory!=0 && directory!=1) || !posix_mcc_share_path_safe(path)) return 0;
#ifdef _WIN32
    return directory ? RemoveDirectoryA(path)!=0 : DeleteFileA(path)!=0;
#else
    return (directory ? rmdir(path) : unlink(path))==0;
#endif
}
