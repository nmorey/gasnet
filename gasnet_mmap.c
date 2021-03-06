/*   $Source: bitbucket.org:berkeleylab/gasnet.git/gasnet_mmap.c $
 * Description: GASNet memory-mapping utilities
 * Copyright 2002, Dan Bonachea <bonachea@cs.berkeley.edu>
 * Terms of use are as specified in license.txt
 */

#include <gasnet_internal.h>

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#if (PLATFORM_OS_LINUX || PLATFORM_OS_CNL) && !GASNETI_BUG3480_WORKAROUND // Suspect bug 3480
#define GASNETI_BUG3480_MSG "\nYour system is suspected to be impacted by bug 3480"
#else
#define GASNETI_BUG3480_MSG
#endif

#if defined(GASNETI_MMAP_OR_PSHM) && !defined(HAVE_MMAP)
 #if PLATFORM_OS_CYGWIN && (GASNETI_PSHM_POSIX || GASNETI_PSHM_FILE)
  /* Use of mmap() for PSHM over POSIX or FILE is a less-than-general case.
   * So on Cygwin define HAVE_MMAP for those cases only. */
  #define HAVE_MMAP 1
  /* And we need these, which would otherwise appear in gasnet_internal.h */
  extern void *gasneti_mmap(uintptr_t segsize);
  extern void gasneti_munmap(void *segbase, uintptr_t segsize);
 #else
  /* Ensure PSHM over SYSV or GHEAP never actually call mmap() */
  #define mmap %%%ERROR__GASNet_does_not_support_mmap_in_this_configuration%%%
  #define munmap %%%ERROR__GASNet_does_not_support_munmap_in_this_configuration%%%
 #endif
#endif

#if defined(GASNETI_MMAP_OR_PSHM) && defined(GASNETI_USE_HUGETLBFS)
  #define gasneti_mmap_aligndown(sz) gasneti_mmap_aligndown_huge(sz)
  #define gasneti_mmap_pagesize()    gasneti_mmap_pagesize_huge()
#else
  #define gasneti_mmap_aligndown(sz) GASNETI_PAGE_ALIGNDOWN(sz)
  #define gasneti_mmap_pagesize()    GASNETI_PAGESIZE
#endif

#ifdef GASNETI_MMAP_OR_PSHM
 #if GASNET_PSHM && !defined(_POSIX_C_SOURCE) && PLATFORM_OS_SOLARIS
  #define _POSIX_C_SOURCE 200112L /* Required for shm_{open,unlink} decls */
  #include <sys/mman.h>
  #undef _POSIX_C_SOURCE
 #elif HAVE_MMAP
  #include <sys/mman.h>
 #elif !defined(MAP_FAILED)
  #define MAP_FAILED ((void*)(uintptr_t)(-1LL))
 #endif

 #if GASNET_PSHM && defined(GASNETI_PSHM_XPMEM)
  #if defined(HAVE_XPMEM_H)
   /* Cray XPMEM */
   #include <xpmem.h>
   typedef struct xpmem_addr gasneti_xpmem_addr_t;
   typedef xpmem_segid_t gasneti_xpmem_segid_t;
   typedef xpmem_apid_t gasneti_xpmem_apid_t;
   #define gasneti_xpmem_apid apid
  #elif defined(HAVE_SN_XPMEM_H)
   /* SGI XPMEM */
   #include <sn/xpmem.h>
   typedef xpmem_addr_t gasneti_xpmem_addr_t;
   typedef int64_t gasneti_xpmem_segid_t;
   typedef int64_t gasneti_xpmem_apid_t;
   #define gasneti_xpmem_apid id
  #endif
 #endif

 /* TODO:
    Below are a couple cases where we want to use the lower portion of the O/S-provided
    mmap()ed region to create our segment because one must ensure some alignment which
    is greater than the page size.  Instead of forcing the use of the bottom portion,
    we *could* change the trimming logic to use a chosen granularity other than that of
    GASNETI_PAGESIZE (such as the huge page size).
  */

 #ifdef GASNETI_USE_HUGETLBFS
  #include <hugetlbfs.h>
  /* Trim only from top to retain alignment: */
  #undef GASNETI_USE_HIGHSEGMENT
  #define GASNETI_USE_HIGHSEGMENT 0
  /* Provide greater alignment than default: */
  static uintptr_t gasneti_mmap_pagesize_huge() {
     static long pagesz = 0;
     if (!pagesz) pagesz = gethugepagesize();
     return pagesz;
  }
  static uintptr_t gasneti_mmap_aligndown_huge(uintptr_t sz) {
     return GASNETI_ALIGNDOWN(sz, gasneti_mmap_pagesize_huge());
  }
 #endif

 #if GASNET_PSHM && PLATFORM_ARCH_SPARC
  /* On SPARC, shared mappings must be L1 data-cache color-aligned.
     The easiest way to ensure that is to trim only from top to preserve
     the alignment which the O/S gave to the original (untrimmed) mapping.
   Possible TODO:
     Since we only support 64-bit SPARC CPUs the alignment will always be MAX(16K, PAGESIZE).
     So we could just use that for GASNETI_PAGESIZE.
   */
  #undef GASNETI_USE_HIGHSEGMENT
  #define GASNETI_USE_HIGHSEGMENT 0
 #endif

#if !HAVE_MMAP
  /* Skip the following platform checks */
#elif PLATFORM_OS_CYGWIN && !GASNET_PSHM
  #error mmap without PSHM not supported on Cygwin - it doesnt work properly
#elif PLATFORM_ARCH_MIC
  #define GASNETI_MMAP_FLAGS (MAP_ANONYMOUS | MAP_PRIVATE | MAP_POPULATE)
#endif

#ifndef GASNETI_MMAP_FLAGS
  #ifndef GASNETI_MMAP_NORESERVE
    #if defined(MAP_NORESERVE) || defined(HAVE_MAP_NORESERVE)
      /* bug 1358: try to avoid allocating swap space, if possible */
      #define GASNETI_MMAP_NORESERVE  MAP_NORESERVE
    #else
      #define GASNETI_MMAP_NORESERVE  0
    #endif
  #endif
  /* find an appropriate flag for anonymous mmap */
  #if defined(MAP_ANONYMOUS) || defined(HAVE_MAP_ANONYMOUS)
    #define GASNETI_MAP_ANONYMOUS MAP_ANONYMOUS
  #elif defined(MAP_ANON) || defined(HAVE_MAP_ANON)
    #define GASNETI_MAP_ANONYMOUS MAP_ANON
  #else /* assume no direct capability exists, fall back on mapping /dev/zero */
    #define GASNETI_MAP_ANONYMOUS 0
    #ifndef GASNETI_MMAP_FILE
    #define GASNETI_MMAP_FILE "/dev/zero"
    #endif
  #endif
  #define GASNETI_MMAP_FLAGS (GASNETI_MAP_ANONYMOUS | MAP_PRIVATE | GASNETI_MMAP_NORESERVE)
#endif

#ifndef GASNETI_MMAP_FIXED_FLAG
  #define GASNETI_MMAP_FIXED_FLAG MAP_FIXED
#endif
#ifndef GASNETI_MMAP_NOTFIXED_FLAG
  #define GASNETI_MMAP_NOTFIXED_FLAG 0
#endif

/* Flag, if any, to prevent write-back of file-based mappings */
#ifdef MAP_NOSYNC
#define GASNETI_MMAP_NOSYNC_FLAG MAP_NOSYNC
#endif
#ifndef GASNETI_MMAP_NOSYNC_FLAG
  #define GASNETI_MMAP_NOSYNC_FLAG 0
#endif

#if GASNET_PSHM && (PLATFORM_OS_BGQ || PLATFORM_OS_CYGWIN)
  /* BG/Q: MAP_FIXED is ignored for fd obtained from pshm_open() */
  /* CYGWIN: may not honor the address passed to shmat() */
  #define GASNETI_PSHM_MAP_FIXED_IGNORED 1
#endif

#if HAVE_MMAP
/* ------------------------------------------------------------------------------------ */
static void *gasneti_mmap_internal(void *segbase, uintptr_t segsize) {
  static int gasneti_mmapfd = -1;
  gasneti_tick_t t1, t2;
  int mmap_errno;
  void	*ptr;

  #ifdef GASNETI_MMAP_FILE
    if (gasneti_mmapfd == -1) {
      gasneti_mmapfd = open(GASNETI_MMAP_FILE, O_RDWR);
      if (gasneti_mmapfd == -1) 
        gasneti_fatalerror("failed to open "GASNETI_MMAP_FILE" for mmap : %s\n",strerror(errno));
    }
  #endif

  t1 = gasneti_ticks_now();
#if defined(GASNETI_USE_HUGETLBFS)
  ptr = gasneti_huge_mmap(segbase, segsize);
#else
  ptr = mmap(segbase, segsize, (PROT_READ|PROT_WRITE), 
      (GASNETI_MMAP_FLAGS | (segbase==NULL?GASNETI_MMAP_NOTFIXED_FLAG:GASNETI_MMAP_FIXED_FLAG)), 
      gasneti_mmapfd, 0);
#endif
  mmap_errno = errno;
  t2 = gasneti_ticks_now();

  #ifdef GASNETI_MMAP_FILE
    /* close(gasneti_mmapfd);  NOPE: we reuse one static fd */
  #endif

  GASNETI_TRACE_PRINTF(C, 
      ("mmap %s("GASNETI_LADDRFMT", %"PRIuPTR"): %.3fus => "GASNETI_LADDRFMT"%s%s\n", 
        (segbase == NULL?"":"fixed"),
        GASNETI_LADDRSTR(segbase), segsize,
        gasneti_ticks_to_ns(t2-t1)/1000.0,
        GASNETI_LADDRSTR(ptr),
        (ptr == MAP_FAILED?"  MAP_FAILED: ":""),
        (ptr == MAP_FAILED?strerror(mmap_errno):"")));

  if (ptr == MAP_FAILED && mmap_errno != ENOMEM) {
    #if PLATFORM_OS_CYGWIN
      if (mmap_errno != EACCES) /* Cygwin stupidly returns EACCES for insuff mem */
    #elif PLATFORM_OS_SOLARIS
      if (mmap_errno != EAGAIN) /* Solaris stupidly returns EAGAIN for insuff mem */
    #endif
    gasneti_fatalerror("unexpected error in mmap%s for size %"PRIuPTR": %s\n", 
                       (segbase == NULL?"":" fixed"),
                       segsize, strerror(mmap_errno));
  }

  if ((ptr != (void*)GASNETI_PAGE_ALIGNDOWN(ptr)) && (ptr != MAP_FAILED)) {
      gasneti_fatalerror("mmap result "GASNETI_LADDRFMT" is not aligned to GASNET_PAGESIZE %lu (0x%lx)",
              GASNETI_LADDRSTR(ptr), (unsigned long)GASNET_PAGESIZE, (unsigned long)GASNET_PAGESIZE);
  }
  if (segbase && ptr == MAP_FAILED) {
    #if GASNETI_BUG3480_WORKAROUND
      if (mmap_errno == ENOMEM) {
         errno = ENOMEM;
         return MAP_FAILED; // Caller will retry
      }
    #endif
      gasneti_fatalerror("mmap fixed failed at "GASNETI_LADDRFMT" for size %"PRIuPTR": %s" GASNETI_BUG3480_MSG,
	      GASNETI_LADDRSTR(segbase), segsize, strerror(mmap_errno));
  }
  if (segbase && segbase != ptr) {
    gasneti_fatalerror("mmap fixed moved from "GASNETI_LADDRFMT" to "GASNETI_LADDRFMT" for size %"PRIuPTR"\n",
	    GASNETI_LADDRSTR(segbase), GASNETI_LADDRSTR(ptr), segsize);
  }
  return ptr;
}
extern void *gasneti_mmap_fixed(void *segbase, uintptr_t segsize) {
  return gasneti_mmap_internal(segbase, segsize);
}
extern void *gasneti_mmap(uintptr_t segsize) {
  return gasneti_mmap_internal(NULL, segsize);
}
#endif /* HAVE_MMAP */

#if GASNET_PSHM

/* an array of filenames/keys with length 1+gasneti_pshm_nodes, the +1 is for AMs */
#if defined(GASNETI_PSHM_SYSV)
  #include <sys/shm.h>
  static key_t *gasneti_pshm_sysvkeys = NULL;
#elif defined(GASNETI_PSHM_FILE) || defined(GASNETI_PSHM_POSIX)
  static char **gasneti_pshmname = NULL;
#elif defined(GASNETI_PSHM_XPMEM)
  static gasneti_xpmem_segid_t *gasneti_pshm_segids = NULL;
  static gasneti_xpmem_apid_t *gasneti_pshm_apids = NULL;
#elif defined(GASNETI_PSHM_GHEAP)
  static void* gasneti_pshm_valloc(size_t size);
  static void gasneti_pshm_vfree(void *addr);
  static void **gasneti_pshm_segaddrs = NULL;
#endif

static char *gasneti_pshm_tmpfile_ = NULL;
static char *gasneti_pshm_tmpfile = NULL;
#define GASNETI_PSHM_PREFIX_LEN1  6  /* "/GASNT" */
#define GASNETI_PSHM_PREFIX_LEN   (GASNETI_PSHM_PREFIX_LEN1 + GASNETI_PSHM_UNIQUE_LEN)

static int gasneti_pshm_mkstemp(const char *prefix, const char *tmpdir) {
  int tmpfd;

  if (!tmpdir || !strlen(tmpdir)) {
    errno = ENOTDIR;
    return -1;
  }
  gasneti_pshm_tmpfile_ = gasneti_realloc(gasneti_pshm_tmpfile_, strlen(tmpdir) + GASNETI_PSHM_PREFIX_LEN + 1);
  strcpy(gasneti_pshm_tmpfile_, tmpdir);
  strcat(gasneti_pshm_tmpfile_, prefix);

  /* Now try to create a unique file in the given directory */
  tmpfd = mkstemp(gasneti_pshm_tmpfile_);
  if (tmpfd >= 0) {
    gasneti_local_wmb();
    gasneti_pshm_tmpfile = gasneti_pshm_tmpfile_;
    close(tmpfd);
    return 0;
  } else {
    return -1;
  }
}

#ifdef GASNETI_PSHM_SYSV
static int gasneti_pshm_settemp(const char *unique, const char *prefix, const char *tmpdir) {
  int tmpfd;
  int len;

  if (gasneti_pshm_tmpfile) return 0;

  if (!tmpdir || !strlen(tmpdir)) {
    errno = ENOTDIR;
    return -1;
  }
  gasneti_pshm_tmpfile_ = gasneti_realloc(gasneti_pshm_tmpfile_, strlen(tmpdir) + GASNETI_PSHM_PREFIX_LEN + 1);
  strcpy(gasneti_pshm_tmpfile_, tmpdir);
  strcat(gasneti_pshm_tmpfile_, prefix);

  /* Note: 'unique' might not be NUL terminated */
  len = strlen(gasneti_pshm_tmpfile_);
  memcpy(gasneti_pshm_tmpfile_ + len - GASNETI_PSHM_UNIQUE_LEN, unique, GASNETI_PSHM_UNIQUE_LEN);

  /* Now try to verify the file exists */
  tmpfd = open(gasneti_pshm_tmpfile_, O_RDWR);
  if (tmpfd >= 0) {
    gasneti_local_wmb();
    gasneti_pshm_tmpfile = gasneti_pshm_tmpfile_;
    close(tmpfd);
    return 0;
  } else {
    return -1;
  }
}
#endif

#if defined(GASNETI_PSHM_FILE) || defined(GASNETI_PSHM_SYSV) || defined(GASNETI_PSHM_POSIX)
static const char *gasneti_pshm_makeunique(const char *unique) {
  static char prefix[] = "/GASNTXXXXXX";
#if defined(GASNETI_PSHM_FILE) || defined(GASNETI_PSHM_SYSV)
  const char *tmpdir = gasneti_tmpdir();
#endif
  size_t tmpdir_len = 0;
#if !defined(GASNETI_PSHM_SYSV)
  size_t base_len;
  char *allnames;
#endif
  int i;

  gasneti_assert(strlen(prefix) == GASNETI_PSHM_PREFIX_LEN);

#if defined(GASNETI_PSHM_FILE) && defined(GASNETI_USE_HUGETLBFS)
  { const char *hugedir = hugetlbfs_find_path();
    if (hugedir && !access(hugedir, R_OK|W_OK|X_OK)) {
      tmpdir = hugedir;
    }
  }
#endif
#if defined(GASNETI_PSHM_FILE) || defined(GASNETI_PSHM_SYSV)
  tmpdir_len = strlen(tmpdir);
#endif

  if (!unique) { /* We get to pick the unique bits */
#if defined(GASNETI_PSHM_FILE) || defined(GASNETI_PSHM_SYSV)
    if (gasneti_pshm_mkstemp(prefix, tmpdir)) {
      gasneti_fatalerror("mkstemp() failed to find a unique prefix: %s", strerror(errno));
    }
#else
    /* We do NOT honor $TMPDIR, since setting it to a job-specific
     * value would interfere with our purpose here of finding a
     * name that is unique per-NODE. */
    if (   gasneti_pshm_mkstemp(prefix, "/tmp")
        && gasneti_pshm_mkstemp(prefix, "/var/tmp")
        && gasneti_pshm_mkstemp(prefix, "/usr/tmp")
  #if PLATFORM_OS_LINUX
        && gasneti_pshm_mkstemp(prefix, "/dev/shm")
  #endif
       ) {
      /* We'll HOPE that our pid's low 24 bits are good enough for node-scope uniqueness */
      static const char pattern[] = "/GASNT%06x";
      gasneti_pshm_tmpfile = gasneti_realloc(gasneti_pshm_tmpfile, sizeof(prefix)); /* inc. \0 */
      snprintf(gasneti_pshm_tmpfile, sizeof(prefix), pattern, (0xFFFFFFU & (unsigned int)getpid()));
    }
#endif
    /* Don't unlink() it until we no longer require uniqueness */

    /* Strip away the tmpdir to yield a unique prefix */
    unique = strrchr(gasneti_pshm_tmpfile, '/');
    gasneti_assert(strlen(unique) == GASNETI_PSHM_PREFIX_LEN);
    unique += GASNETI_PSHM_PREFIX_LEN1;
  }

#if defined(GASNETI_PSHM_SYSV)
  gasneti_pshm_settemp(unique, prefix, tmpdir);
  key_t *keys = (key_t *)gasneti_malloc((gasneti_pshm_nodes+1)*sizeof(key_t));;
  for (i = 0; i <= gasneti_pshm_nodes; ++i) {
    key_t key = ftok(gasneti_pshm_tmpfile, i + 1);
    if (key == (key_t)-1){
        gasneti_fatalerror("failed to produce a unique SYSV key value for %s and rank %d, from ftok: %s",
                           gasneti_pshm_tmpfile, i, strerror(errno));
    }
  #if GASNETI_PSHM_MAX_NODES > 255
    else { /* ftok() is documented (on many systems) as using only low 8 bits - so verify */
      int j;
      for (j = 0; j < i; ++j) {
        if_pf (key == keys[j]) {
          key = (key_t)-1;
          gasneti_fatalerror("failed to produce a unique SYSV key value for %s and rank %d, dup of %d",
                             gasneti_pshm_tmpfile, i, j);
        }
      }
    }
  #endif
    keys[i] = key;
  }
  gasneti_local_wmb();
  gasneti_pshm_sysvkeys = keys;
#else
  /* Three base-36 "digits" provide 46,656 unique names, even if case-insensitive. */
 #if GASNETI_PSHM_MAX_NODES > 255
  gasneti_assert_always(gasneti_pshm_nodes < (36*36*36));
 #endif

  /* Note: 'unique' might not be NUL terminated */
  memcpy(prefix + GASNETI_PSHM_PREFIX_LEN1, unique, GASNETI_PSHM_UNIQUE_LEN);

  char **names = (char **)gasneti_malloc((gasneti_pshm_nodes+1)*sizeof(char*));
  base_len = tmpdir_len + GASNETI_PSHM_PREFIX_LEN;
  allnames = (char *)gasneti_malloc((gasneti_pshm_nodes+1)*(base_len + 4));

  for (i = 0; i <= gasneti_pshm_nodes; ++i) {
    const char tbl[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const unsigned int namelen = (base_len + 4);
    char *filename = allnames + (i * namelen);
    const int base = 36;
    int digit = i;

 #ifdef GASNETI_PSHM_FILE
    memcpy(filename, tmpdir, tmpdir_len);
 #endif
    memcpy(filename + tmpdir_len, prefix, GASNETI_PSHM_PREFIX_LEN);

    filename[base_len + 2] = tbl[digit % base]; digit /= base;
    filename[base_len + 1] = tbl[digit % base]; digit /= base;
    filename[base_len + 0] = tbl[digit];
    filename[base_len + 3] = '\0';

    names[i] = filename;
  }
  gasneti_local_wmb();
  gasneti_pshmname = names;
#endif

  return unique;
}
#endif
#endif /* GASNET_PSHM */

#if defined(GASNETI_USE_HUGETLBFS)

/* Apply the default hugepage size for mapping of the requested size.
 * The given size is adjusted for proper alignment and returned.
 */
static uintptr_t huge_pagesz(void *addr, uintptr_t size) {
  static long pagesz = 0;
  if (!pagesz) pagesz = gethugepagesize();
  gasneti_assert((uintptr_t)addr % pagesz == 0); /* alignment check */
  return GASNETI_ALIGNUP(size, pagesz);
}

extern void *gasneti_huge_mmap(void *addr, uintptr_t size) {
  int fd = hugetlbfs_unlinked_fd();
  const int mmap_flags = MAP_SHARED | (addr ? GASNETI_MMAP_FIXED_FLAG : GASNETI_MMAP_NOTFIXED_FLAG);
  void *ptr = mmap(addr, huge_pagesz(addr, size), (PROT_READ|PROT_WRITE), mmap_flags, fd, 0);

  int save_errno = errno;
  (void) close(fd);
  errno = save_errno;

  return ptr;
}

extern void gasneti_huge_munmap(void *addr, uintptr_t size) {
  if (munmap(addr, huge_pagesz(addr, size)) != 0)
    gasneti_fatalerror("munmap("GASNETI_LADDRFMT",%"PRIuPTR") failed: %s\n",
                       GASNETI_LADDRSTR(addr), size, strerror(errno));
}

#endif /* defined(GASNETI_USE_HUGETLBFS) */

#if GASNET_PSHM

static void gasneti_pshm_unlink(int pshm_rank);

/* create the object/region/segment and return its address */
static void * __attribute__((noinline)) gasneti_pshm_mmap(int pshm_rank, void *segbase, size_t segsize) {
  const int create = (pshm_rank == gasneti_pshm_mynode) ||
                     ((pshm_rank == gasneti_pshm_nodes) && !gasneti_pshm_mynode);
  void * ptr = MAP_FAILED;

#if defined(GASNETI_PSHM_SYSV)
  const int flags = S_IRUSR | S_IWUSR | (create ? ( IPC_CREAT | IPC_EXCL ) : 0);

  /* Create and size in 1 step */
  int id = shmget(gasneti_pshm_sysvkeys[pshm_rank], segsize, flags);
  if (id == -1) {
    #if PLATFORM_OS_CYGWIN
    if (errno == ENOSYS) {
      gasneti_fatalerror("Cygwin's SystemV shared memory support is not enabled.");
    }
    #endif
    if (errno == EINVAL) {
      // shmget() returns EINVAL for (size > SHMMAX), among other causes
      // TODO: can we confirm that is the actual cause?
      errno = ENOMEM;
    }
    if (errno == ENOSPC) {
      // shmget() returns ENOSPC for (total_size > SHMALL), among other causes
      // TODO: can we confirm that is the actual cause?
      errno = ENOMEM;
    }
    return MAP_FAILED;
  }

  /* map */
  ptr = shmat(id, segbase, 0);

  if (ptr == (void *)(intptr_t)(-1)) ptr = MAP_FAILED;
#elif defined(GASNETI_PSHM_FILE) || defined(GASNETI_PSHM_POSIX)
  const int flags = O_RDWR | (create ? ( O_CREAT | O_EXCL ) : 0);
  const char *filename = gasneti_pshmname[pshm_rank];
  int fd = -1;

  /* create or open */
  #if defined(GASNETI_PSHM_FILE)
   #if defined(GASNETI_USE_HUGETLBFS)
    segsize = huge_pagesz(segbase, segsize);
   #endif
    fd = open(filename, flags, S_IRUSR | S_IWUSR);
  #elif defined(GASNETI_PSHM_POSIX)
    fd = shm_open(filename, flags, S_IRUSR | S_IWUSR);
    #if PLATFORM_OS_DARWIN
      if ((fd == -1) && (errno == EEXIST)) {
        /* Work around Darwin stupidity observed by Filip */
        int retries_remain = 32;
        do {
          gasneti_sched_yield();
          fd = shm_open(filename, flags, S_IRUSR | S_IWUSR);
        } while ((fd == -1) && (errno == EEXIST) && retries_remain--);
      }
    #endif
  #else
    #error
  #endif
  if (fd == -1) return MAP_FAILED;

  /* size on create only */
  if (create && ftruncate(fd, segsize)) {
    /* resize failed - fall through */
  } else {
    /* map */
    const int mmap_flags = MAP_SHARED | GASNETI_MMAP_NOSYNC_FLAG |
                           (segbase ? GASNETI_MMAP_FIXED_FLAG : GASNETI_MMAP_NOTFIXED_FLAG);
    ptr = mmap(segbase, segsize, (PROT_READ|PROT_WRITE), mmap_flags, fd, 0);
  }

  {
    const int save_errno = errno;
    (void) close(fd);
    errno = save_errno;
  }
#elif defined(GASNETI_PSHM_XPMEM)
  if (create) {
  #if GASNETI_USE_HUGETLBFS
    ptr = gasneti_huge_mmap(segbase, segsize);
  #else
    const int mmap_flags = GASNETI_MAP_ANONYMOUS | MAP_SHARED | \
                           (segbase ? GASNETI_MMAP_FIXED_FLAG : GASNETI_MMAP_NOTFIXED_FLAG);
    ptr = mmap(segbase, segsize, (PROT_READ|PROT_WRITE), mmap_flags, 0, 0);
  #endif
  } else {
    gasneti_xpmem_apid_t apid =
  #if HAVE_XPMEM_MAKE_2
            xpmem_get_2(gasneti_pshm_segids[pshm_rank], XPMEM_RDWR, XPMEM_PERMIT_MODE, NULL);
  #else
              xpmem_get(gasneti_pshm_segids[pshm_rank], XPMEM_RDWR, XPMEM_PERMIT_MODE, NULL);
  #endif
    if (apid != (gasneti_xpmem_apid_t)-1) {
    #if HAVE_XPMEM_MAKE_2
      ptr = xpmem_attach_2(apid, 0, segsize, segbase);
    #else
      gasneti_xpmem_addr_t xa;
      xa.offset = 0;
      xa.gasneti_xpmem_apid = apid;
      ptr = xpmem_attach(xa, segsize, segbase);
    #endif
    }
    gasneti_pshm_apids[pshm_rank] = apid;
  }
#elif defined(GASNETI_PSHM_GHEAP)
  if (create) {
    ptr = gasneti_pshm_valloc(segsize);
  } else {
    ptr = gasneti_pshm_segaddrs[pshm_rank];
  }
#else
  #error
#endif

  if_pf (create && (ptr == MAP_FAILED)) {
    const int save_errno = errno;
    gasneti_pshm_unlink(pshm_rank);
    errno = save_errno;
  }
  return ptr;
}

/* Helper: remove local object's mapping from address space */
static void gasneti_pshm_munmap(void *segbase, uintptr_t segsize) {
  gasneti_assert(segsize > 0);
#if defined(GASNETI_PSHM_SYSV)
  if (shmdt(segbase) != 0) {
      gasneti_fatalerror("shmdt("GASNETI_LADDRFMT") failed: %s\n",
	      GASNETI_LADDRSTR(segbase), strerror(errno));
  }
#elif defined(GASNETI_PSHM_FILE) || defined(GASNETI_PSHM_POSIX) || defined(GASNETI_PSHM_XPMEM)
  gasneti_munmap(segbase, segsize);
#elif defined(GASNETI_PSHM_GHEAP)
  gasneti_pshm_vfree(segbase);
#else
  #error
#endif
  gasneti_pshm_unlink(gasneti_pshm_mynode);
}

static void gasneti_munmap_remote(gasnet_node_t pshm_rank, void *segbase, uintptr_t segsize) {
  gasneti_assert(segsize > 0);

#if defined(GASNETI_PSHM_SYSV)
  if (shmdt(segbase) != 0) {
      gasneti_fatalerror("shmdt("GASNETI_LADDRFMT") failed: %s\n",
	      GASNETI_LADDRSTR(segbase), strerror(errno));
  }
#elif defined(GASNETI_PSHM_FILE) || defined(GASNETI_PSHM_POSIX)
  gasneti_munmap(segbase, segsize);
#elif defined(GASNETI_PSHM_XPMEM)
 #if HAVE_XPMEM_MAKE_2
  xpmem_detach_2(segbase, segsize);
  xpmem_release_2(gasneti_pshm_apids[pshm_rank]);
 #else
  xpmem_detach(segbase);
  xpmem_release(gasneti_pshm_apids[pshm_rank]);
 #endif
#elif defined(GASNETI_PSHM_GHEAP)
 /* Nothing to do here */
#else
  #error
#endif
}

/* Called collectively */
GASNETI_INLINE(gasneti_export_segment)
void gasneti_export_segment(void *segbase, uintptr_t segsize) {
#if defined(GASNETI_PSHM_XPMEM)
  /* Create and supernode-exchange xpmem segment ids */
  gasneti_xpmem_segid_t segid =
  #if HAVE_XPMEM_MAKE_2
          xpmem_make_2(segbase, segsize, XPMEM_PERMIT_MODE, (void *)(uintptr_t)0600);
  #else
            xpmem_make(segbase, segsize, XPMEM_PERMIT_MODE, (void *)(uintptr_t)0600);
  #endif
  if_pf (segid == (gasneti_xpmem_segid_t)(-1)) {
    fprintf(stderr, "xpmem_make() failed:%s\n", strerror(errno));
  }
  gasneti_pshmnet_bootstrapExchange(gasneti_request_pshmnet, &segid, sizeof(segid), gasneti_pshm_segids);
#elif defined(GASNETI_PSHM_GHEAP)
  gasneti_pshmnet_bootstrapExchange(gasneti_request_pshmnet, &segbase, sizeof(segbase), gasneti_pshm_segaddrs);
#else
  /* empty */
#endif
}

/* Helper: destroy the "attach point" */
static void gasneti_pshm_unlink(int pshm_rank){
#if defined(GASNETI_PSHM_SYSV)
  int shmget_id = shmget(gasneti_pshm_sysvkeys[pshm_rank], 0, 0);
  (void)shmctl(shmget_id, IPC_RMID, NULL);
#elif defined(GASNETI_PSHM_FILE)
  const char *filename = gasneti_pshmname[pshm_rank];
  (void)unlink(filename);
#elif defined(GASNETI_PSHM_POSIX)
  const char *filename = gasneti_pshmname[pshm_rank];
  (void)shm_unlink(filename);
#elif defined(GASNETI_PSHM_XPMEM) || defined(GASNETI_PSHM_GHEAP)
  /* Nothing to do */
#else
  #error
#endif
}

/* gasneti_pshm_unlink() so the shared memory will disappear upon exit.
 * This must be called collectively, because barriers are
 * used to prevent races against shm_open() before or after.
 */
static void gasneti_unlink_segments(void) {
  gasneti_pshmnet_bootstrapBarrier();
#if defined(GASNETI_PSHM_XPMEM) || defined(GASNETI_PSHM_GHEAP)
  /* Nothing to do because gasneti_pshm_unlink() is a no-op */
#else
  gasneti_pshm_unlink(gasneti_pshm_mynode);
  gasneti_pshmnet_bootstrapBarrier();
#endif
}

/* Try to unlink everything we can, ignoring errors */
static void gasneti_cleanup_shm(void) {
#ifdef GASNETI_PSHM_SYSV
  /* Unlink the segments and vnet */
  if (gasneti_pshm_sysvkeys) {
    gasneti_local_rmb();
    for (int i=0; i<gasneti_pshm_nodes+1; ++i) {
      gasneti_pshm_unlink(i);
    }
    gasneti_free(gasneti_pshm_sysvkeys);
    gasneti_pshm_sysvkeys = NULL;
  }
#elif defined(GASNETI_PSHM_FILE) || defined(GASNETI_PSHM_POSIX)
  if (gasneti_pshmname) {
    gasneti_local_rmb();
    /* Unlink the segments and vnet, and free the filenames */
    int i;
    for (i=0; i<gasneti_pshm_nodes+1; ++i) {
      gasneti_pshm_unlink(i);
    }
    gasneti_free(gasneti_pshmname[0]);
    gasneti_free(gasneti_pshmname);
    gasneti_pshmname = NULL;
  }
#elif defined(GASNETI_PSHM_XPMEM)
  gasneti_free(gasneti_pshm_segids);
  gasneti_pshm_segids = NULL;
  gasneti_free(gasneti_pshm_apids);
  gasneti_pshm_apids = NULL;
#elif defined(GASNETI_PSHM_GHEAP)
  gasneti_free(gasneti_pshm_segaddrs);
  gasneti_pshm_segaddrs = NULL;
#else
  #error
#endif

  /* Remove the tmpfile that ensures uniqueness of our filenames */
  if (gasneti_pshm_tmpfile) {
    gasneti_local_rmb();
    (void)unlink(gasneti_pshm_tmpfile);
    gasneti_free(gasneti_pshm_tmpfile);
    gasneti_pshm_tmpfile = NULL;
  }
}

static void  __attribute__((noinline)) *gasneti_mmap_shared_internal(int pshmnode, void *segbase, uintptr_t segsize,
                                          int may_fail) {
  int mmap_errno;
  gasneti_tick_t t1, t2;
  void	*ptr;

  /* 0-byte failure modes can vary by implemenation */
  if (!segsize) {
    if (!may_fail) {
      gasneti_cleanup_shm();
      gasneti_fatalerror("failed to setup 0-byte shared memory file/segment for node %d", pshmnode);
    }
    return MAP_FAILED;
  }

  t1 = gasneti_ticks_now();
  ptr = gasneti_pshm_mmap(pshmnode, segbase, segsize);
  mmap_errno = errno;
  t2 = gasneti_ticks_now();

  GASNETI_TRACE_PRINTF(C, 
      ("mmap %s("GASNETI_LADDRFMT", %"PRIuPTR"): %.3fus => "GASNETI_LADDRFMT"%s%s\n", 
        (segbase == NULL?"":"fixed"),
        GASNETI_LADDRSTR(segbase), segsize,
        gasneti_ticks_to_ns(t2-t1)/1000.0,
        GASNETI_LADDRSTR(ptr),
        (ptr == MAP_FAILED?"  MAP_FAILED: ":""),
        (ptr == MAP_FAILED?strerror(mmap_errno):"")));

  if ((ptr == MAP_FAILED) && !may_fail) {
  #if GASNETI_BUG3480_WORKAROUND
    if (segbase && (mmap_errno == ENOMEM)) {
      errno = ENOMEM;
      return MAP_FAILED; // Caller will retry
    }
  #endif

    gasneti_cleanup_shm();

    if (mmap_errno != ENOMEM) {
      #if PLATFORM_OS_CYGWIN
        if (mmap_errno != EACCES) /* Cygwin stupidly returns EACCES for insuff mem */
      #elif PLATFORM_OS_SOLARIS
        if (mmap_errno != EAGAIN) /* Solaris stupidly returns EAGAIN for insuff mem */
      #endif
      gasneti_fatalerror("unexpected error in mmap%s for size %"PRIuPTR": %s\n", 
                         (segbase == NULL?"":" fixed"),
                         segsize, strerror(mmap_errno));
    }

    if (!segbase) {
      gasneti_fatalerror("mmap failed for size %"PRIuPTR": %s", segsize, strerror(mmap_errno));
    } else {
      gasneti_fatalerror("mmap fixed failed at "GASNETI_LADDRFMT" for size %"PRIuPTR": %s" GASNETI_BUG3480_MSG,
              GASNETI_LADDRSTR(segbase), segsize, strerror(mmap_errno));
    }
  }

  if ((ptr != (void*)GASNETI_PAGE_ALIGNDOWN(ptr)) && (ptr != MAP_FAILED)) {
    gasneti_cleanup_shm();
    gasneti_fatalerror("mmap result "GASNETI_LADDRFMT" is not aligned to GASNET_PAGESIZE %lu (0x%lx)",
              GASNETI_LADDRSTR(ptr), (unsigned long)GASNET_PAGESIZE, (unsigned long)GASNET_PAGESIZE);
  }
#if !GASNETI_PSHM_MAP_FIXED_IGNORED
  if (segbase && (segbase != ptr) && (ptr != MAP_FAILED)) {
    gasneti_cleanup_shm();
    gasneti_fatalerror("mmap fixed moved from "GASNETI_LADDRFMT" to "GASNETI_LADDRFMT" for size %"PRIuPTR,
            GASNETI_LADDRSTR(segbase), GASNETI_LADDRSTR(ptr), segsize);
  }
#endif

  return ptr;
}

static void *gasneti_mmap_remote_shared(void *segbase, uintptr_t segsize, gasnet_node_t pshmnode) {
  gasneti_assert(pshmnode < gasneti_pshm_nodes);
  return gasneti_mmap_shared_internal(pshmnode, segbase, segsize, 0);
}
extern void *gasneti_mmap_shared_fixed(void *segbase, uintptr_t segsize) {
  return gasneti_mmap_shared_internal(gasneti_pshm_mynode, segbase, segsize, 0);
}
extern void *gasneti_mmap_shared(uintptr_t segsize) {
  return gasneti_mmap_shared_internal(gasneti_pshm_mynode, NULL, segsize, 1);
}

static void* gasneti_vnet_addr;
static uintptr_t gasneti_vnet_size;

extern void *gasneti_mmap_vnet(uintptr_t size, gasneti_bootstrapBroadcastfn_t snodebcastfn) {
  void *ptr = MAP_FAILED;
  int save_errno = 0;

  #if defined(GASNETI_PSHM_SYSV) && PLATFORM_OS_CYGWIN
  /* Cygwin may raise SIGSYS when SysV support is absent.
     This will yield more informative error messages. */
  gasneti_sighandlerfn_t prev_handler = gasneti_reghandler(SIGSYS, SIG_IGN);
  #endif

  #if defined(GASNETI_PSHM_FILE) || defined(GASNETI_PSHM_SYSV) || defined(GASNETI_PSHM_POSIX)
  {
    char unique[GASNETI_PSHM_UNIQUE_LEN];

    /* First in each supernode creates the segment plus the names/keys and returns the unique identifier */
    if (gasneti_pshm_mynode == 0) {
      const char *tmp = gasneti_pshm_makeunique(NULL);
      memcpy(unique, tmp, GASNETI_PSHM_UNIQUE_LEN);
      ptr = gasneti_mmap_shared_internal(gasneti_pshm_nodes, NULL, size, 1);
      save_errno = errno;
    }

    /* Supernode-scoped bcast communicates the names/keys generated by the firsts */
    (*snodebcastfn)(unique, GASNETI_PSHM_UNIQUE_LEN, unique, gasneti_pshm_firstnode);
  
    /* Non-first nodes attach */
    if (gasneti_pshm_mynode != 0) {
      (void)gasneti_pshm_makeunique(unique);
      ptr = gasneti_mmap_shared_internal(gasneti_pshm_nodes, NULL, size, 1);
      save_errno = errno;
    }
  }
  #elif defined(GASNETI_PSHM_XPMEM)
  {
    gasneti_xpmem_segid_t segid = (gasneti_xpmem_segid_t)(-1);

    /* Initialization */
    gasneti_pshm_segids = gasneti_malloc(sizeof(gasneti_xpmem_segid_t) * (gasneti_pshm_nodes + 1));
    gasneti_pshm_apids = gasneti_malloc(sizeof(gasneti_xpmem_apid_t) * (gasneti_pshm_nodes + 1));

    /* First in each supernode creates the segment */
    if (gasneti_pshm_mynode == 0) {
      ptr = gasneti_mmap_shared_internal(gasneti_pshm_nodes, NULL, size, 1);
      save_errno = errno;
      if (ptr != MAP_FAILED) {
      #if HAVE_XPMEM_MAKE_2
        segid = xpmem_make_2(ptr, size, XPMEM_PERMIT_MODE, (void *)(uintptr_t)0600);
      #else
        segid =   xpmem_make(ptr, size, XPMEM_PERMIT_MODE, (void *)(uintptr_t)0600);
      #endif
        save_errno = errno;
        if_pf (segid == (gasneti_xpmem_segid_t)(-1)) {
          fprintf(stderr, "xpmem_make() failed:%s\n", strerror(errno));
        }
      }
    }
    
    /* Supernode-scoped bcast communicates the segment identifier generated by the firsts */
    (*snodebcastfn)(&segid, sizeof(segid),
                    &gasneti_pshm_segids[gasneti_pshm_nodes],
                    gasneti_pshm_firstnode);

    /* Non-first nodes attach */
    if (gasneti_pshm_mynode != 0) {
      ptr = gasneti_mmap_shared_internal(gasneti_pshm_nodes, NULL, size, 1);
      save_errno = errno;
    }
  }
  #elif defined(GASNETI_PSHM_GHEAP)
  {
    /* Initialization */
    gasneti_pshm_segaddrs = gasneti_malloc(sizeof(void*) * (gasneti_pshm_nodes + 1));

    /* First in each supernode creates the segment */
    if (gasneti_pshm_mynode == 0) {
      ptr = gasneti_mmap_shared_internal(gasneti_pshm_nodes, NULL, size, 1);
      save_errno = errno;
      if (ptr != MAP_FAILED) {
        memset(ptr, 0, size);
      }
    }
    
    /* Supernode-scoped bcast communicates the segment addresses allocated by the firsts */
    (*snodebcastfn)(&ptr, sizeof(ptr),
                    &gasneti_pshm_segaddrs[gasneti_pshm_nodes],
                    gasneti_pshm_firstnode);

    /* Non-first nodes attach */
    if (gasneti_pshm_mynode != 0) {
      ptr = gasneti_mmap_shared_internal(gasneti_pshm_nodes, NULL, size, 1);
      save_errno = errno;
    }
  }
  #else
    #error
  #endif

  #if defined(GASNETI_PSHM_SYSV) && PLATFORM_OS_CYGWIN
  gasneti_reghandler(SIGSYS, prev_handler);
  #endif

  /* restore the pertinent errno, if any */
  errno = save_errno;

  gasneti_vnet_addr = ptr;
  gasneti_vnet_size = size;

  return (ptr == MAP_FAILED) ? NULL : ptr;
}
extern void gasneti_unlink_vnet(void) {
  gasneti_pshm_unlink(gasneti_pshm_nodes);
}
#endif /* GASNET_PSHM */

/* ------------------------------------------------------------------------------------ */
#if HAVE_MMAP
extern void gasneti_munmap(void *segbase, uintptr_t segsize) {
  gasneti_tick_t t1, t2;
  gasneti_assert(segsize > 0);
  t1 = gasneti_ticks_now();
  #if defined(GASNETI_USE_HUGETLBFS)
    gasneti_huge_munmap(segbase, segsize);
  #else
    if (munmap(segbase, segsize) != 0) 
      gasneti_fatalerror("munmap("GASNETI_LADDRFMT",%"PRIuPTR") failed: %s\n",
	      GASNETI_LADDRSTR(segbase), segsize, strerror(errno));
  #endif
  t2 = gasneti_ticks_now();

  GASNETI_TRACE_PRINTF(D,("munmap("GASNETI_LADDRFMT", %"PRIuPTR"): %.3fus\n", 
     GASNETI_LADDRSTR(segbase), segsize,
     gasneti_ticks_to_ns(t2-t1)/1000.0) );
}
#endif
/* ------------------------------------------------------------------------------------ */

#if GASNET_PSHM
  #define gasneti_do_mmap       gasneti_mmap_shared
  #define gasneti_do_mmap_fixed gasneti_mmap_shared_fixed
  #define gasneti_do_munmap     gasneti_pshm_munmap
#else
  #define gasneti_do_mmap       gasneti_mmap
  #define gasneti_do_mmap_fixed gasneti_mmap_fixed
  #define gasneti_do_munmap     gasneti_munmap
#endif

#if GASNETI_BUG3480_WORKAROUND
// Bounded retry on FIXED mappings
static void *gasneti_mmap_fixed_with_retry(void *segbase, uintptr_t segsize) {
  const uint64_t max_delay = 2e7;
  uint64_t delay = 1e3;
  const int max_retries = 100;
  int retries = 0;

  void *ptr = gasneti_do_mmap_fixed(segbase, segsize);

  while ((ptr == MAP_FAILED) && (errno == ENOMEM) && (retries++ < max_retries)) {
    GASNETI_TRACE_PRINTF(I, ("Bug 3480: retry #%d delay %gs\n", retries, 1e-9*delay));
    (void) gasneti_nsleep(delay);
    delay = MIN(max_delay, delay * 2);
    ptr = gasneti_do_mmap_fixed(segbase, segsize);
  }

  if (ptr == MAP_FAILED) {
  #if GASNET_PSHM
    gasneti_cleanup_shm();
  #endif
    gasneti_fatalerror("mmap fixed failed at "GASNETI_LADDRFMT" for size %"PRIuPTR": %s",
            GASNETI_LADDRSTR(segbase), segsize, strerror(errno));
  }

  return ptr;
}
#undef gasneti_do_mmap_fixed
#define gasneti_do_mmap_fixed gasneti_mmap_fixed_with_retry
#endif // GASNETI_BUG3480_WORKAROUND

/* binary search for segment - returns location, not mmaped */
static gasnet_seginfo_t gasneti_mmap_binary_segsrch(uintptr_t lowsz, uintptr_t highsz) {
  gasnet_seginfo_t si;

  if (highsz - lowsz <= GASNETI_MMAP_GRANULARITY) {
    si.size = 0;
    si.addr = NULL;
    return si;
  }

  si.size = gasneti_mmap_aligndown((lowsz + (highsz - lowsz) / 2));
  gasneti_assert(si.size > 0);

  si.addr = gasneti_do_mmap(si.size);

  if (si.addr == MAP_FAILED) 
    return gasneti_mmap_binary_segsrch(lowsz, si.size);
  else {
    gasnet_seginfo_t si_temp;
    gasneti_do_munmap(si.addr, si.size);

    si_temp = gasneti_mmap_binary_segsrch(si.size, highsz);
    if (si_temp.size) return si_temp;
    else return si;
  }
}
/* descending linear search for segment - returns location mmaped */
static gasnet_seginfo_t gasneti_mmap_lineardesc_segsrch(uintptr_t highsz) {
  gasnet_seginfo_t si;
  si.addr = MAP_FAILED;
  si.size = highsz;
  while (si.addr == MAP_FAILED && si.size > GASNET_PAGESIZE) {
    si.size -= GASNET_PAGESIZE;
    si.addr = gasneti_do_mmap(si.size);
  }
  if (si.addr == MAP_FAILED) {
    si.addr = NULL;
    si.size = 0;
  }
  return si;
}
/* ascending linear search for segment - returns location, not mmaped */
static gasnet_seginfo_t gasneti_mmap_linearasc_segsrch(uintptr_t highsz) {
  gasnet_seginfo_t si;
  gasnet_seginfo_t last_si = { NULL, 0 };
  si.size = GASNET_PAGESIZE;
  si.addr = gasneti_do_mmap(si.size);

  while (si.addr != MAP_FAILED && si.size <= highsz) {
    last_si = si;
    gasneti_do_munmap(last_si.addr, last_si.size);
    si.size += GASNET_PAGESIZE;
    si.addr = gasneti_do_mmap(si.size);
  }
  if (si.addr == MAP_FAILED) return last_si;
  else {
    gasneti_do_munmap(si.addr, si.size);
    return si;
  }
}

/* Differs from gasneti_mmap_segment_search() in that:
 * + maxsz must already be page aligned and non-zero
 * + zero-length result is not an error
 */
static gasnet_seginfo_t _gasneti_mmap_segment_search_inner(uintptr_t maxsz) {
  gasnet_seginfo_t si;
  int mmaped = 0;

  gasneti_assert(maxsz == GASNETI_PAGE_ALIGNDOWN(maxsz));

  si.addr = gasneti_do_mmap(maxsz);
  if (si.addr != MAP_FAILED) { /* succeeded at max value - done */
    si.size = maxsz;
    mmaped = 1;
  } else { /* use a search to find largest possible */
    #if 0
      /* linear descending search best on systems with 
         fast mmap-failed and very slow unmap and/or mmap-succeed */
      si = gasneti_mmap_lineardesc_segsrch(maxsz);
      mmaped = 1;
    #elif 0
      /* linear ascending search best on systems with 
         fast mmap-succeed and fast unmap but very slow mmap-failed */
      si = gasneti_mmap_linearasc_segsrch(maxsz);
      mmaped = 0;
    #else
      /* binary search best for systems with 
         well-balanced mmap performance */
      si = gasneti_mmap_binary_segsrch(0, maxsz);
      mmaped = 0;
    #endif
  }

  if (si.addr == NULL) {
    si.size = 0;
    return si;
  }

  gasneti_assert(si.addr != NULL && si.addr != MAP_FAILED && si.size > 0);
  gasneti_assert(si.size % GASNET_PAGESIZE == 0);
  if (mmaped && ((uintptr_t)si.addr) % GASNET_PAGESIZE == 0) {
    /* aligned and mmaped - nothing to do */
  } else { /* need to page-align base */
    if (mmaped) gasneti_do_munmap(si.addr, si.size); 
    /*  ensure page-alignment of base and size */
    { uintptr_t begin = (uintptr_t)si.addr;
      uintptr_t end = (uintptr_t)si.addr + si.size;
      begin = GASNETI_PAGE_ALIGNUP(begin);
      end = GASNETI_PAGE_ALIGNDOWN(end);
      si.addr = (void *)begin;
      si.size = end - begin;
    }
    gasneti_do_mmap_fixed(si.addr, si.size);
  }

  gasneti_assert(si.addr != NULL && si.addr != MAP_FAILED && si.size > 0);
  gasneti_assert(((uintptr_t)si.addr) % GASNET_PAGESIZE == 0 && si.size % GASNET_PAGESIZE == 0);
  return si;
}

/* gasneti_mmap_segment_search allocates the largest possible page-aligned mmap 
 * with sz <= maxsz and returns the base address and size
 */
extern gasnet_seginfo_t gasneti_mmap_segment_search(uintptr_t maxsz) {
  gasnet_seginfo_t si;

  maxsz = gasneti_mmap_aligndown(maxsz);
  if (maxsz == 0) {
    si.size = 0;
    si.addr = NULL;
    return si;
  }

  si = _gasneti_mmap_segment_search_inner(maxsz);

  if (si.addr == NULL) 
    gasneti_fatalerror("Unable to find an adequate mmap segment.");

  return si;
}

/* ------------------------------------------------------------------------------------ */
#endif /* GASNETI_MMAP_OR_PSHM */

GASNETI_IDENT(gasneti_IdentString_DefaultMaxSegsizeStr, 
              "$GASNetDefaultMaxSegsizeStr: " GASNETI_MAX_SEGSIZE_CONFIGURE " $");

/* return user-selected limit for the max segment size, as gleaned from several sources */
const char *gasnet_max_segsize_str; // intentional tentative definition, to allow client override
uint64_t gasnet_max_segsize;        // DEPRECATED: intentional tentative definition, to allow client override 
uintptr_t gasneti_max_segsize() {
  static uintptr_t result = 0;
  uint64_t tmp;
  if (!result) {
    uintptr_t auxsegsz = gasneti_auxseg_preinit();
    uint64_t pph = gasneti_myhost.node_count;
    gasneti_assert(pph > 0);
    /* start with the configure-selected default */
    const char *dflt = GASNETI_MAX_SEGSIZE_CONFIGURE;
    /* next, check the compile-time overrides */
    if (gasnet_max_segsize) { // lower-priority deprecated override, interpreted as /p
      static char tmp[80];
      snprintf(tmp,sizeof(tmp),"%"PRIu64"/p",gasnet_max_segsize);
      dflt = tmp;
    }
    if (gasnet_max_segsize_str) { // higher-priority string override
      dflt = gasnet_max_segsize_str;
    }

    #if PLATFORM_ARCH_32 && !defined(GASNETI_ALLOW_HUGE_32BIT_SEGMENT)
      /* need to be careful about overflow on 32-bit:
         can't use a full 4 GB due to sign bit problems 
         on the int argument to mmap() for some 32-bit systems
         so use 2GB - pagesz 
      */
      uint64_t hardmax = (((uint64_t)1)<<31) - GASNET_PAGESIZE;
    #else
      uint64_t hardmax = (uint64_t)-1; // unlimited
    #endif

    // finally, check the environment override, parse the result and factor in min/max/auxseg
    uint64_t val = gasneti_getenv_memsize_withdefault("GASNET_MAX_SEGSIZE", dflt,
                                                GASNET_PAGESIZE+auxsegsz, hardmax,
                                                gasneti_getPhysMemSz(1), pph,
                                                auxsegsz);

    // round UP to nearest huge page, if needed, to ensure we don't truncate client's MAX_SEGSIZE request
    val = GASNETI_ALIGNUP(val, gasneti_mmap_pagesize());

    gasneti_assert(val == GASNETI_PAGE_ALIGNDOWN(val));
    gasneti_assert(val >= GASNET_PAGESIZE);
    gasneti_assert(val <= hardmax);
    result = (uintptr_t)val;
    gasneti_assert(result == val); // overflow check
  }
  return result;
}

#if !GASNET_SEGMENT_EVERYTHING
/* mmap-based segment init/attach */
static gasnet_seginfo_t gasneti_segment = {0,0}; /* local segment info */
#ifdef GASNETI_MMAP_OR_PSHM
static uintptr_t gasneti_myheapend = 0; /* top of my malloc heap */
static uintptr_t gasneti_maxheapend = 0; /* top of max malloc heap */
static uintptr_t gasneti_maxbase = 0; /* start of segment overlap region */
#endif /* GASNETI_MMAP_OR_PSHM */

typedef struct {
  gasnet_seginfo_t seginfo;
  union {
    uintptr_t heapend; /* during init only */
    uintptr_t segsize_request; /* during attach only */
  } u;
} gasneti_segexch_t;
static gasneti_segexch_t *gasneti_segexch = NULL; /* exchanged segment information */

#ifdef GASNETI_MMAP_OR_PSHM
/* perform a coordinated mmap probe to determine the max memory
    that can be mmap()ed while considering multiple GASNet nodes
    per shared memory node
   localLimit is an optional conduit-specific upper limit per GASNet node
   sharedLimit is an optional upper limit per shared memory node
   requires an exchangefn callback function that can be used to exchange data
   and a barrierfn callback to perform a barrier
   returns a value suitable for use as localSegmentLimit in a call
    to gasneti_segmentInit()
   
   for exchangefn and barrierfn: the implementations are only required to
    perform their functions with respect the peers on a shared-memory
    node (though exchangefn does require a "full" third argument).
    however, global implementations are acceptible
 */
uintptr_t gasneti_mmapLimit(uintptr_t localLimit, uint64_t sharedLimit,
                            gasneti_bootstrapExchangefn_t exchangefn,
                            gasneti_bootstrapBarrierfn_t barrierfn) {
  int i;
  uintptr_t maxsz;
  const gasnet_node_t local_count = gasneti_myhost.node_count;

#if GASNET_PSHM
  gasneti_pshm_cs_enter(&gasneti_cleanup_shm);
#endif

  gasneti_assert(exchangefn);
  gasneti_assert(barrierfn); /* No longer optional */
  gasneti_assert(gasneti_nodemap);

  /* Apply system-dependent defaults, if any */
#if defined(GASNETI_HAVE_BGQ_INLINES)
  if ((localLimit == (uintptr_t)-1) || (sharedLimit == (uint64_t)-1)) {
    const uint64_t nodemem = gasneti_getPhysMemSz(1); /* sysconf() reports phys mem for full node */
    const uint64_t safemem = (nodemem * 4) / 5; /* 80% as a safety margin (but just a guess) */
    if (sharedLimit == (uint64_t)-1) {
      sharedLimit = safemem;
    }
    if (localLimit == (uintptr_t)-1) {
      /* Use node's configured ppn value, even if running fewer actual procs */
      const uint64_t sprg7 = mfspr(SPRN_SPRG7RO);
      const uint8_t ppn = (sprg7 >> 8) & 0xff; /* Byte 6 is processes per node: 1,2,4,8,16,32 or 64 */
      localLimit = safemem / ppn;
    }
  }
#else
  if (sharedLimit == (uint64_t)-1) {
    /* Start at something reasonable if we expect to avoid swapping */
    const uint64_t nodemem = gasneti_getPhysMemSz(0);
    if (nodemem) sharedLimit = nodemem; /* no change if getPhysMemSz failed */
  }
#endif

  /* Apply intial limits, even if not sharing nodes */
  maxsz = GASNETI_MMAP_LIMIT;
  if ((uint64_t)localLimit > sharedLimit) localLimit = sharedLimit;
  maxsz = MIN(maxsz, localLimit);

  /* Coordinate the search IFF there are any shared nodes. */
  if (gasneti_myhost.grp_count != gasneti_nodes) {
    uintptr_t *sz_exchg = gasneti_malloc(gasneti_nodes * sizeof(uintptr_t));
    gasnet_seginfo_t se = {0,0};

    /* Ensure our probe will not collectively exceed the shareLimit, if any. */
    if ((sharedLimit != (uint64_t)-1) && (local_count > 1)) {
#if SIZEOF_VOID_P != 8
       /* Skip MIN() on overflow */
       if ((sharedLimit / local_count) < (uint64_t)(uintptr_t)(-1))
#endif
       { uintptr_t tmp = sharedLimit / local_count;
         maxsz = MIN(maxsz, tmp);
       }
    }

    /* Allow each node in a given host to probe SEQUENTIALLY, and then collect the results */
    maxsz = gasneti_mmap_aligndown(maxsz);
#if GASNET_PSHM
    if (maxsz && (gasneti_myhost.grp_count == gasneti_mysupernode.grp_count)) { /* host==supernode */
      for (i = 0; i < gasneti_nodemap_local_count; ++i) {
        if (i == gasneti_nodemap_local_rank) {
          se = _gasneti_mmap_segment_search_inner(maxsz);
          maxsz = se.size;
        }
        /* Bcast because we can use "declining expectations" to potentially speed later probes */
        gasneti_pshmnet_bootstrapBroadcast(gasneti_request_pshmnet, &maxsz, sizeof(uintptr_t), &maxsz, i);
        sz_exchg[gasneti_nodemap_local[i]] = maxsz;
      }
    } else
#endif
    if (maxsz) {
      /* Find widest host */
      gasnet_node_t rounds = 0;
      {
        const gasnet_node_t num_hosts = gasneti_myhost.grp_count;
        gasnet_node_t *tmp = gasneti_calloc(num_hosts, sizeof(gasnet_node_t));
        for (i = 0; i < gasneti_nodes; ++i) {
          const gasnet_node_t host = gasneti_nodeinfo[i].host;
          gasneti_assert(host < num_hosts);
          tmp[host] += 1;
          rounds = MAX(rounds, tmp[host]);
        }
        gasneti_free(tmp);
      }

      for (i = 0; i < rounds; ++i) {
        if (i == gasneti_myhost.node_rank) {
          se = _gasneti_mmap_segment_search_inner(maxsz);
        }
        (*barrierfn)();
      }
    }
    (*exchangefn)(&se.size, sizeof(uintptr_t), sz_exchg);

    /* Compute the host-local mean */
    { uint64_t sum = 0;
      for (i = 0; i < local_count; ++i) {
        sum += sz_exchg[gasneti_myhost.nodes[i]];
      }
      maxsz = gasneti_mmap_aligndown(sum / local_count);

#if GASNET_PSHM
    #if defined(GASNETI_PSHM_GHEAP)
      /* We don't consue any additional virtual address space for each segment.
       * So, the coordnation below is not required.
       */
    #else
      /* The probe completed has determined how much memory we can map.
       * However, we now need to be sure that multiple segments will fit.
       * The limit could be, for instance, due to
       * + RLIMIT_AS or RLIMIT_VMEM
       * + size of the holes in the address space (especially on 32-bit arch)
       * NOTE: must use pshm's view of supernode, which may be less than nodemap's.
       */
      if (se.size) gasneti_do_munmap(se.addr, se.size);
      gasneti_unlink_segments(); /* Includes barrier to complete munmap()s */
      se.size = 0;

      if (gasneti_pshm_mynode == 0) {
        gasnet_seginfo_t *tmp_se = gasneti_calloc(gasneti_pshm_nodes,sizeof(gasnet_seginfo_t));
	int done;
	/* Iterate until we find a size for which N segments fit.
	 * Ideally the first pass finds that the size probed above works.
	 * If rlimit or total address space limit the mapping, then the first
	 *   iteration determines the right size and the second verifies it.
	 * If we are limited by the size of the holes in the address space
	 *   then it may take several iterations to work out what fits.
	 * TODO: If we reach a 3rd pass perhaps we could try something else?
	 */
	do {
          sum = 0; done = 1;
          for (i = 0; i < gasneti_pshm_nodes; ++i) {
            tmp_se[i] = _gasneti_mmap_segment_search_inner(maxsz);
            gasneti_pshm_unlink(gasneti_pshm_mynode);
            sum += tmp_se[i].size;
	    if (tmp_se[i].size != maxsz) {
	      done = 0;
	      if (tmp_se[i].size < GASNETI_MMAP_GRANULARITY) break;
            }
          }
          for (i = 0; i < gasneti_pshm_nodes; ++i) {
            if (tmp_se[i].size) gasneti_do_munmap(tmp_se[i].addr, tmp_se[i].size);
            tmp_se[i].size = 0;
          }
          maxsz = gasneti_mmap_aligndown(sum / gasneti_pshm_nodes);
        } while (!done);
        gasneti_free(tmp_se);
      }

      /* supernode-local communication of the maxsz results */
      gasneti_pshmnet_bootstrapBroadcast(gasneti_request_pshmnet, &maxsz, sizeof(uintptr_t), &maxsz, 0);

      /* Unlink the shared segments to prevent leaks (they are recreated in segmentInit) */
      gasneti_unlink_segments();
    #endif
#endif
    }

    /* Free held resources */
    gasneti_free(sz_exchg);
    if (se.size) gasneti_do_munmap(se.addr, se.size);
    (*barrierfn)(); /* Ensures munmap()s complete on-node before return */
  }

#if GASNET_PSHM
  gasneti_pshm_cs_leave();
#endif
  return maxsz;
}
#endif /* GASNETI_MMAP_OR_PSHM */

/* do the work necessary for initing a standard segment map in arbitrary memory 
     uses mmap if available, or malloc otherwise
   requires an exchange callback function that can be used to exchange data
   sets max local & global segment size
   localSegmentLimit provides an optional conduit-specific limit on max segment sz
    (for example, to limit size based on physical memory availability)
    pass (uintptr_t)-1 for unlimited
    Use of gasneti_mmapLimit() can help determine the right value to pass here
   keeps internal state for attach
 */
void gasneti_segmentInit(uintptr_t localSegmentLimit,
                         gasneti_bootstrapExchangefn_t exchangefn) {
#if GASNET_PSHM
  gasneti_pshm_cs_enter(&gasneti_cleanup_shm);
#endif

  gasneti_assert(gasneti_MaxLocalSegmentSize == 0);
  gasneti_assert(gasneti_MaxGlobalSegmentSize == 0);
  gasneti_assert(exchangefn);
  gasneti_assert(gasneti_nodes > 0);
  gasneti_assert(gasneti_mynode < gasneti_nodes);

  gasneti_segexch = (gasneti_segexch_t *)gasneti_malloc(gasneti_nodes*sizeof(gasneti_segexch_t));

  if (localSegmentLimit != (uintptr_t)-1) 
    localSegmentLimit = gasneti_mmap_aligndown(localSegmentLimit);

  #ifdef GASNETI_MMAP_OR_PSHM
  { gasneti_segexch_t se;
    int i;

    // NOTE: If the conduit did not derive localSegmentLimit from a call to
    // gasneti_mmapLimit(), then this call might lead to unexpected failures
    // (such as bug 651) due to it's lack of coordination among processes.
    gasneti_segment = gasneti_mmap_segment_search(localSegmentLimit == (uintptr_t)-1 ?
                                                  GASNETI_MMAP_LIMIT : 
                                                  MIN(localSegmentLimit,GASNETI_MMAP_LIMIT));
    GASNETI_TRACE_PRINTF(C, ("My segment: addr="GASNETI_LADDRFMT"  sz=%"PRIuPTR,
      GASNETI_LADDRSTR(gasneti_segment.addr), gasneti_segment.size));

    se.seginfo = gasneti_segment;
  #if PLATFORM_OS_DARWIN
    /* sbrk() is "emulated", making the heap-separation test invalid. */
    /* TODO: is there an alternative separation test we could/should apply? */
    gasneti_myheapend = GASNETI_PAGE_ALIGNDOWN((uintptr_t)-1);
  #else
    gasneti_myheapend = (uintptr_t)sbrk(0);
    if (gasneti_myheapend == (uintptr_t)-1) gasneti_fatalerror("Failed to sbrk(0):%s",strerror(errno));
    gasneti_myheapend = GASNETI_PAGE_ALIGNUP(gasneti_myheapend);
  #endif
    se.u.heapend = gasneti_myheapend;

    /* gather the sbrk info and mmap segment location */
    (*exchangefn)(&se, sizeof(gasneti_segexch_t), gasneti_segexch);

    /* compute bounding-box of segment location */
    { uintptr_t maxbase = 0;
      uintptr_t maxsize = 0;
      uintptr_t minsize = (uintptr_t)-1;
      uintptr_t minend = (uintptr_t)-1;
      uintptr_t maxheapend = 0;
      char segstats[255];
      /* compute various stats across nodes */
      for (i=0;i < gasneti_nodes; i++) {
        if (gasneti_segexch[i].u.heapend > maxheapend)
          maxheapend = gasneti_segexch[i].u.heapend;
        if (((uintptr_t)gasneti_segexch[i].seginfo.addr) > maxbase)
          maxbase = (uintptr_t)gasneti_segexch[i].seginfo.addr;
        if (gasneti_segexch[i].seginfo.size > maxsize)
          maxsize = gasneti_segexch[i].seginfo.size;
        if (gasneti_segexch[i].seginfo.size < minsize)
          minsize = gasneti_segexch[i].seginfo.size;
        if ((uintptr_t)gasneti_segexch[i].seginfo.addr + gasneti_segexch[i].seginfo.size < minend)
          minend = (uintptr_t)gasneti_segexch[i].seginfo.addr + gasneti_segexch[i].seginfo.size;
      }
      snprintf(segstats, sizeof(segstats),
          "Segment stats: "
          "maxsize = %"PRIuPTR"   "
          "minsize = %"PRIuPTR"   "
          "maxbase = "GASNETI_LADDRFMT"   "
          "minend = "GASNETI_LADDRFMT"   "
          "maxheapend = "GASNETI_LADDRFMT"   ",
          maxsize, minsize,
          GASNETI_LADDRSTR(maxbase), GASNETI_LADDRSTR(minend), GASNETI_LADDRSTR(maxheapend));
      segstats[sizeof(segstats)-1] = '\0';
      GASNETI_TRACE_MSG(C, segstats);

      gasneti_maxheapend = maxheapend;
      gasneti_maxbase = maxbase;
      #if GASNET_ALIGNED_SEGMENTS
       /* BG/[PQ] would incorrectly probe the I/O node */
       #if !defined(PLATFORM_OS_BGQ)
        if (gasneti_nodes > 1) { 
          /* bug 2067 - detect if the compute nodes are using Linux's 'intentional VM space randomization'
           * security feature, which is known to break GASNET_ALIGNED_SEGMENTS, esp at large scale
           */
           FILE *fp = fopen("/proc/sys/kernel/randomize_va_space", "r");
           if (fp) {
             int VMrand = fgetc(fp);
             if (VMrand != EOF && VMrand != '0') {
               const char *wmsg = "WARNING: It appears your compute nodes are using a Linux security feature "
                                  "which intentionally randomizes the virtual address space, "
                                  "but GASNet was configured to optimize for congruent address spaces. "
                                  "You probably need to re-configure with --disable-aligned-segments to avoid "
                                  "errors at job startup (especially for runs with large node count or shared segment size).";
               GASNETI_TRACE_MSG(I, wmsg);
               if (!gasneti_getenv_yesno_withdefault("GASNET_QUIET",0)) {
                 fprintf(stderr, "%s\n", wmsg);
                 fflush(stderr);
               }
             }
             fclose(fp);
           }
        }   
       #endif
        if (maxbase >= minend) { /* no overlap - maybe should be a fatal error... */
          const char *wmsg = "WARNING: unable to locate overlapping mmap segments in gasneti_segmentInit()"
            ": perhaps you need to re-configure with --disable-aligned-segments";
          GASNETI_TRACE_MSG(I, wmsg);
          if (!gasneti_mynode && !gasneti_getenv_yesno_withdefault("GASNET_QUIET",0)) {
            fprintf(stderr, "%s\n%s\n", wmsg, segstats);
            for (i=0;i < gasneti_nodes; i++) {
              fprintf(stderr, " %i: seg=["GASNETI_LADDRFMT","GASNETI_LADDRFMT"]"
                              " size=%"PRIuPTR" heapend="GASNETI_LADDRFMT"\n", i,
                      GASNETI_LADDRSTR(gasneti_segexch[i].seginfo.addr), 
                      GASNETI_LADDRSTR(((uintptr_t)gasneti_segexch[i].seginfo.addr)+gasneti_segexch[i].seginfo.size), 
                      gasneti_segexch[i].seginfo.size,
                      GASNETI_LADDRSTR(gasneti_segexch[i].u.heapend));
              fflush(stderr);
            }
          }
          gasneti_MaxLocalSegmentSize = 0;
          gasneti_MaxGlobalSegmentSize = 0;
        } else {
          gasneti_MaxLocalSegmentSize = ((uintptr_t)gasneti_segment.addr + gasneti_segment.size) - maxbase;
          gasneti_MaxGlobalSegmentSize = minend - maxbase;
        }
      #else
        gasneti_MaxLocalSegmentSize = gasneti_segment.size;
        gasneti_MaxGlobalSegmentSize = minsize;
      #endif
    }
  }
  #else /* !GASNETI_MMAP_OR_PSHM */
    #if GASNET_ALIGNED_SEGMENTS && !GASNET_CONDUIT_SMP
      #error bad config: dont know how to provide GASNET_ALIGNED_SEGMENTS when !HAVE_MMAP
    #endif
    /* some systems don't support mmap - 
       TODO: safe mechanism to determine a true max seg sz, 
       for now just trust gasneti_max_segsize */
    gasneti_MaxLocalSegmentSize = GASNETI_PAGE_ALIGNDOWN(MIN(localSegmentLimit, gasneti_max_segsize()));
    gasneti_MaxGlobalSegmentSize = gasneti_MaxLocalSegmentSize;
  #endif
  GASNETI_TRACE_PRINTF(C, ("MaxLocalSegmentSize = %"PRIuPTR"   MaxGlobalSegmentSize = %"PRIuPTR,
                           gasneti_MaxLocalSegmentSize, gasneti_MaxGlobalSegmentSize));
  gasneti_assert(gasneti_MaxLocalSegmentSize % GASNET_PAGESIZE == 0);
  gasneti_assert(gasneti_MaxGlobalSegmentSize % GASNET_PAGESIZE == 0);
  gasneti_assert(gasneti_MaxGlobalSegmentSize <= gasneti_MaxLocalSegmentSize);
  gasneti_assert(gasneti_MaxLocalSegmentSize <= localSegmentLimit);

#if GASNET_PSHM
  gasneti_unlink_segments();
  gasneti_pshm_cs_leave();
#endif
}

/* ------------------------------------------------------------------------------------ */

void gasneti_segmentAttach(uintptr_t segsize, uintptr_t minheapoffset,
                           gasnet_seginfo_t *seginfo,
                           gasneti_bootstrapExchangefn_t exchangefn) {
  void *segbase = NULL;
  gasneti_assert(seginfo);
  gasneti_assert(exchangefn);
  gasneti_assert(gasneti_segexch);
  gasneti_memcheck(gasneti_segexch);

  #if GASNET_PSHM
    /* Avoid leaking shared memory files in case of non-collective exit between init/attach */
    gasneti_pshm_cs_enter(&gasneti_cleanup_shm);
    gasneti_pshmnet_bootstrapBarrier();
  #endif

  #ifdef GASNETI_MMAP_OR_PSHM
  { /* TODO: this assumes heap grows up */
    uintptr_t topofheap;
    #if GASNET_ALIGNED_SEGMENTS
      #if GASNETI_USE_HIGHSEGMENT
        { /* the segsizes requested may differ across nodes, so in order to 
             place the segment as high as possible while maintaining alignment, 
             we need another all-to-all to calculate the new aligned base address
           */
          gasneti_segexch_t se;
          uintptr_t minsegstart = (uintptr_t)-1;
          int i;

          /* gather the segsize info again */
          se.seginfo = gasneti_segment;
          se.u.segsize_request = segsize;
          (*exchangefn)(&se, sizeof(gasneti_segexch_t), gasneti_segexch);

          for (i=0;i<gasneti_nodes;i++) {
            uintptr_t segstart = 
                ((uintptr_t)gasneti_segexch[i].seginfo.addr + gasneti_segexch[i].seginfo.size) - 
                 gasneti_segexch[i].u.segsize_request;
            /*gasneti_assert(gasneti_segexch[i].u.segsize_request >= 0); True by typing */
            gasneti_assert(segstart >= gasneti_maxbase);
            if (segstart < minsegstart) minsegstart = segstart;
          }

          segbase = (void *)minsegstart;
        }
      #else /* !GASNETI_USE HIGH_SEGMENTS */
        segbase = (void *)gasneti_maxbase;
      #endif /* GASNETI_USE HIGH_SEGMENTS */
      topofheap = gasneti_maxheapend;
    #else /* !GASNETI_ALIGNED_SEGMENTS */
      topofheap = gasneti_myheapend;
      #if GASNETI_USE_HIGHSEGMENT
        segbase = (void *)((uintptr_t)gasneti_segment.addr + 
                           gasneti_segment.size - segsize);
      #else
        segbase = gasneti_segment.addr;
      #endif
    #endif /* GASNETI_ALIGNED_SEGMENTS */

    if (segsize == 0) { /* no segment */
      gasneti_do_munmap(gasneti_segment.addr, gasneti_segment.size);
      segbase = NULL; 
    }
    else {
      /* check if segment is above the heap (in its path) and too close */
      if ((((uintptr_t)segbase + segsize) > topofheap) &&
        (topofheap + minheapoffset > (uintptr_t)segbase)) {
        uintptr_t maxsegsz;
        void *endofseg = (void *)((uintptr_t)gasneti_segment.addr + gasneti_segment.size);
        /* we're too close to the heap - readjust to prevent collision 
           note this allows us to return different segsizes on diff nodes
           (even when we are using GASNET_ALIGNED_SEGMENTS)
         */
        segbase = (void *)(topofheap + minheapoffset);
        if (segbase >= endofseg) 
          gasneti_fatalerror("minheapoffset too large to accomodate a segment");
        maxsegsz = (uintptr_t)endofseg - (uintptr_t)segbase;
        if (segsize > maxsegsz) {
          GASNETI_TRACE_PRINTF(I, ("WARNING: gasneti_segmentAttach() reducing requested segsize (%"PRIuPTR"=>%"PRIuPTR") to accomodate minheapoffset",
            segsize, maxsegsz));
          segsize = maxsegsz;
        }
      }

    #if GASNET_PSHM
      /* Must always recreate the segment*/
      const int trim = 1;
    #else
      /* trim final segment if required */
      const int trim = (gasneti_segment.addr != segbase || gasneti_segment.size != segsize);
    #endif

      if (trim) {
        gasneti_assert(segbase >= gasneti_segment.addr &&
               (uintptr_t)segbase + segsize <= (uintptr_t)gasneti_segment.addr + gasneti_segment.size);
        gasneti_do_munmap(gasneti_segment.addr, gasneti_segment.size);
      }

      #if GASNETI_BUG3480_WORKAROUND
        // Barrier between unmap and re-map, via 1-byte exchange (a.k.a. GatherAll).
        // This is a bit of a hack, but is the most expedient way to get a barrier
        // with compute-node scope, since gasneti_pshmnet_bootstrapBarrier() may
        // have a narrower scope when env var GASNET_SUPERNODE_MAXSIZE is set.
        char a; char *b = gasneti_malloc(gasneti_nodes);
        (*exchangefn)(&a, sizeof(char), b);
        gasneti_free(b);
      #endif

      if (trim) {
#if GASNETI_PSHM_MAP_FIXED_IGNORED
        segbase =
#endif
        gasneti_do_mmap_fixed(segbase, segsize);
      }
    }
    gasneti_free(gasneti_segexch);
    gasneti_segexch = NULL;
  }
  #else /* !GASNETI_MMAP_OR_PSHM */
    /* for the T3E, and other platforms which don't support mmap */
    segbase = gasneti_malloc_allowfail(segsize + GASNET_PAGESIZE);
    while (!segbase) {
      segsize = GASNETI_PAGE_ALIGNDOWN(segsize/2);
      if (segsize == 0) break; 
      segbase = gasneti_malloc_allowfail(segsize + GASNET_PAGESIZE);
    }
    if (segbase) {
      segbase = (void *)GASNETI_PAGE_ALIGNUP(segbase);
    }
  #endif /* GASNETI_MMAP_OR_PSHM */
  gasneti_assert(((uintptr_t)segbase) % GASNET_PAGESIZE == 0);
  gasneti_assert(segsize % GASNET_PAGESIZE == 0);
  GASNETI_TRACE_PRINTF(C, ("Final segment: segbase="GASNETI_LADDRFMT"  segsize=%"PRIuPTR,
    GASNETI_LADDRSTR(segbase), segsize));

  /*  gather segment information */
  gasneti_segment.addr = segbase;
  gasneti_segment.size = segsize;
  (*exchangefn)(&gasneti_segment, sizeof(gasnet_seginfo_t), seginfo);

  #if GASNET_ALIGNED_SEGMENTS == 1
    if (segsize > 0) { int i; /*  check that segments are aligned */
      for (i=0; i < gasneti_nodes; i++) {
        if (seginfo[i].size != 0 && seginfo[i].addr != segbase) 
          gasneti_fatalerror("Failed to acquire aligned segments for GASNET_ALIGNED_SEGMENTS");
      }
    }
  #endif

  #if GASNET_PSHM
  { /* Map the remote shared segments */
    int i;

    gasneti_nodeinfo[gasneti_mynode].offset = 0;
    gasneti_export_segment(gasneti_segment.addr, gasneti_segment.size);
    for (i = 0; i < gasneti_pshm_nodes; i++){
        if (i != gasneti_pshm_mynode) {
            const gasnet_node_t node = gasneti_nodemap_local[i];
            const uintptr_t size = seginfo[node].size;
            void *segbase = gasneti_mmap_remote_shared(NULL, size, i);

            gasneti_assert(((uintptr_t)segbase) % GASNET_PAGESIZE == 0);
            gasneti_assert(size % GASNET_PAGESIZE == 0);

            if (((uintptr_t)segbase >= gasneti_myheapend) &&
                ((uintptr_t)segbase <  gasneti_myheapend + minheapoffset))
              gasneti_fatalerror("minheapoffset too large to accomodate a remote segment");

            gasneti_nodeinfo[node].offset = (uintptr_t)segbase - (uintptr_t)seginfo[node].addr;

            GASNETI_TRACE_PRINTF(C, ("Remote segment %d: segbase="GASNETI_LADDRFMT"  segsize=%"PRIuPTR,
                                     (int)node, GASNETI_LADDRSTR(segbase), size));
        }
    }

    /* Barrier #1 ensures all attaches complete before unlinking */
    gasneti_pshmnet_bootstrapBarrier();
    gasneti_cleanup_shm();
    /* Barrier #2 ensures unlinking completes before return, so crashes cannot leak segments */
    gasneti_pshmnet_bootstrapBarrier();
    gasneti_pshm_cs_leave();
  }
  #endif /* GASNET_PSHM */
}
#endif /* !GASNET_SEGMENT_EVERYTHING */

/* Used to pass the nodemap information to the client
 * Similar to gasneti_getSegmentInfo(). 
 * */
extern int gasneti_getNodeInfo(gasnet_nodeinfo_t *nodeinfo_table, int numentries) {
  GASNETI_CHECKINIT();

  if_pf (numentries <= 0) {
    if (numentries == 0) return GASNET_OK;
    else GASNETI_RETURN_ERR(BAD_ARG);
  }
  gasneti_assert(nodeinfo_table);
  if_pf (numentries > gasneti_nodes) numentries = gasneti_nodes;

  if (gasneti_nodeinfo) {
    memcpy(nodeinfo_table, gasneti_nodeinfo, numentries*sizeof(gasnet_nodeinfo_t));
  } else {
    gasnet_node_t i;

    for (i=0; i < numentries; i++) {
      nodeinfo_table[i].host = i;
      nodeinfo_table[i].supernode = i;
    #if GASNET_PSHM
      nodeinfo_table[i].offset = 0;
    #endif
    }
  }

  return GASNET_OK;
}

/* ------------------------------------------------------------------------------------ */
/* seginfo initialization and manipulation */
extern int gasneti_getSegmentInfo(gasnet_seginfo_t *seginfo_table, int numentries) {
  GASNETI_CHECKATTACH();
  gasneti_memcheck(gasneti_seginfo);
  gasneti_memcheck(gasneti_seginfo_client);
  gasneti_memcheck(gasneti_seginfo_ub);
  gasneti_memcheck(gasneti_seginfo_client_ub);
  #if GASNET_DEBUG /*  sanity checks */
    #if GASNET_SEGMENT_EVERYTHING
    { int i; /*  sanity check seg-everything condition */
      for (i=0; i < gasneti_nodes; i++) {
        gasneti_assert(gasneti_seginfo[i].addr == 0 && gasneti_seginfo[i].size == (uintptr_t)-1);
        gasneti_assert(gasneti_seginfo_client[i].addr == 0 && gasneti_seginfo_client[i].size == (uintptr_t)-1);
        gasneti_assert(gasneti_seginfo_ub[i] == (void *)(uintptr_t)-1);
        gasneti_assert(gasneti_seginfo_client_ub[i] == (void *)(uintptr_t)-1);
      }
    }
    #else
    { int i; 
      #if GASNET_ALIGNED_SEGMENTS
      void *segbase = NULL;
      #endif
      for (i=0; i < gasneti_nodes; i++) {
        if (gasneti_seginfo[i].size == 0) {
          gasneti_assert(gasneti_seginfo[i].addr == 0);
          gasneti_assert(gasneti_seginfo_client[i].addr == 0);
          gasneti_assert(gasneti_seginfo_ub[i] == 0);
          gasneti_assert(gasneti_seginfo_client_ub[i] == 0);
        } else {
          #if GASNET_ALIGNED_SEGMENTS
            /*  sanity check that segments are aligned */
            if (!segbase) segbase = gasneti_seginfo[i].addr;
            else if (gasneti_seginfo[i].addr != segbase)  
              gasneti_fatalerror("Failed sanity check for aligned segments with GASNET_ALIGNED_SEGMENTS");
          #endif
          /* sanity check _ub caches */
          gasneti_assert(gasneti_seginfo_ub[i] == (void*)(((uintptr_t)gasneti_seginfo[i].addr) + gasneti_seginfo[i].size));
          gasneti_assert(gasneti_seginfo_client_ub[i] == (void*)(((uintptr_t)gasneti_seginfo_client[i].addr) + gasneti_seginfo_client[i].size));
          if (gasneti_seginfo_client[i].size > 0) {
            gasneti_assert(gasneti_seginfo[i].addr <= gasneti_seginfo_client[i].addr);
            gasneti_assert(gasneti_seginfo_ub[i] >= gasneti_seginfo_client_ub[i]);
          }
          /* misc segment sanity checks */
          gasneti_assert(((uintptr_t)gasneti_seginfo[i].addr) % GASNET_PAGESIZE == 0);
          gasneti_assert(((uintptr_t)gasneti_seginfo_client[i].addr) % GASNET_PAGESIZE == 0);
          gasneti_assert(((uintptr_t)gasneti_seginfo_ub[i]) % GASNET_PAGESIZE == 0);
          gasneti_assert(((uintptr_t)gasneti_seginfo_client_ub[i]) % GASNET_PAGESIZE == 0);
        }
      }
    }
    #endif
  #endif
  if_pf (numentries <= 0) {
    if (numentries == 0) return GASNET_OK;
    else GASNETI_RETURN_ERR(BAD_ARG);
  }
  gasneti_assert(seginfo_table);
  if_pf (numentries > gasneti_nodes) numentries = gasneti_nodes;
  memcpy(seginfo_table, gasneti_seginfo_client, numentries*sizeof(gasnet_seginfo_t));
  return GASNET_OK;
}
/* ------------------------------------------------------------------------------------ */
// Unmap vnet and cross-mapped segments.
#if GASNET_PSHM
extern void gasneti_pshm_fini(void) {
  #if PLATFORM_OS_WSL || PLATFORM_OS_LINUX
    if (!gasneti_platform_isWSL()) return;

    // WSL cleanup code
    if (gasneti_attach_done) {
      int lrank = 0;
      for (gasnet_node_t n = 0; n < gasneti_nodes; ++n) {
        if (!gasneti_pshm_in_supernode(n)) continue;
        void *segbase = (void*)((uintptr_t)gasneti_seginfo[n].addr + gasneti_nodeinfo[n].offset);
        // NOTE: assumes OK to "remote" unmap local segment - may not be appropriate for XPMEM
        gasneti_munmap_remote(lrank, segbase, gasneti_seginfo[n].size);
        lrank += 1;
      }
    } else {
      gasneti_munmap(gasneti_segment.addr, gasneti_segment.size);
    }
    if (gasneti_vnet_addr) {
      gasneti_munmap(gasneti_vnet_addr, gasneti_vnet_size);
    }
  #else
    // Not currently supported (or thought to be necessary) on platforms other than WSL
  #endif
}
#endif // PSHM
/* ------------------------------------------------------------------------------------ */
/* Aux-seg support */

#include <gasnet_core_internal.h> /* for _hidx_gasnetc_auxseg_reqh */

/* if the client requestsz is power-of-two, 
   reduce the client request to maintain a power-of-two full segsize */
#ifndef GASNETI_AUXSEG_PRESERVE_POW2_FULLSEGSZ
#define GASNETI_AUXSEG_PRESERVE_POW2_FULLSEGSZ 0
#endif

/* force the client segment to appear at the base of the fullseg */
#ifndef GASNETI_FORCE_CLIENTSEG_TO_BASE
#define GASNETI_FORCE_CLIENTSEG_TO_BASE 0
#endif

/* lists of internal functions that want auxseg (with trailing commas) */
/* conduit-specific auxseg fns */
#ifndef GASNETC_AUXSEG_FNS
#define GASNETC_AUXSEG_FNS() 
#endif
/* extended-ref auxseg fns */
#ifndef GASNETE_AUXSEG_FNS
#define GASNETE_AUXSEG_FNS() 
#endif
/* extended-ref collectives auxseg fns */
#ifndef GASNETE_COLL_AUXSEG_FNS
#define GASNETE_COLL_AUXSEG_FNS() 
#endif

gasneti_auxseg_request_t gasneti_auxseg_dummy(gasnet_seginfo_t *auxseg_info);
#ifdef GASNETC_AUXSEG_DECLS
  GASNETC_AUXSEG_DECLS
#endif
#ifdef GASNETE_AUXSEG_DECLS
  GASNETE_AUXSEG_DECLS
#endif
#ifdef GASNETE_COLL_AUXSEG_DECLS
  GASNETE_COLL_AUXSEG_DECLS
#endif

gasneti_auxsegregfn_t gasneti_auxsegfns[] = {
  GASNETC_AUXSEG_FNS()
  GASNETE_AUXSEG_FNS()
  GASNETE_COLL_AUXSEG_FNS()
  #if GASNET_DEBUG
    gasneti_auxseg_dummy, 
  #endif
  NULL
};

/* page-aligned size of auxseg */
static gasneti_auxseg_request_t gasneti_auxseg_total_alignedsz = { 0, 0 };
static gasneti_auxseg_request_t *gasneti_auxseg_alignedsz = NULL;
static uintptr_t gasneti_auxseg_sz = 0;
static uintptr_t gasneti_auxseg_client_request_sz = 0;

#if GASNET_DEBUG
  /* spawner hint of our auxseg requirements */
  #define GASNETI_AUXSEG_DUMMY_SZ    463
  GASNETI_IDENT(gasneti_dummy_auxseg_IdentString, "$GASNetAuxSeg_dummy: "_STRINGIFY(GASNETI_AUXSEG_DUMMY_SZ)" $");

  gasneti_auxseg_request_t gasneti_auxseg_dummy(gasnet_seginfo_t *auxseg_info) {
    gasneti_auxseg_request_t retval;
    static gasnet_seginfo_t *auxseg_save = NULL;
    int i, selftest=0;
    retval.minsz = 213;
    retval.optimalsz = GASNETI_AUXSEG_DUMMY_SZ;
    if (auxseg_info == NULL) return retval; /* initial query */
    if (auxseg_info == (void*)(uintptr_t)-1) { /* self test */
      selftest = 1;
      gasneti_assert(auxseg_save);
    } else { /* auxseg granted */
      gasneti_assert(!auxseg_save);
      auxseg_save = gasneti_malloc(gasneti_nodes*sizeof(gasnet_seginfo_t));
      memcpy(auxseg_save, auxseg_info, gasneti_nodes*sizeof(gasnet_seginfo_t));
      gasneti_leak(auxseg_save); /* Needed by self test, if any */
    }
    for (i=0; i < gasneti_nodes; i++) {
      gasneti_assert(auxseg_save[i].addr);
      gasneti_assert(((uintptr_t)auxseg_save[i].addr) % GASNETI_CACHE_LINE_BYTES == 0);
      gasneti_assert(((uintptr_t)auxseg_save[i].addr) % 8 == 0);
      gasneti_assert(auxseg_save[i].size >= retval.minsz);
      gasneti_assert(auxseg_save[i].size <= retval.optimalsz);
    }
    for (i=0; i < auxseg_save[gasneti_mynode].size; i++) {
      uint8_t *p = (uint8_t *)auxseg_save[gasneti_mynode].addr;
      #define AUXSEG_TESTVAL(i) ((uint8_t)(8|((i+0x3F)^(i>>8))))
      if (selftest) gasneti_assert(p[i] == AUXSEG_TESTVAL(i));
      else p[i] = AUXSEG_TESTVAL(i);
      #undef AUXSEG_TESTVAL
    }
    return retval;
  }
#endif

// collect and return optimal auxseg size sum, padded to page size
// may be called multiple times, subsequent calls return cached value
uintptr_t gasneti_auxseg_preinit(void) {
  if (gasneti_auxseg_sz) return gasneti_auxseg_sz; // only the first call computes requirements

  const int numfns = (sizeof(gasneti_auxsegfns)/sizeof(gasneti_auxsegregfn_t))-1;
  gasneti_assert(gasneti_auxsegfns[numfns] == NULL);
  if (numfns > 0)
    gasneti_auxseg_alignedsz = gasneti_calloc(numfns,sizeof(gasneti_auxseg_request_t));

  /* collect requests */
  for (int i = 0; i < numfns; i++) {
    gasneti_auxseg_alignedsz[i] = (gasneti_auxsegfns[i])(NULL);
    gasneti_auxseg_total_alignedsz.minsz += 
      GASNETI_ALIGNUP(gasneti_auxseg_alignedsz[i].minsz,GASNETI_CACHE_LINE_BYTES);
    gasneti_auxseg_total_alignedsz.optimalsz += 
      GASNETI_ALIGNUP(gasneti_auxseg_alignedsz[i].optimalsz,GASNETI_CACHE_LINE_BYTES);
  }
  gasneti_auxseg_total_alignedsz.minsz = 
    GASNETI_PAGE_ALIGNUP(gasneti_auxseg_total_alignedsz.minsz);
  gasneti_auxseg_total_alignedsz.optimalsz = 
    GASNETI_PAGE_ALIGNUP(gasneti_auxseg_total_alignedsz.optimalsz);

  gasneti_auxseg_sz = gasneti_auxseg_total_alignedsz.optimalsz;
  GASNETI_TRACE_PRINTF(C, ("gasneti_auxseg_preinit(): gasneti_auxseg_sz = %"PRIuPTR, gasneti_auxseg_sz));
  gasneti_assert(gasneti_auxseg_sz % GASNET_PAGESIZE == 0);
  return gasneti_auxseg_sz;
}
// subtract auxseg requirements from the values to report to client
void gasneti_auxseg_init(void) {
  gasneti_auxseg_preinit();
  #if !GASNET_SEGMENT_EVERYTHING
    /* TODO: implement request downsizing down to minsz */
    if (gasneti_auxseg_sz >= gasneti_MaxGlobalSegmentSize) {
      const char *moreinfo = "";
      if (gasneti_max_segsize() <= gasneti_auxseg_sz) {
        moreinfo = "\nYou may need to adjust the GASNET_MAX_SEGSIZE envvar - see the GASNet README for details.";
      }
      gasneti_fatalerror("GASNet internal auxseg size (%"PRIuPTR" bytes) exceeds available segment size (%"PRIuPTR" bytes).%s",
        gasneti_auxseg_sz, gasneti_MaxGlobalSegmentSize, moreinfo);
    }

    #if GASNETI_AUXSEG_PRESERVE_POW2_FULLSEGSZ
      if (!GASNETI_POWEROFTWO(gasneti_MaxLocalSegmentSize) && 
          !GASNETI_POWEROFTWO(gasneti_MaxGlobalSegmentSize)) 
    #endif
      { /* could relax single-value restriction on auxseg registration size by doing another exchange here */
        gasneti_MaxLocalSegmentSize -= gasneti_auxseg_sz;
        gasneti_MaxGlobalSegmentSize -= gasneti_auxseg_sz;
      }
    GASNETI_TRACE_PRINTF(C, ("gasneti_auxseg_init(): gasneti_auxseg_sz = %"PRIuPTR": "
                   "MaxLocalSegmentSize = %"PRIuPTR"   MaxGlobalSegmentSize = %"PRIuPTR,
                   gasneti_auxseg_sz, gasneti_MaxLocalSegmentSize, gasneti_MaxGlobalSegmentSize));
  #endif
}

#if GASNET_SEGMENT_EVERYTHING
  static gasnet_seginfo_t _gasneti_my_auxseg;
  static volatile gasnet_seginfo_t *_gasneti_auxseg_everything = NULL;
  static gasneti_weakatomic32_t _gasneti_auxseg_rcvd[32]; // Implicitly zero-initialized

  extern void gasnetc_auxseg_reqh(gasnet_token_t token, void *buf, size_t nbytes, gasnet_handlerarg_t arg0) {
    gasneti_assert(_gasneti_auxseg_everything != NULL);
    const int step = arg0 & 0x1f; // Max 2^5 steps            => 2^32 nodes
    const int seq = arg0 >> 5;    // Max 2^27 fragments * 512 => 64GB (and max sent is len*nodes/2)
    const int distance = (1 << step);
    gasneti_assert(distance < gasneti_nodes);
    uint8_t *dest = (uint8_t*)(_gasneti_auxseg_everything + distance) + (seq * gasnet_AMMaxMedium());
    gasneti_assert(dest + nbytes <= (uint8_t*)(_gasneti_auxseg_everything + gasneti_nodes));
    memcpy(dest, buf, nbytes);
    gasneti_weakatomic32_increment(&_gasneti_auxseg_rcvd[step], GASNETI_ATOMIC_REL);
  }

  static void gasneti_auxseg_exchange(void) {
    const size_t len = sizeof(gasnet_seginfo_t);
    uint8_t *dest = (uint8_t *)_gasneti_auxseg_everything;

    /* copy in local contribution */
    memcpy(dest, &_gasneti_my_auxseg, len);

    /* Bruck's concatenation algorithm: */
    unsigned int step, distance;
    for (step = 0, distance = 1; distance < gasneti_nodes; ++step, distance *= 2) {
      gasnet_node_t peer = (distance <= gasneti_mynode) ? gasneti_mynode - distance
                                                        : gasneti_mynode + (gasneti_nodes - distance);
      size_t nbytes = len * MIN(distance, gasneti_nodes - distance);
      size_t offset = 0;
      uint32_t seq = 0;

      /* Send payload using AMMedium(s) */
      do {
        const size_t to_xfer = MIN(nbytes, gasnet_AMMaxMedium());
        GASNETI_SAFE(gasnet_AMRequestMedium1(peer, _hidx_gasnetc_auxseg_reqh,
                                             dest + offset, to_xfer, step | (seq << 5)));
        ++seq;
        offset += to_xfer;
        nbytes -= to_xfer;
      } while (nbytes);

      /* Poll until we have received the same number of messages as we sent */
      GASNET_BLOCKUNTIL((int)gasneti_weakatomic32_read(&_gasneti_auxseg_rcvd[step], 0) >= (int)seq);
      gasneti_assert((int)gasneti_weakatomic32_read(&_gasneti_auxseg_rcvd[step], 0) == (int)seq);
    }

    /* Copy to final destination while performing the rotation */
    uint8_t *temp = gasneti_malloc(len * gasneti_nodes);
    const size_t a = len * (gasneti_nodes - gasneti_mynode);
    const size_t b = len * gasneti_mynode;
    memcpy(temp, dest + a, b);
    memcpy(temp + b, dest, a);
    gasneti_free(dest);
    _gasneti_auxseg_everything = (volatile gasnet_seginfo_t *)temp;

    gasneti_assert(! memcmp((void*)(_gasneti_auxseg_everything + gasneti_mynode), &_gasneti_my_auxseg, len));
  }
#endif

/* consume the client's segsize request and return the 
   value to acquire including auxseg requirements */
uintptr_t gasneti_auxseg_preattach(uintptr_t client_request_sz) {
  uintptr_t result;
  gasneti_assert(gasneti_auxseg_sz % GASNET_PAGESIZE == 0);
  #if GASNET_SEGMENT_EVERYTHING
  { /* malloc page/cache aligned space for gasneti_auxseg_sz */
    void *auxseg = gasneti_malloc(gasneti_auxseg_sz+MAX(GASNETI_CACHE_LINE_BYTES, GASNET_PAGESIZE));
    _gasneti_auxseg_everything = gasneti_malloc(gasneti_nodes*sizeof(gasnet_seginfo_t));
    _gasneti_my_auxseg.addr = (void *)GASNETI_ALIGNUP(auxseg,MAX(GASNETI_CACHE_LINE_BYTES, GASNET_PAGESIZE));
    _gasneti_my_auxseg.size = gasneti_auxseg_sz;
    result = 0;
  }
  #else
    gasneti_assert(client_request_sz % GASNET_PAGESIZE == 0);
    gasneti_auxseg_client_request_sz = client_request_sz;
    #if GASNETI_AUXSEG_PRESERVE_POW2_FULLSEGSZ
      if (GASNETI_POWEROFTWO(client_request_sz)) { 
        result = client_request_sz;
        while (result < (client_request_sz+gasneti_auxseg_sz) && result*2 <= gasneti_MaxGlobalSegmentSize)
          result *= 2;
      } else
    #endif
        result = client_request_sz + gasneti_auxseg_sz;
  #endif
  GASNETI_TRACE_PRINTF(C,("gasneti_auxseg_preattach(%"PRIuPTR") => %"PRIuPTR,
                          client_request_sz, result));
  return result;
}

/* provide auxseg to GASNet components and init secondary segment arrays 
   requires gasneti_seginfo has been initialized to the correct values
   exchangefn is used only for GASNET_SEGMENT_EVERYTHING and may be NULL
 */
void gasneti_auxseg_attach(gasneti_bootstrapExchangefn_t exchangefn) {
  gasnet_seginfo_t *si;
  int numfns = (sizeof(gasneti_auxsegfns)/sizeof(gasneti_auxsegregfn_t))-1;
  int i,j;

  gasneti_assert(gasneti_auxsegfns[numfns] == NULL);
  gasneti_seginfo_client = gasneti_calloc(gasneti_nodes, sizeof(gasnet_seginfo_t));
  gasneti_leak(gasneti_seginfo_client);

  /* point si at the auxseg */
  #if GASNET_SEGMENT_EVERYTHING
    /* exchange locations into _gasneti_auxseg_everything */
    gasneti_assert(_gasneti_auxseg_everything);
    if (exchangefn) {
      (*exchangefn)(&_gasneti_my_auxseg, sizeof(gasnet_seginfo_t), (void*)_gasneti_auxseg_everything);
    } else {
      gasneti_auxseg_exchange();
    }
    si = (gasnet_seginfo_t *)_gasneti_auxseg_everything;
  #else
    si = gasneti_malloc(gasneti_nodes*sizeof(gasnet_seginfo_t));
    /* break up fullseg into client seg and auxseg */
    for (j=0; j < gasneti_nodes; j++) {
      #if GASNETI_FORCE_CLIENTSEG_TO_BASE
        gasneti_seginfo_client[j].addr = gasneti_seginfo[j].addr;
        gasneti_seginfo_client[j].size = gasneti_seginfo[j].size - gasneti_auxseg_sz;
        si[j].addr = (void *)(((uintptr_t)gasneti_seginfo_client[j].addr) + gasneti_seginfo_client[j].size);
        si[j].size = gasneti_auxseg_sz;
      #else /* place auxseg at bottom of fullseg by default, to reduce chance of client overflow damage */
        gasneti_seginfo_client[j].addr = (void *)(((uintptr_t)gasneti_seginfo[j].addr) + gasneti_auxseg_sz);
        gasneti_seginfo_client[j].size = gasneti_seginfo[j].size - gasneti_auxseg_sz;
        si[j].addr = gasneti_seginfo[j].addr;
        si[j].size = gasneti_auxseg_sz;
      #endif
      #if GASNETI_AUXSEG_PRESERVE_POW2_FULLSEGSZ 
        /* bug 361: cannot safely do this in general without a bootstrap exchange, because segsize requests may differ across conduits */
        /* trim client segment, which may be inflated due to GASNETI_AUXSEG_PRESERVE_POW2_FULLSEGSZ */
        if (gasneti_seginfo_client[j].size > gasneti_auxseg_client_request_sz)
          gasneti_seginfo_client[j].size = gasneti_auxseg_client_request_sz;
      #endif
    }
  #endif

  gasneti_seginfo_ub = gasneti_malloc(gasneti_nodes*sizeof(void *));
  gasneti_leak(gasneti_seginfo_ub);
  gasneti_seginfo_client_ub = gasneti_malloc(gasneti_nodes*sizeof(void *));
  gasneti_leak(gasneti_seginfo_client_ub);

  for (i=0; i < gasneti_nodes; i++) {
    #if GASNET_SEGMENT_EVERYTHING
      gasneti_assert(gasneti_seginfo[i].addr == 0 && gasneti_seginfo[i].size == (uintptr_t)-1);
      gasneti_seginfo_client[i].addr = 0;
      gasneti_seginfo_client[i].size = (uintptr_t)-1;
      gasneti_seginfo_ub[i] = (void *)(uintptr_t)-1;
      gasneti_seginfo_client_ub[i] = (void *)(uintptr_t)-1;
    #else
      if (gasneti_seginfo_client[i].size == 0) {
        gasneti_seginfo_client[i].addr = 0;
        gasneti_seginfo_client_ub[i] = 0;
      } else {
        gasneti_seginfo_client_ub[i] = (void*)(((uintptr_t)gasneti_seginfo_client[i].addr) + gasneti_seginfo_client[i].size);
      }
      if (gasneti_seginfo[i].size == 0) {
        gasneti_seginfo_ub[i] = 0;
      } else {
        gasneti_seginfo_ub[i] = (void*)(((uintptr_t)gasneti_seginfo[i].addr) + gasneti_seginfo[i].size);
      }
    #endif
  }
  GASNETI_TRACE_PRINTF(C,("gasneti_auxseg_attach() clientsegment => ("GASNETI_LADDRFMT".."GASNETI_LADDRFMT") (%"PRIuPTR" bytes)",
                  GASNETI_LADDRSTR(gasneti_seginfo_client[gasneti_mynode].addr), 
                  GASNETI_LADDRSTR(gasneti_seginfo_client_ub[gasneti_mynode]),
                  gasneti_seginfo_client[gasneti_mynode].size));

  if (numfns > 0) {
    for (j=0; j < gasneti_nodes; j++) {
      gasneti_assert((uintptr_t)si[j].addr % GASNET_PAGESIZE == 0);
      gasneti_assert((uintptr_t)si[j].addr % GASNETI_CACHE_LINE_BYTES == 0);
      gasneti_assert((uintptr_t)si[j].size == gasneti_auxseg_sz);
      si[j].size = gasneti_auxseg_alignedsz[0].optimalsz;
    }

    for (i=0; i < numfns; i++) {
      GASNETI_TRACE_PRINTF(C,("gasneti_auxseg_attach() fn[%i] => ("GASNETI_LADDRFMT".."GASNETI_LADDRFMT") (%"PRIuPTR" bytes)",
                      i, GASNETI_LADDRSTR(si[gasneti_mynode].addr), 
                      GASNETI_LADDRSTR(((uintptr_t)si[gasneti_mynode].addr)+si[gasneti_mynode].size),
                      si[gasneti_mynode].size));
      (gasneti_auxsegfns[i])(si);
      if (i+1 < numfns) {
        for (j=0; j < gasneti_nodes; j++) {
          si[j].addr = (void *)(((uintptr_t)si[j].addr) + gasneti_auxseg_alignedsz[i].optimalsz);
          si[j].addr = (void *)GASNETI_ALIGNUP(si[j].addr,GASNETI_CACHE_LINE_BYTES);
          si[j].size = gasneti_auxseg_alignedsz[i+1].optimalsz;
        }
      }
    }
  }
  gasneti_free(si);
  gasneti_leak(gasneti_auxseg_alignedsz);
}
/* ------------------------------------------------------------------------------------ */
#if defined(GASNETI_PSHM_GHEAP)
  /* Access to unwrapped malloc/free */
  #undef malloc
  #undef free
  static void *gasneti_pshm_valloc(size_t size) {
    void *result;
    /* TODO: configure probe for posix_memalign() and alternatives when missing? */
    if (0 != posix_memalign(&result, GASNETI_PSHMNET_PAGESIZE, size)) {
      result = MAP_FAILED;
    }
    return result;
  }
  static void gasneti_pshm_vfree(void *addr) {
    /* TODO: alternatives as above? */
    free(addr);
  }
#endif
/* ------------------------------------------------------------------------------------ */
