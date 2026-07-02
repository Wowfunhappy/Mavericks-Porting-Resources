/*
 * Functional smoke test for the de-gated MacPorts polyfills, run by `make test`.
 *
 * Each polyfill is exercised for real on 10.9 and checked against an
 * independent source of truth where possible.
 */

/* Opt in to the pthread_[f]chdir_np() prototypes (see pthread.h wrapper). */
#define _MACPORTS_LEGACY_PTHREAD_CHDIR 1

#include <assert.h>          /* static_assert */
#include <dirent.h>          /* fdopendir */
#include <errno.h>
#include <fcntl.h>           /* openat, AT_FDCWD, O_* */
#include <libgen.h>          /* basename */
#include <pthread.h>        /* pthread_chdir_np */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mach/mach_time.h>  /* mach_approximate_time, mach_continuous_time */
#include <notify.h>          /* notify_is_valid_token */
#include <os/lock.h>         /* os_unfair_lock */
#include <sys/attr.h>        /* getattrlistat attrlist, VOL_CAP_INT_CLONE */
#include <sys/clonefile.h>   /* clonefile */
#include <sys/mman.h>        /* MAP_ANONYMOUS */
#include <sys/mount.h>       /* statfs (for fsgetpath) */
#include <sys/random.h>      /* getentropy */
#include <sys/stat.h>        /* fstatat, fchmodat, mkdirat, utimensat, futimens */
#include <sys/fsgetpath.h>   /* fsgetpath */
#include <time.h>            /* clock_gettime, timespec_get */

static int failures = 0;

#define CHECK(cond) do {                                              \
    if (cond) { printf("  ok   : %s\n", #cond); }                     \
    else { printf("  FAIL : %s  (errno=%d %s)\n", #cond, errno,       \
                  strerror(errno)); ++failures; }                     \
  } while (0)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
static_assert(sizeof(long) >= 4, "static_assert must be available (assert.h)");
#endif

static char tdir[PATH_MAX];

int
main(void)
{
  /* ---- header-only: MAP_ANONYMOUS, VOL_CAP_INT_CLONE, machine defs ---- */
  CHECK(MAP_ANONYMOUS == MAP_ANON);
  CHECK(VOL_CAP_INT_CLONE == 0x00010000);
  CHECK(CPU_TYPE_ARM == 12);   /* mach/machine.h addition */
  {
    void *p = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                   MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    CHECK(p != MAP_FAILED);
    if (p != MAP_FAILED) munmap(p, 4096);
  }

  /* ---- clocks ---- */
  {
    struct timespec a = {0,0}, b = {0,0}, res = {-1,-1}, u = {0,0};
    CHECK(clock_gettime(CLOCK_REALTIME, &a) == 0 && a.tv_sec > 1500000000);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &a) == 0);
    CHECK(clock_gettime(CLOCK_MONOTONIC, &b) == 0);
    CHECK(b.tv_sec > a.tv_sec || (b.tv_sec==a.tv_sec && b.tv_nsec>=a.tv_nsec));
    CHECK(clock_getres(CLOCK_MONOTONIC_RAW, &res) == 0 && res.tv_nsec >= 1);
    CHECK(clock_gettime_nsec_np(CLOCK_UPTIME_RAW) != 0);
    CHECK(timespec_get(&u, TIME_UTC) == TIME_UTC && u.tv_sec > 1500000000);
  }

  /* ---- mach time ---- */
  {
    uint64_t abs1 = mach_absolute_time();
    uint64_t approx = mach_approximate_time();
    uint64_t cont = mach_continuous_time();
    uint64_t capprox = mach_continuous_approximate_time();
    CHECK(approx != 0 && approx >= abs1 - 1000000000ULL);
    CHECK(cont >= abs1);              /* continuous includes sleep time */
    CHECK(capprox != 0);
  }

  /* ---- getentropy ---- */
  {
    unsigned char b1[64], b2[64];
    memset(b1, 0, sizeof(b1)); memset(b2, 0, sizeof(b2));
    CHECK(getentropy(b1, sizeof(b1)) == 0);
    CHECK(getentropy(b2, sizeof(b2)) == 0);
    CHECK(memcmp(b1, b2, sizeof(b1)) != 0);   /* two draws should differ */
    CHECK(getentropy(b1, 256) == 0);          /* 256 is the max allowed */
    errno = 0;
    CHECK(getentropy(b1, 257) == -1 && errno == EIO);  /* > 256 must fail */
  }

  /* ---- os_unfair_lock ---- */
  {
    os_unfair_lock lock = OS_UNFAIR_LOCK_INIT;
    os_unfair_lock_lock(&lock);
    CHECK(os_unfair_lock_trylock(&lock) == false);
    os_unfair_lock_unlock(&lock);
    CHECK(os_unfair_lock_trylock(&lock) == true);
    os_unfair_lock_unlock(&lock);
  }

  /* ---- sysconf(_SC_PHYS_PAGES) ---- */
  CHECK(sysconf(_SC_PHYS_PAGES) > 0);

  /* ---- clonefile: no APFS on 10.9, must fail with ENOTSUP ---- */
  CHECK(clonefile("/etc/hosts", "/tmp/mls_clone_xyz", 0) == -1 && errno == ENOTSUP);

  /* ---- build a scratch directory for the path-based tests ---- */
  snprintf(tdir, sizeof(tdir), "/tmp/mls_test_%ld", (long)getpid());
  CHECK(mkdir(tdir, 0755) == 0);

  /* ---- the *at family (fchdir-emulated) ---- */
  {
    int dfd = open(tdir, O_RDONLY);
    CHECK(dfd >= 0);

    /* openat to create a file relative to dfd */
    int fd = openat(dfd, "file.txt", O_CREAT | O_WRONLY, 0644);
    CHECK(fd >= 0);
    if (fd >= 0) { CHECK(write(fd, "hello", 5) == 5); close(fd); }

    /* fstatat */
    struct stat st;
    CHECK(fstatat(dfd, "file.txt", &st, 0) == 0 && st.st_size == 5);

    /* faccessat */
    CHECK(faccessat(dfd, "file.txt", R_OK | W_OK, 0) == 0);
    CHECK(faccessat(dfd, "nope.txt", F_OK, 0) == -1);

    /* fchmodat then verify */
    CHECK(fchmodat(dfd, "file.txt", 0600, 0) == 0);
    CHECK(fstatat(dfd, "file.txt", &st, 0) == 0 && (st.st_mode & 0777) == 0600);

    /* fchownat no-op (uid/gid -1) should succeed without privilege */
    CHECK(fchownat(dfd, "file.txt", (uid_t)-1, (gid_t)-1, 0) == 0);

    /* symlinkat + readlinkat */
    CHECK(symlinkat("file.txt", dfd, "link.txt") == 0);
    char lbuf[64]; ssize_t ln = readlinkat(dfd, "link.txt", lbuf, sizeof(lbuf)-1);
    CHECK(ln == 8 && (lbuf[ln]=0, strcmp(lbuf, "file.txt") == 0));

    /* linkat (hard link) */
    CHECK(linkat(dfd, "file.txt", dfd, "hard.txt", 0) == 0);
    CHECK(fstatat(dfd, "hard.txt", &st, 0) == 0 && st.st_nlink == 2);

    /* renameat */
    CHECK(renameat(dfd, "hard.txt", dfd, "renamed.txt") == 0);
    CHECK(fstatat(dfd, "renamed.txt", &st, 0) == 0);

    /* mkdirat + unlinkat(AT_REMOVEDIR) */
    CHECK(mkdirat(dfd, "subdir", 0755) == 0);
    CHECK(unlinkat(dfd, "subdir", AT_REMOVEDIR) == 0);

    /* getattrlistat: fetch the entry name */
    {
      struct attrlist al;
      memset(&al, 0, sizeof(al));
      al.bitmapcount = ATTR_BIT_MAP_COUNT;
      al.commonattr = ATTR_CMN_NAME;
      struct { uint32_t len; attrreference_t ref; char buf[256]; }
        __attribute__((aligned(4), packed)) ab;
      CHECK(getattrlistat(dfd, "file.txt", &al, &ab, sizeof(ab), 0) == 0);
      CHECK(strcmp((char *)&ab.ref + ab.ref.attr_dataoffset, "file.txt") == 0);
    }

    /* fdopendir over dfd: enumerate and find file.txt */
    {
      int dfd2 = open(tdir, O_RDONLY);
      DIR *d = fdopendir(dfd2);
      CHECK(d != NULL);
      int found = 0; struct dirent *de;
      while (d && (de = readdir(d))) if (!strcmp(de->d_name, "file.txt")) found = 1;
      CHECK(found);
      if (d) closedir(d);            /* also closes dfd2 */
    }

    /* utimensat / futimens: set a known mtime and read it back */
    {
      struct timespec times[2];
      times[0].tv_sec = 1000000000; times[0].tv_nsec = 0;  /* atime */
      times[1].tv_sec = 1111111111; times[1].tv_nsec = 0;  /* mtime */
      CHECK(utimensat(dfd, "file.txt", times, 0) == 0);
      CHECK(fstatat(dfd, "file.txt", &st, 0) == 0 && st.st_mtime == 1111111111);

      int ffd = openat(dfd, "file.txt", O_RDWR);
      CHECK(ffd >= 0);
      struct timespec ftimes[2];
      ftimes[0].tv_sec = 1222222222; ftimes[0].tv_nsec = 0;
      ftimes[1].tv_sec = 1333333333; ftimes[1].tv_nsec = 0;
      CHECK(futimens(ffd, ftimes) == 0);
      if (ffd >= 0) close(ffd);
      CHECK(fstatat(dfd, "file.txt", &st, 0) == 0 && st.st_mtime == 1333333333);
    }

    /* cleanup the at-test files */
    unlinkat(dfd, "link.txt", 0);
    unlinkat(dfd, "renamed.txt", 0);
    if (dfd >= 0) close(dfd);
  }

  /* ---- pthread_chdir_np / pthread_fchdir_np ---- */
  {
    char before[PATH_MAX], now[PATH_MAX], want[PATH_MAX];
    getcwd(before, sizeof(before));
    realpath(tdir, want);
    CHECK(pthread_chdir_np(tdir) == 0);
    CHECK(getcwd(now, sizeof(now)) && strcmp(now, want) == 0);
    CHECK(pthread_fchdir_np(-1) == 0);       /* back to process cwd */
    CHECK(getcwd(now, sizeof(now)) && strcmp(now, before) == 0);
  }

  /* ---- fsgetpath: resolve a known file by fsid + inode ---- */
  {
    char fpath[PATH_MAX];
    snprintf(fpath, sizeof(fpath), "%s/file.txt", tdir);
    struct statfs sfs; struct stat st;
    if (statfs(fpath, &sfs) == 0 && stat(fpath, &st) == 0) {
      fsid_t fsid;
      memcpy(&fsid, &sfs.f_fsid, sizeof(fsid));
      char out[PATH_MAX];
      ssize_t n = fsgetpath(out, sizeof(out), &fsid, st.st_ino);
      char real[PATH_MAX]; realpath(fpath, real);
      CHECK(n > 0 && strcmp(out, real) == 0);
    } else {
      CHECK(0 && "statfs/stat setup for fsgetpath");
    }
  }

  /* ---- fmemopen ---- */
  {
    char mem[32] = "abcdef";
    FILE *f = fmemopen(mem, sizeof(mem), "r");
    CHECK(f != NULL);
    if (f) {
      char rb[8] = {0};
      CHECK(fread(rb, 1, 6, f) == 6 && strcmp(rb, "abcdef") == 0);
      fclose(f);
    }
  }

  /* ---- open_memstream ---- */
  {
    char *bufp = NULL; size_t sz = 0;
    FILE *f = open_memstream(&bufp, &sz);
    CHECK(f != NULL);
    if (f) {
      fputs("xyz123", f);
      fflush(f);
      CHECK(sz == 6 && bufp && strcmp(bufp, "xyz123") == 0);
      fclose(f);
      free(bufp);
    }
  }

  /* ---- notify_is_valid_token ---- */
  {
    int tok = -1;
    CHECK(notify_is_valid_token(0) == false);
    CHECK(notify_is_valid_token(-1) == false);
    CHECK(notify_register_check("org.mavericks-legacy-support.test.notify", &tok)
          == NOTIFY_STATUS_OK);
    CHECK(notify_is_valid_token(tok) == true);
    CHECK(notify_cancel(tok) == NOTIFY_STATUS_OK);
    CHECK(notify_is_valid_token(tok) == false);
  }

  /* ---- final cleanup ---- */
  {
    char p[PATH_MAX];
    snprintf(p, sizeof(p), "%s/file.txt", tdir); unlink(p);
    rmdir(tdir);
  }

  if (failures == 0) { printf("PASS\n"); return 0; }
  printf("FAILED (%d checks)\n", failures);
  return 1;
}
