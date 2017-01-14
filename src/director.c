/*
 * @(#)msd_dir.c 1.4 87/11/06	Public Domain.
 *
 *  A public domain implementation of BSD directory routines for
 *  MS-DOS.  Written by Michael Rendell ({uunet,utai}michael@garfield),
 *  August 1897
 *
 *  Modified by Ian Stewartson, Data Logic (istewart@datlog.co.uk).
 *
 *  Updates:  1.  To support OS/2 1.x
 *	      2.  To support HPFS long filenames
 *	      3.  To support OS/2 2.x
 *	      4.  To support TurboC
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef __TURBOC__
#  include <malloc.h>
#endif

#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>

#ifdef __TURBOC__
#  include <dir.h>
#endif

#if defined (OS2) || defined (__OS2__)
#  define INCL_DOSFILEMGR
#  define INCL_DOSMISC
#  include <os2.h>

#  if defined (__OS2__)
#    define DISABLE_HARD_ERRORS	DosError (FERR_DISABLEHARDERR)
#    define ENABLE_HARD_ERRORS	DosError (FERR_ENABLEHARDERR)
#  else
#    define DISABLE_HARD_ERRORS	DosError (HARDERROR_DISABLE)
#    define ENABLE_HARD_ERRORS	DosError (HARDERROR_ENABLE)
#  endif

#else
#  include <dos.h>
#  define DISABLE_HARD_ERRORS
#  define ENABLE_HARD_ERRORS
#endif

#if defined (OS2) || defined (__OS2__)
#  define ATTRIBUTES		(FILE_DIRECTORY | FILE_HIDDEN | FILE_SYSTEM | \
				 FILE_NORMAL | FILE_READONLY | FILE_ARCHIVED)
#elif defined (__TURBOC__)
#  define ATTRIBUTES		(FA_RDONLY | FA_HIDDEN | FA_SYSTEM | \
				 FA_DIREC | FA_ARCH)
#else
#  define ATTRIBUTES		(_A_SUBDIR | _A_HIDDEN | _A_SYSTEM | \
				 _A_NORMAL | _A_RDONLY | _A_ARCH)
#endif

/*
 * OS/2 2.x has these missing
 */

#ifndef ENOTDIR
#  define ENOTDIR	120	/* Not a directory			*/
#endif

#ifndef S_IFMT
#  define	S_IFMT	0xf000	/* type of file				*/
#endif

#ifndef S_ISDIR
#  define S_ISDIR(m)	((((m) & S_IFMT) == S_IFDIR))
#endif

/*
 * Internals
 */

typedef struct _dircontents	DIRCONT;
static void			free_dircontents (DIRCONT *);

/*
 * Open the directory stream
 */

DIR		*opendir (name)
const char	*name;
{
    struct stat		statb;
    DIR			*dirp;
    char		*last;
    DIRCONT		*dp;
    char		*nbuf;
#if defined (__OS2__)
    FILEFINDBUF3 	dtabuf;
    HDIR		d_handle = HDIR_SYSTEM;
    ULONG		d_count = 1;
    bool		HPFS = FALSE;
#elif defined (OS2)
    FILEFINDBUF 	dtabuf;
    HDIR		d_handle = HDIR_SYSTEM;
    USHORT		d_count = 1;
    bool		HPFS = FALSE;
#elif defined (__TURBOC__)
    struct ffblk	dtabuf;
#else
    struct find_t	dtabuf;
#endif
    int			len = strlen (name);
    unsigned long	rc;

    if (!len)
    {
	errno = ENOTDIR;
	return (DIR *)NULL;
    }

    if ((nbuf = malloc (len + 5)) == (char *)NULL)
	return (DIR *) NULL;

    strcpy (nbuf, name);
    last = &nbuf[len - 1];

/* Ok, DOS is very picky about its directory names.  The following are
 * valid.
 *
 *  c:/
 *  c:.
 *  c:name/name1
 *
 *  c:name/ is not valid
 */

    if (((*last == '\\') || (*last == '/')) && (len > 1) &&
	(!((len == 3) && (name[1] == ':'))))
	*(last--) = 0;

/* Check its a directory */

    DISABLE_HARD_ERRORS;
    rc = stat (nbuf, &statb);
    ENABLE_HARD_ERRORS;

    if (rc)
    {
	free (nbuf);
	return (DIR *) NULL;
    }

    if (!S_ISDIR (statb.st_mode))
    {
	free (nbuf);
	errno = ENOTDIR;
	return (DIR *)NULL;
    }

    if ((dirp = (DIR *) malloc (sizeof (DIR))) == (DIR *) NULL)
    {
	free (nbuf);
	return (DIR *) NULL;
    }

/* Set up to find everything */

    if ((*last != '\\') && (*last != '/'))
	strcat (last, "/");

    strcat (last, "*.*");

/* For OS/2, find the file system type */

#if defined (OS2) || defined (__OS2__)
    HPFS = IsHPFSFileSystem (nbuf);
#endif

    dirp->dd_loc      = 0;
    dirp->dd_cp       = (DIRCONT *) NULL;
    dirp->dd_contents = (DIRCONT *) NULL;

    DISABLE_HARD_ERRORS;

#  if defined (__OS2__)
    rc = DosFindFirst (nbuf, &d_handle, ATTRIBUTES, &dtabuf,
		       sizeof (FILEFINDBUF3), &d_count, FIL_STANDARD);
#  elif defined (OS2)
    rc = DosFindFirst (nbuf, &d_handle, ATTRIBUTES, &dtabuf,
		       sizeof (FILEFINDBUF), &d_count, (ULONG)0);
#elif defined (__TURBOC__)
    rc = findfirst (nbuf, &dtabuf, ATTRIBUTES);
#else
    rc = _dos_findfirst (nbuf, ATTRIBUTES, &dtabuf);
#endif

    ENABLE_HARD_ERRORS;

    if (rc)
    {
	free (nbuf);
	free (dirp);
	return (DIR *) NULL;
    }

    do
    {
#if defined (OS2) || defined (__OS2__)
	if (((dp = (DIRCONT *) malloc (sizeof (DIRCONT))) == (DIRCONT *)NULL) ||
	    ((dp->_d_entry = strdup (dtabuf.achName)) == (char *) NULL))
#elif defined (__TURBOC__)
	if (((dp = (DIRCONT *) malloc (sizeof (DIRCONT))) == (DIRCONT *)NULL) ||
	    ((dp->_d_entry = strdup (dtabuf.ff_name)) == (char *) NULL))
#else
	if (((dp = (DIRCONT *) malloc (sizeof (DIRCONT))) == (DIRCONT *)NULL) ||
	    ((dp->_d_entry = strdup (dtabuf.name)) == (char *) NULL))
#endif
	{
	    if (dp->_d_entry != (char *)NULL)
		free ((char *)dp);

	    free (nbuf);
	    free_dircontents (dirp->dd_contents);

#if defined (OS2) || defined (__OS2__)
	    DosFindClose (d_handle);
#endif
	    return (DIR *) NULL;
	}

#if defined (OS2) || defined (__OS2__)
	if (!HPFS)
	    strlwr (dp->_d_entry);
#else
	strlwr (dp->_d_entry);
#endif

	if (dirp->dd_contents != (DIRCONT *) NULL)
	    dirp->dd_cp = dirp->dd_cp->_d_next = dp;

	else
	    dirp->dd_contents = dirp->dd_cp = dp;

	dp->_d_next = (DIRCONT *) NULL;

#if defined (OS2) || defined (__OS2__)
	d_count = 1;
    } while (DosFindNext (d_handle, &dtabuf, sizeof (FILEFINDBUF),
			  &d_count) == 0);
#elif defined (__TURBOC__)
    } while (findnext (&dtabuf) == 0);
#else
    } while (_dos_findnext (&dtabuf) == 0);
#endif

    dirp->dd_cp = dirp->dd_contents;
    free (nbuf);

#if defined (OS2) || defined (__OS2__)
    DosFindClose (d_handle);
#endif

    return dirp;
}


/*
 * Close the directory stream
 */

int	closedir (dirp)
DIR	*dirp;
{
    if (dirp != (DIR *)NULL)
    {
	free_dircontents (dirp->dd_contents);
	free ((char *)dirp);
    }

    return 0;
}

/*
 * Read the next record from the stream
 */

struct dirent	*readdir (dirp)
DIR		*dirp;
{
    static struct dirent	dp;

    if ((dirp == (DIR *)NULL) || (dirp->dd_cp == (DIRCONT *) NULL))
	return (struct dirent *) NULL;

    dp.d_reclen = strlen (strcpy (dp.d_name, dirp->dd_cp->_d_entry));
    dp.d_off    = dirp->dd_loc * 32;
    dp.d_ino    = (ino_t)++dirp->dd_loc;
    dirp->dd_cp = dirp->dd_cp->_d_next;

    return &dp;
}

/*
 * Restart the directory stream
 */

void	rewinddir (dirp)
DIR	*dirp;
{
    seekdir (dirp, (off_t)0);
}

/*
 * Move to a know position in the stream
 */

void	seekdir (dirp, off)
DIR	*dirp;
off_t	off;
{
    long	i = off;
    DIRCONT	*dp;

    if ((dirp == (DIR *)NULL) || (off < 0L))
	return;

    for (dp = dirp->dd_contents; (--i >= 0) && (dp != (DIRCONT *)NULL);
	 dp = dp->_d_next)
	;

    dirp->dd_loc = off - (i + 1);
    dirp->dd_cp = dp;
}

/*
 * Get the current position
 */

off_t	telldir(dirp)
DIR	*dirp;
{
    return (dirp == (DIR *)NULL) ? (off_t) -1 : dirp->dd_loc;
}

/*
 * Release the internal structure
 */

static void	free_dircontents (dp)
DIRCONT		*dp;
{
    DIRCONT	*odp;

    while ((odp = dp) != (DIRCONT *)NULL)
    {
	if (dp->_d_entry != (char *)NULL)
	    free (dp->_d_entry);

	dp = dp->_d_next;
	free ((char *)odp);
    }
}

/*
 * For OS/2, we need to know if we have to convert to lower case.  This
 * only applies to non-HPFS (FAT, NETWARE etc) file systems.
 */

#if defined (OS2) || defined (__OS2__)

/*
 * Define the know FAT systems
 */

static char	*FATSystems[] = {"FAT", "NETWARE", (char *)NULL};

/*
 * Check for Long filenames
 */

bool		IsHPFSFileSystem (char *directory)
{
    ULONG		lMap;
    BYTE		bData[128];
    BYTE		bName[3];
    int			i;
    char		*FName;
    unsigned long	rc;
#if defined (__OS2__)
    ULONG		cbData;
    ULONG		nDrive;
    PFSQBUFFER2		pFSQ = (PFSQBUFFER2)bData;
#else
    USHORT		cbData;
    USHORT		nDrive;
#endif

    if ( _osmode == DOS_MODE )
	return FALSE;

/*
 * Mike tells me there are IFS calls to determine this, but he carn't
 * remember which.  So we read the partition info and check for HPFS.
 */

    if (isalpha (directory[0]) && (directory[1] == ':'))
	nDrive = toupper (directory[0]) - '@';

    else
	DosQCurDisk (&nDrive, &lMap);

/* Set up the drive name */

    bName[0] = (char) (nDrive + '@');
    bName[1] = ':';
    bName[2] = 0;

    cbData = sizeof (bData);

/* Read the info, if we fail - assume non-HPFS */

    DISABLE_HARD_ERRORS;

#  ifdef __OS2__
    rc = DosQFSAttach (bName, 0, FSAIL_QUERYNAME, pFSQ, &cbData);
#  else
    rc = DosQFSAttach (bName, 0, FSAIL_QUERYNAME, bData, &cbData, 0L);
#  endif

    ENABLE_HARD_ERRORS;

    if (rc)
	return FALSE;

#  ifdef __OS2__
    FName = pFSQ->szName + pFSQ->cbName + 1;
#  else
    FName = bData + (*((USHORT *) (bData + 2)) + 7);
#  endif

#ifdef TEST
    printf ("File System for <%s> = <%s>\n", directory, FName);
#endif

    for (i = 0; FATSystems[i] != (char *)NULL; i++)
    {
        if (stricmp (FName, FATSystems[i]) == 0)
	    return FALSE;
    }

    return TRUE;
}
#endif

#ifdef TEST
int	main (int argc, char **argv)
{
    int			i;
    struct dirent	*cdp;
    DIR			*dp;

    for (i = 1; i < argc; i++)
    {
#if defined (OS2) || defined (__OS2__)
	printf ("IsHPFSFileSystem returns %d\n", IsHPFSFileSystem (argv[1]));
#endif

        if ((dp = opendir (argv[i])) == (DIR *)NULL)
	    printf ("Cannot open %s\n", argv[1]);

	else
	{
	    while ((cdp = readdir (dp)) != (struct dirent *)NULL)
		printf ("Found %s\n", cdp->d_name);

	    closedir (dp);
	}
    }

    return 0;
}
#endif
