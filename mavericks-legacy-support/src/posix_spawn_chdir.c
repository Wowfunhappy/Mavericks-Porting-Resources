/*
 * posix_spawn_chdir.c -- MavericksLegacySupport libSystem shim.
 * Split verbatim from the former modern_api_polyfills.c; the shared DBG
 * helper and common includes live in mav_shim_debug.h.
 */
#include "mav_shim_debug.h"

/* ── posix_spawn chdir actions (10.15+): record the requested path but there's
 * no kernel support, so the child won't actually chdir. Return 0 so callers
 * that only check the action-builder's return code don't bail out. */
/* Track chdir requests keyed on the posix_spawn_file_actions_t pointer. When
 * posix_spawn is called, our wrapper below looks up the path and does a real
 * fork/chdir/execve instead. Tiny static table (8 entries) since we don't
 * expect many concurrent spawns. */
#define CHDIR_TABLE_SIZE 16
static struct {
    const void *acts;
    char path[1024];
    int fd;              /* -1 if chdir by path, else fchdir */
} g_chdir_table[CHDIR_TABLE_SIZE];
static pthread_mutex_t g_chdir_mu = PTHREAD_MUTEX_INITIALIZER;

static void chdir_set(const void *acts, const char *path, int fd) {
    pthread_mutex_lock(&g_chdir_mu);
    for (int i = 0; i < CHDIR_TABLE_SIZE; i++) {
        if (g_chdir_table[i].acts == NULL || g_chdir_table[i].acts == acts) {
            g_chdir_table[i].acts = acts;
            g_chdir_table[i].fd = fd;
            if (path) { strncpy(g_chdir_table[i].path, path, sizeof(g_chdir_table[i].path)-1); g_chdir_table[i].path[sizeof(g_chdir_table[i].path)-1] = 0; }
            else      { g_chdir_table[i].path[0] = 0; }
            break;
        }
    }
    pthread_mutex_unlock(&g_chdir_mu);
}
static int chdir_get(const void *acts, char *path_out, size_t plen, int *fd_out) {
    pthread_mutex_lock(&g_chdir_mu);
    int found = 0;
    for (int i = 0; i < CHDIR_TABLE_SIZE; i++) {
        if (g_chdir_table[i].acts == acts) {
            if (path_out) { strncpy(path_out, g_chdir_table[i].path, plen); path_out[plen-1] = 0; }
            if (fd_out) *fd_out = g_chdir_table[i].fd;
            g_chdir_table[i].acts = NULL;    /* consume */
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_chdir_mu);
    return found;
}

int posix_spawn_file_actions_addchdir_np(void *acts, const char *path) {
    DBG("posix_spawn_addchdir path=%s acts=%p", path, acts);
    chdir_set(acts, path, -1);
    return 0;
}
int posix_spawn_file_actions_addfchdir_np(void *acts, int fd) {
    DBG("posix_spawn_addfchdir fd=%d acts=%p", fd, acts);
    chdir_set(acts, NULL, fd);
    return 0;
}

/* Override posix_spawn/spawnp to honor pending chdir. If a chdir is registered
 * for the given actions, fall back to fork/chdir/execve so the child's CWD is
 * actually changed. Otherwise delegate to the real libSystem impl via
 * RTLD_NEXT. */
#include <dlfcn.h>
typedef int (*spawn_fn)(pid_t *, const char *, const void *, const void *,
                        char *const[], char *const[]);

/* Serialize posix_spawn when a chdir is requested: briefly change the
 * parent's CWD, call the real posix_spawn (so all file-actions and attrs get
 * handled by libSystem — stdin/stdout pipes, signal masks, process groups,
 * etc.), then restore. Children fork() inside posix_spawn, so they inherit
 * our transient CWD without other threads racing on it (we hold the lock).
 *
 * Trade-off: concurrent spawns with different chdirs are serialized, and any
 * thread that calls getcwd() during the window sees the transient path.
 * Acceptable in practice — this cost only applies to spawns that actually
 * request a chdir, and Bun doesn't do a high-frequency spawn workload. */
static pthread_mutex_t g_spawn_cwd_mu = PTHREAD_MUTEX_INITIALIZER;

static int spawn_common(spawn_fn real, pid_t *pid, const char *path_or_file,
                        const void *acts, const void *attrs,
                        char *const argv[], char *const envp[], int searchpath) {
    (void)searchpath;
    char cd[1024] = {0};
    int cd_fd = -1;
    int have_cd = acts ? chdir_get(acts, cd, sizeof(cd), &cd_fd) : 0;

    if (!have_cd || (cd[0] == 0 && cd_fd < 0))
        return real(pid, path_or_file, acts, attrs, argv, envp);

    if (dbg_on()) {
        fprintf(stderr, "[patch] spawn_wrapper real+chdir path=%s cwd=%s fd=%d argv=",
                path_or_file, cd, cd_fd);
        for (int i = 0; argv && argv[i]; i++) fprintf(stderr, " %s", argv[i]);
        fprintf(stderr, "\n");
    }
    pthread_mutex_lock(&g_spawn_cwd_mu);
    int saved_fd = open(".", O_RDONLY);
    if (saved_fd < 0) {
        pthread_mutex_unlock(&g_spawn_cwd_mu);
        return errno;
    }
    int chdir_rc = (cd_fd >= 0) ? fchdir(cd_fd) : chdir(cd);
    if (chdir_rc < 0) {
        int e = errno;
        close(saved_fd);
        pthread_mutex_unlock(&g_spawn_cwd_mu);
        return e;
    }
    int rc = real(pid, path_or_file, acts, attrs, argv, envp);
    if (fchdir(saved_fd) < 0) {
        /* Best-effort: log but don't fail the spawn (child already forked). */
        DBG("spawn_wrapper WARN: failed to restore cwd: %s", strerror(errno));
    }
    close(saved_fd);
    pthread_mutex_unlock(&g_spawn_cwd_mu);
    return rc;
}

int posix_spawn_wrapper(pid_t *pid, const char *path, const void *acts,
                        const void *attrs, char *const argv[], char *const envp[]) __asm("_posix_spawn");
int posix_spawn_wrapper(pid_t *pid, const char *path, const void *acts,
                        const void *attrs, char *const argv[], char *const envp[]) {
    static spawn_fn real = NULL;
    if (!real) real = (spawn_fn)dlsym(RTLD_NEXT, "posix_spawn");
    return spawn_common(real, pid, path, acts, attrs, argv, envp, 0);
}
int posix_spawnp_wrapper(pid_t *pid, const char *file, const void *acts,
                         const void *attrs, char *const argv[], char *const envp[]) __asm("_posix_spawnp");
int posix_spawnp_wrapper(pid_t *pid, const char *file, const void *acts,
                         const void *attrs, char *const argv[], char *const envp[]) {
    static spawn_fn real = NULL;
    if (!real) real = (spawn_fn)dlsym(RTLD_NEXT, "posix_spawnp");
    return spawn_common(real, pid, file, acts, attrs, argv, envp, 1);
}

