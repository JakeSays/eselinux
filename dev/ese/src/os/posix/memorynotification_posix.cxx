// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
//
// POSIX equivalent of os/memorynotification.cxx.
//
// Upstream uses CreateMemoryResourceNotification + RegisterWaitForSingleObject
// so the engine's buffer-manager maintenance task can quiesce itself when
// memory is plentiful and be woken when the kernel raises a low-memory
// pressure signal.
//
// Linux equivalent: kernel PSI (Pressure Stall Information, kernel 4.20+) at
// /proc/pressure/memory.  We try two strategies, in order:
//
//   1. PSI user trigger (best).  Write "some <threshold_us> <window_us>" to
//      the file, then poll(POLLPRI) on the FD; the kernel fires POLLPRI when
//      the stall threshold is crossed.  No polling overhead from us.
//
//   2. PSI counter polling (fallback).  When the trigger write isn't
//      accepted (some kernels — notably Ubuntu's — reject user-space
//      triggers, returning EINVAL), the poller wakes every 1 s, reads
//      "some avg10" from /proc/pressure/memory, and fires the callback if
//      it's above threshold.  More wake-ups but the same outcome.
//
// Query() returns the *current* pressure state by reading PSI counters
// (or /proc/meminfo as a final fallback) — that read is cheap (single
// small /proc file), and the engine calls it sparingly.
//
// If PSI is missing entirely (kernel < 4.20, or non-Linux POSIX), we
// degrade to the original stub behaviour: Register/Query always succeed,
// fLowMemory stays FALSE, callback never fires.  The cache then runs at
// full configured size — an advisory miss, not a correctness problem.

#include "osstd.hxx"

#include "memorynotification.hxx"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace
{
//  PSI trigger spec: fire when *some* task is stalled on memory for at
//  least 150 ms inside a 1000 ms window.  Conservative-ish — tight enough
//  to catch real pressure, loose enough that brief allocation spikes
//  don't repeatedly wake the maintenance task.
constexpr const char c_psiTrigger[] = "some 150000 1000000\n";

//  PSI Query threshold: report fLowMemory == TRUE when "some" pressure
//  averaged over the last 10 s exceeds this percentage of wall-clock.
constexpr double c_psiQueryThresholdPercent = 5.0;

//  /proc/meminfo fallback: report fLowMemory == TRUE when MemAvailable
//  drops below this fraction of MemTotal.  Engine-side cache reacts
//  before the OOM killer becomes likely.
constexpr double c_meminfoMinAvailableFraction = 0.05;

enum class PsiMode
{
    NotAvailable,
    //  /proc/pressure/memory missing entirely — no poller, no signal
    Trigger,
    //  Kernel accepted our trigger — poll(POLLPRI) on psiFd
    CounterPoll,
    //  Trigger rejected (Ubuntu et al.) — poll counters every 1 s
};

struct PosixMemoryNotification
{
    PfnMemNotification pfnCallback = nullptr;
    DWORD_PTR dwContext = 0;

    PsiMode mode = PsiMode::NotAvailable;
    int psiFd = -1;
    bool hasPoller = false;
    pthread_t pollerThread = 0;
    int wakePipe[2] = {-1, -1};

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    bool armed = false;
    bool shuttingDown = false;
};

//  Counter-polling cadence when the kernel rejects our PSI trigger.
//  1 s matches the trigger's window, so we observe the same "10s window
//  avg" smoothing the trigger would have done.
constexpr int c_counterPollIntervalMs = 1000;

//  Read a small /proc file into a stack buffer.  Returns bytes read (0
//  on failure), null-terminating.
size_t ReadProcFile(const char* path, char* buf, size_t cbBuf)
{
    if (cbBuf == 0)
    {
        return 0;
    }
    buf[0] = '\0';
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
    {
        return 0;
    }
    ssize_t n;
    do
    {
        n = read(fd, buf, cbBuf - 1);
    } while (n < 0 && errno == EINTR);
    close(fd);
    if (n <= 0)
    {
        return 0;
    }
    buf[n] = '\0';
    return (size_t) n;
}

//  Read /proc/pressure/memory and extract the "some" avg10 percentage.
//  Returns -1.0 on failure (caller falls back to meminfo).
double ReadPsiSomeAvg10()
{
    char buf[512];
    if (ReadProcFile("/proc/pressure/memory", buf, sizeof(buf)) == 0)
    {
        return -1.0;
    }
    //  Format: "some avg10=N.NN avg60=... avg300=... total=...\nfull ..."
    const char* const some = strstr(buf, "some avg10=");
    if (!some)
    {
        return -1.0;
    }
    char* end = nullptr;
    const double avg10 = strtod(some + strlen("some avg10="), &end);
    if (end == nullptr || avg10 < 0.0)
    {
        return -1.0;
    }
    return avg10;
}

//  Fallback for kernels without PSI.  Returns TRUE when MemAvailable is a
//  small fraction of MemTotal.  Returns FALSE on parse failure (safe
//  default — engine treats this as "memory is fine").
bool MeminfoIsLow()
{
    char buf[4096];
    if (ReadProcFile("/proc/meminfo", buf, sizeof(buf)) == 0)
    {
        return false;
    }
    auto FindLine = [ & ](const char* key) -> unsigned long long
    {
        const char* p = strstr(buf, key);
        if (!p)
        {
            return 0;
        }
        p += strlen(key);
        return strtoull(p, nullptr, 10);
    };
    const unsigned long long total = FindLine("MemTotal:");
    const unsigned long long available = FindLine("MemAvailable:");
    if (total == 0)
    {
        return false;
    }
    return (double) available < (double) total * c_meminfoMinAvailableFraction;
}

//  Fire the armed callback once.  Returns true if it fired (state moves
//  back to disarmed); false if there was nothing to fire.
bool FireArmedCallback(PosixMemoryNotification* pn)
{
    PfnMemNotification cb = nullptr;
    DWORD_PTR ctx = 0;
    pthread_mutex_lock(&pn->mutex);
    if (pn->armed && !pn->shuttingDown)
    {
        cb = pn->pfnCallback;
        ctx = pn->dwContext;
        pn->armed = false;
    }
    pthread_mutex_unlock(&pn->mutex);
    if (cb)
    {
        cb(ctx);
        return true;
    }
    return false;
}

void* PollerThreadMain(void* arg)
{
    PosixMemoryNotification* const pn = static_cast<PosixMemoryNotification*>(arg);

    const bool fUseTrigger = (pn->mode == PsiMode::Trigger);
    const int timeoutMs = fUseTrigger
                          ? -1
                          : c_counterPollIntervalMs;

    while (true)
    {
        struct pollfd fds[2];
        int nfds = 0;
        if (fUseTrigger)
        {
            fds[nfds].fd = pn->psiFd;
            fds[nfds].events = POLLPRI;
            fds[nfds].revents = 0;
            nfds++;
        }
        fds[nfds].fd = pn->wakePipe[0];
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        const int idxWake = nfds++;

        int n;
        do
        {
            n = poll(fds, nfds, timeoutMs);
        } while (n < 0 && errno == EINTR);
        if (n < 0)
        {
            //  Defensive: unexpected error.  Bail to avoid a spin.
            break;
        }

        if (fds[idxWake].revents & POLLIN)
        {
            char drain[16];
            (void) read(pn->wakePipe[0], drain, sizeof(drain));
            break;
        }

        bool firePressure = false;
        if (fUseTrigger && (fds[0].revents & POLLPRI))
        {
            firePressure = true;
        }
        else if (!fUseTrigger && n == 0)
        {
            //  Timeout in counter-poll mode — check PSI counters directly.
            const double avg10 = ReadPsiSomeAvg10();
            firePressure = (avg10 >= c_psiQueryThresholdPercent);
        }

        if (firePressure)
        {
            (void) FireArmedCallback(pn);
        }
    }

    return nullptr;
}

//  Try to set up PSI for this notification.  Sets pn->mode to:
//   * Trigger      — kernel accepted our user trigger; poll on POLLPRI
//   * CounterPoll  — /proc/pressure/memory exists, trigger rejected; poll
//                    counters every c_counterPollIntervalMs
//   * NotAvailable — PSI absent entirely; no poller, no callback firing
bool StartPsiPoller(PosixMemoryNotification* pn)
{
    pn->psiFd = open("/proc/pressure/memory", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (pn->psiFd < 0)
    {
        //  PSI counters might still be readable read-only, but without
        //  the FD the poller has nothing to wait on.  Try RO so Query
        //  still gets real data even with no poller.
        pn->psiFd = open("/proc/pressure/memory", O_RDONLY | O_CLOEXEC);
        if (pn->psiFd < 0)
        {
            pn->mode = PsiMode::NotAvailable;
            return false;
        }
        //  Can't write the trigger over a RO fd; drop straight to the
        //  counter-poll path so we at least poll counters.
        pn->mode = PsiMode::CounterPoll;
    }
    else
    {
        ssize_t w;
        do
        {
            w = write(pn->psiFd, c_psiTrigger, sizeof(c_psiTrigger) - 1);
        } while (w < 0 && errno == EINTR);
        pn->mode = (w >= 0)
                   ? PsiMode::Trigger
                   : PsiMode::CounterPoll;
    }

    if (pipe2(pn->wakePipe, O_CLOEXEC | O_NONBLOCK) != 0)
    {
        close(pn->psiFd);
        pn->psiFd = -1;
        pn->mode = PsiMode::NotAvailable;
        return false;
    }
    if (pthread_create(&pn->pollerThread, nullptr, PollerThreadMain, pn) != 0)
    {
        close(pn->psiFd);
        close(pn->wakePipe[0]);
        close(pn->wakePipe[1]);
        pn->psiFd = -1;
        pn->wakePipe[0] = -1;
        pn->wakePipe[1] = -1;
        pn->mode = PsiMode::NotAvailable;
        return false;
    }
    pn->hasPoller = true;
    return true;
}
} // anonymous

ERR ErrOSCreateLowMemoryNotification(
    PfnMemNotification const pfnCallback,
    DWORD_PTR const dwContext,
    _Out_ HMEMORY_NOTIFICATION* const ppNotification)
{
    PosixMemoryNotification* pn = new PosixMemoryNotification;
    if (!pn)
    {
        *ppNotification = nullptr;
        return ErrERRCheck(JET_errOutOfMemory);
    }
    pn->pfnCallback = pfnCallback;
    pn->dwContext = dwContext;
    (void) StartPsiPoller(pn); //  Failure is fine — degrades to no-op.
    *ppNotification = pn;
    return JET_errSuccess;
}

ERR ErrOSRegisterMemoryNotification(_In_ HMEMORY_NOTIFICATION pvNotification)
{
    PosixMemoryNotification* const pn = static_cast<PosixMemoryNotification*>(pvNotification);
    if (!pn)
    {
        return ErrERRCheck(JET_errInvalidParameter);
    }
    //  Arm one-shot callback.  If PSI isn't available the callback will
    //  simply never fire — engine retries Register later via its own
    //  scheduling path.
    pthread_mutex_lock(&pn->mutex);
    pn->armed = true;
    pthread_mutex_unlock(&pn->mutex);
    return JET_errSuccess;
}

ERR ErrOSQueryMemoryNotification(HMEMORY_NOTIFICATION const pvNotification,
    _Out_ BOOL* const pfLowMemory)
{
    if (pfLowMemory == nullptr)
    {
        return ErrERRCheck(JET_errInvalidParameter);
    }
    *pfLowMemory = fFalse;
    PosixMemoryNotification* const pn = static_cast<PosixMemoryNotification*>(pvNotification);
    if (!pn)
    {
        return ErrERRCheck(JET_errInvalidParameter);
    }
    const double psiAvg10 = ReadPsiSomeAvg10();
    if (psiAvg10 >= 0.0)
    {
        *pfLowMemory = (psiAvg10 >= c_psiQueryThresholdPercent)
                       ? fTrue
                       : fFalse;
    }
    else if (MeminfoIsLow())
    {
        *pfLowMemory = fTrue;
    }
    return JET_errSuccess;
}

VOID OSUnregisterAndDestroyMemoryNotification(HMEMORY_NOTIFICATION const pvNotification)
{
    PosixMemoryNotification* const pn = static_cast<PosixMemoryNotification*>(pvNotification);
    if (!pn)
    {
        return;
    }
    if (pn->hasPoller)
    {
        pthread_mutex_lock(&pn->mutex);
        pn->shuttingDown = true;
        pn->armed = false;
        pthread_mutex_unlock(&pn->mutex);
        //  Wake the poller and join it.  Pipe writes can't fail here in
        //  practice (single-byte write on a pipe with capacity 64K), but
        //  retry on EINTR just in case.
        char b = 'x';
        ssize_t w;
        do
        {
            w = write(pn->wakePipe[1], &b, 1);
        } while (w < 0 && errno == EINTR);
        pthread_join(pn->pollerThread, nullptr);
        close(pn->wakePipe[0]);
        close(pn->wakePipe[1]);
    }
    if (pn->psiFd >= 0)
    {
        close(pn->psiFd);
    }
    delete pn;
}
