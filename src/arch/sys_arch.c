/*
 * Copyright (c) 2011, Stefan Lankes, RWTH Aachen University
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the University nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <hermit/stddef.h>
#include <hermit/time.h>

#include "lwip/opt.h"
#include "lwip/debug.h"
#include "lwip/sys.h"
#include "lwip/stats.h"
#include "lwip/sockets.h"
#include "lwip/err.h"

#ifndef TRUE
#define TRUE	1
#endif

#ifndef FALSE
#define FALSE	0
#endif

#if SYS_LIGHTWEIGHT_PROT && !NO_SYS
#if MAX_CORES > 1
static spinlock_irqsave_t lwprot_lock;
#endif
#endif

/** Returns the current time in milliseconds,
 * may be the same as sys_jiffies or at least based on it. */
u32_t sys_now(void)
{
	return (get_clock_tick() / TIMER_FREQ) * 1000;
}

u32_t sys_jiffies(void)
{
	return (get_clock_tick() / TIMER_FREQ) * 1000;
}

#if !NO_SYS

/* sys_init(): init needed system resources
 * Note: At the moment there are none
 */
void sys_init(void)
{
#if SYS_LIGHTWEIGHT_PROT
#if MAX_CORES > 1
	spinlock_irqsave_init(&lwprot_lock);
#endif
#endif
}

/* sys_thread_new(): Spawns a new thread with given attributes as supported
 * Note: In HermitCore this is realized as kernel tasks
 */
sys_thread_t sys_thread_new(const char *name, lwip_thread_fn thread, void *arg,
		int stacksize, int prio)
{
	int err;
	sys_thread_t id;

	LWIP_UNUSED_ARG(name);
	LWIP_UNUSED_ARG(stacksize);

	err = create_kernel_task(&id, (entry_point_t)thread, arg, prio);
	LWIP_DEBUGF(SYS_DEBUG, ("sys_thread_new: create_kernel_task %d, id = %u, prio = %d\n", err, id, prio));

	return id;
}

/* sys_sem_free(): destroy's given semaphore
 * and releases system resources.
 * This semaphore also gets invalid.
 */
void sys_sem_free(sys_sem_t* sem)
{
	if (BUILTIN_EXPECT(sem != NULL, 1)) {
		sem->valid = FALSE;
		SYS_STATS_DEC(sem.used);
		sem_destroy(&sem->sem);
	}
}

/* sys_sem_valid(): returns if semaphore is valid 
 * at the moment
 */
int sys_sem_valid(sys_sem_t* sem)
{
	if (BUILTIN_EXPECT(sem == NULL, 0))
		return FALSE;
	return sem->valid;
}

/* sys_sem_new(): creates a new semaphre with given count.
 * This semaphore becomes valid
 */
err_t sys_sem_new(sys_sem_t* s, u8_t count)
{
	int err;

	if (BUILTIN_EXPECT(!s, 0))
		return ERR_VAL;

	err = sem_init(&s->sem, count);
	if (err < 0)
		return ERR_VAL;

	SYS_STATS_INC_USED(sem);
	s->valid = TRUE;

	return ERR_OK;
}

/* sys_sem_set_invalid(): this semapohore becomes invalid
 * Note: this does not mean it is destroyed
 */
void sys_sem_set_invalid(sys_sem_t * sem)
{
	sem->valid = FALSE;
}

/* sys_sem_signal(): this semaphore is signaled
 *
 */
void sys_sem_signal(sys_sem_t* sem)
{
	sem_post(&sem->sem);
}

/* sys_arch_sem_wait): wait for the given semaphore for
 * a given timeout
 * Note: timeout = 0 means wait forever
 */
u32_t sys_arch_sem_wait(sys_sem_t *sem, u32_t timeout)
{
	int err;

	err = sem_wait(&sem->sem, timeout);
	if (!err)
		return 0;

	return SYS_ARCH_TIMEOUT;
}

/* sys_mbox_valid() : returns if the given mailbox
 * is valid
 */
int sys_mbox_valid(sys_mbox_t * mbox)
{
	if (BUILTIN_EXPECT(mbox == NULL, 0))
		return FALSE;
	return mbox->valid;
}

/* sys_arch_mbox_fetch(): wait for the given mailbox for a specified
 * amount of time.
 * Note: timeout = 0 means wait forever
 */
u32_t sys_arch_mbox_fetch(sys_mbox_t * mbox, void **msg, u32_t timeout)
{
	int err;

	err = mailbox_ptr_fetch(&mbox->mailbox, msg, timeout);
	//LWIP_DEBUGF(SYS_DEBUG, ("sys_arch_mbox_fetch: %d\n", err));
	if (!err)
		return 0;

	return SYS_ARCH_TIMEOUT;
}

/* sys_mbox_free() : free the given mailbox, release the system resources
 * and set mbox to invalid
 */
void sys_mbox_free(sys_mbox_t* mbox)
{
	if (BUILTIN_EXPECT(mbox != NULL, 1)) {
		mbox->valid = FALSE;
		SYS_STATS_DEC(mbox.used);
		mailbox_ptr_destroy(&mbox->mailbox);
	}
}

/* sys_arch_mbox_tryfetch(): poll for new data in mailbox
 *
 */
u32_t sys_arch_mbox_tryfetch(sys_mbox_t* mbox, void** msg)
{
	int ret = mailbox_ptr_tryfetch(&mbox->mailbox, msg);
	if (ret)
		return SYS_MBOX_EMPTY;
	return 0;
}

/* sys_mbox_new(): create a new mailbox with a minimum size of "size"
 *
 */
err_t sys_mbox_new(sys_mbox_t* mb, int size)
{
	int err;
	
	//LWIP_DEBUGF(SYS_DEBUG, ("sys_mbox_new: create mailbox with the minimum size: %d\n", size));
	if (BUILTIN_EXPECT(!mb, 0))
		return ERR_VAL;

	mb->valid = TRUE;
	SYS_STATS_INC_USED(mbox);
	err = mailbox_ptr_init(&mb->mailbox);
	if (err)
		return ERR_MEM;
	return ERR_OK;
}

/* sys_mbox_set_invalid(): set the given mailbox to invald
 * Note: system resources are NOT freed
 */
void sys_mbox_set_invalid(sys_mbox_t* mbox)
{
	mbox->valid = FALSE;
}

/* sys_mbox_trypost(): try to post data to the mailbox
 *
 */
err_t sys_mbox_trypost(sys_mbox_t *mbox, void *msg)
{
	int err;

	err = mailbox_ptr_trypost(&mbox->mailbox, msg);
	if (err != 0) {
		LWIP_DEBUGF(SYS_DEBUG, ("sys_mbox_trypost: %d\n", err));
		return ERR_MEM;
	}

	return ERR_OK;
}

/* sys_mbox_post(): post new data to the mailbox
 *
 */
void sys_mbox_post(sys_mbox_t* mbox, void* msg)
{
	mailbox_ptr_post(&mbox->mailbox, msg);
}

/* sys_mutex_lock(): lock the given mutex
 * Note: There is no specific mutex in 
 * HermitCore so we use a semaphore with
 * 1 element
 */
void sys_mutex_lock(sys_mutex_t* mutex)
{
	sem_wait(mutex, 0);
}

/* sys_mutex_unlock(): unlock the given mutex
 *
 */
void sys_mutex_unlock(sys_mutex_t* mutex)
{
	sem_post(mutex);
}

/* sys_mutex_new(): create a new mutex
 *
 */
err_t sys_mutex_new(sys_mutex_t * m)
{
	if (BUILTIN_EXPECT(!m, 0))
		return ERR_VAL;
	SYS_STATS_INC_USED(mutex);
	sem_init(m, 1);
	return ERR_OK;
}

#if SYS_LIGHTWEIGHT_PROT
#if MAX_CORES > 1
sys_prot_t sys_arch_protect(void)
{
	spinlock_irqsave_lock(&lwprot_lock);
	return ERR_OK;
}

void sys_arch_unprotect(sys_prot_t pval)
{
	LWIP_UNUSED_ARG(pval);
	spinlock_irqsave_unlock(&lwprot_lock);
}
#endif
#endif

int* __getreent(void);

static inline int* libc_errno(void)
{
	return __getreent();
}

int accept(int s, struct sockaddr *addr, socklen_t *addrlen)
{
	int fd = lwip_accept(s & ~LWIP_FD_BIT, addr, addrlen);

	if (fd < 0)
	{
		*libc_errno() = errno;
		return -1;
	}

	return fd | LWIP_FD_BIT;
}

int bind(int s, const struct sockaddr *name, socklen_t namelen)
{
	int ret = lwip_bind(s & ~LWIP_FD_BIT, name, namelen);


	if (ret)
	{
		*libc_errno() = errno;
		return -1;
	}

	return 0;
}

int getpeername(int s, struct sockaddr *name, socklen_t *namelen)
{
	int ret = lwip_getpeername(s & ~LWIP_FD_BIT, name, namelen);

	if (ret)
	{
		*libc_errno() = errno;
		return -1;
	}

	return 0;
}

int getsockname(int s, struct sockaddr *name, socklen_t *namelen)
{
	int ret = lwip_getsockname(s & ~LWIP_FD_BIT, name, namelen);

	if (ret)
	{
		*libc_errno() = errno;
		return -1;
	}

	return 0;
}

int getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
	int ret = lwip_getsockopt(s & ~LWIP_FD_BIT, level, optname, optval, optlen);

	if (ret)
	{
		*libc_errno() = errno;
		return -1;
	}

	return 0;
}

int setsockopt(int s, int level, int optname, const void *optval, socklen_t optlen)
{
	int ret = lwip_setsockopt(s & ~LWIP_FD_BIT, level, optname, optval, optlen);

	if (ret)
	{
		*libc_errno() = errno;
		return -1;
	}

	return 0;
}

int connect(int s, const struct sockaddr *name, socklen_t namelen)
{
	int ret = lwip_connect(s & ~LWIP_FD_BIT, name, namelen);

	if (ret)
	{
		*libc_errno() = errno;
		return -1;
	}

	return 0;
}

int listen(int s, int backlog)
{
	int ret = lwip_listen(s & ~LWIP_FD_BIT, backlog);

	if (ret)
	{
		*libc_errno() = errno;
		return -1;
	}

	return 0;
}

int recv(int s, void *mem, size_t len, int flags)
{
	int ret = lwip_recv(s & ~LWIP_FD_BIT, mem, len, flags);

	if (ret < 0)
	{
		*libc_errno() = errno;
		return -1;
	}

	return ret;
}

int recvfrom(int s, void *mem, size_t len, int flags, struct sockaddr *from, socklen_t *fromlen)
{
	int ret = lwip_recvfrom(s & ~LWIP_FD_BIT, mem, len, flags, from, fromlen);

	if (ret < 0)
	{
		*libc_errno() = errno;
		return -1;
	}

	return ret;
}

int send(int s, const void *dataptr, size_t size, int flags)
{
	int ret = lwip_send(s & ~LWIP_FD_BIT, dataptr, size, flags);

	if (ret < 0)
	{
		*libc_errno() = errno;
		return -1;
	}

	return ret;
}

int sendto(int s, const void *dataptr, size_t size, int flags, const struct sockaddr *to, socklen_t tolen)
{
	int ret = lwip_sendto(s & ~LWIP_FD_BIT, dataptr, size, flags, to, tolen);

	if (ret < 0)
	{
		*libc_errno() = errno;
		return -1;
	}

	return ret;
}

int socket(int domain, int type, int protocol)
{
	int fd = lwip_socket(domain, type, protocol);

	if (fd < 0)
	{
		*libc_errno() = errno;
		return -1;
	}

	return fd | LWIP_FD_BIT;
}

int select(int maxfdp1, fd_set *readset, fd_set *writeset, fd_set *exceptset, struct timeval *timeout)
{
	int ret;

	ret = lwip_select(maxfdp1, readset, writeset, exceptset, timeout);

	if (ret < 0)
	{
		*libc_errno() = errno;
		return -1;
	}

	return ret;
}

#endif /* !NO_SYS */
