// POSIX equivalent of os/task.cxx. Linux has no IOCP, so we always run the
// non-IOCP code path of CTaskManager — a simple CSemaphore/CCriticalSection
// work queue dispatched onto a pthread pool via ErrUtilThreadCreate.
// CGPTaskManager wraps CTaskManager (the QueueUserWorkItem fast path is
// Windows-only and is omitted entirely on Linux).

#include "osstd.hxx"

LOCAL const CHAR * const    szCritTaskList      = "CTaskManager::m_critTask";
LOCAL const CHAR * const    szCritActiveThread  = "CTaskManager::m_critActiveThread";
LOCAL const CHAR * const    szSemTaskDispatch   = "CTaskManager::m_semTaskDispatch";
LOCAL const CHAR * const    szAsigAllDone       = "CGPTaskManager::m_asigAllDone";
LOCAL const CHAR * const    szRwlPostTasks      = "CGPTaskManager::m_rwlPostTasks";


////////////////////////////////////////////////
//
//  Generic Task Manager
//

CTaskManager::CTaskManager()
    :       m_critTask( CLockBasicInfo( CSyncBasicInfo( szCritTaskList ), rankCritTaskList, 0 ) ),
            m_critActivateThread( CLockBasicInfo( CSyncBasicInfo( szCritActiveThread ), rankCritTaskList, 0 ) ),
            m_semTaskDispatch( CSyncBasicInfo( szSemTaskDispatch ) ),
            m_rgpTaskNode( NULL ),
            m_cThread( 0 ),
            m_cThreadMax( 0 ),
            m_cPostedTasks( 0 ),
            m_cmsLastActivateThreadTime( 0 ),
            m_cTasksThreshold( 0 ),
            m_rgThreadContext( NULL ),
            m_hIOCPTaskDispatch( NULL ),
            m_fIOCPHasRegisteredFile( fFalse ),
#ifndef RTM
            m_irrcpiLast( 0 ),
#endif
            m_pfnFileIOCompletion( NULL )
{
#ifndef RTM
    memset( m_rgcpiLast, 0, _countof(m_rgcpiLast)*sizeof(m_rgcpiLast[0]) );
#endif
}

CTaskManager::~CTaskManager()
{
}


ERR CTaskManager::ErrTMInit(    const ULONG                     cThread,
                                const DWORD_PTR *const          rgThreadContext,
                                const BOOL                      fForceMaxThreads )
{
    ERR err;

    Assert( 0 == m_cThread );
    Assert( !m_rgThreadContext );
    Assert( m_ilTask.FEmpty() );
    Assert( 0 == m_semTaskDispatch.CAvail() );
    Assert( !m_rgpTaskNode );

    m_cThread = 0;
    Alloc( m_rgThreadContext = new THREADCONTEXT[cThread] );
    memset( m_rgThreadContext, 0, sizeof( THREADCONTEXT ) * cThread );

    //  allocate extra task-nodes for TMTerm (prevents out-of-memory)

    Alloc( m_rgpTaskNode = new CTaskNode*[cThread] );
    memset( m_rgpTaskNode, 0, cThread * sizeof( CTaskNode * ) );
    for ( ULONG iThread = 0; iThread < cThread; iThread++ )
    {
        Alloc( m_rgpTaskNode[iThread] = new CTaskNode() );
    }

    Assert( cThread < 1000 );
    m_cThreadMax = cThread;

    for ( size_t iThread = 0; iThread < cThread; iThread++ )
    {
        m_rgThreadContext[iThread].dwThreadContext = rgThreadContext ? rgThreadContext[iThread] : NULL;
        m_rgThreadContext[iThread].ptm = this;
    }

    Assert( 0 == m_cThread );

    if ( fForceMaxThreads )
    {
        Call( ErrAddThreadCheck( fForceMaxThreads ) );
    }

    Assert( fForceMaxThreads ? cThread == m_cThread : 0 == m_cThread );

    return JET_errSuccess;

HandleError:
    TMTerm();
    return err;
}


VOID CTaskManager::TMTerm()
{
    ULONG iThread;
    ULONG cThread;

    m_critActivateThread.Enter();
    m_cTasksThreshold   = 0xffffffff;
    m_cThreadMax        = 0;
    m_critActivateThread.Leave();

    cThread = m_cThread;

    if ( NULL != m_rgThreadContext )
    {
        for ( iThread = 0; iThread < cThread; iThread++ )
        {
            //  verify the extra task-node

            Assert( m_rgpTaskNode );
            Assert( m_rgpTaskNode[iThread] );

            m_rgpTaskNode[iThread]->m_pfnCompletion = TMITermTask;

            m_critTask.Enter();
            m_ilTask.InsertAsNextMost( m_rgpTaskNode[iThread] );
            m_critTask.Leave();
            m_semTaskDispatch.Release();

            //  prevent cleanup from freeing the extra task-node

            m_rgpTaskNode[iThread] = NULL;
        }

        for ( iThread = 0; iThread < cThread; iThread++ )
        {
            if ( m_rgThreadContext[iThread].thread )
            {
                UtilThreadEnd( m_rgThreadContext[iThread].thread );
            }
        }

        delete[] m_rgThreadContext;
    }
    m_rgThreadContext = NULL;
    m_cThread = 0;
    m_cTasksThreshold = 0;
    m_cmsLastActivateThreadTime = 0;

    Assert( m_ilTask.FEmpty() );
    if ( !m_ilTask.FEmpty() )
    {
        m_critTask.Enter();
        while ( !m_ilTask.FEmpty() )
        {
            CTaskNode *ptn = m_ilTask.PrevMost();
            m_ilTask.Remove( ptn );
            delete ptn;
        }
        m_critTask.Leave();
    }

    Assert( 0 == m_semTaskDispatch.CAvail() );
    while ( m_semTaskDispatch.FTryAcquire() )
    {
    }

    if ( m_rgpTaskNode )
    {
        for ( iThread = 0; iThread < m_cThread; iThread++ )
        {
            delete m_rgpTaskNode[iThread];
        }
        delete [] m_rgpTaskNode;
    }
    m_rgpTaskNode = NULL;
}


ERR CTaskManager::ErrTMPost(    CTaskManager::PfnCompletion pfnCompletion,
                                const DWORD                 dwCompletionKey1,
                                const DWORD_PTR             dwCompletionKey2 )
{
    ERR err = JET_errSuccess;

    Assert( pfnCompletion );

    AtomicIncrement( (LONG *)&m_cPostedTasks );

    if ( ErrAddThreadCheck( fFalse ) < JET_errSuccess )
    {
        if ( m_cThread == 0 )
        {
            Error( ErrERRCheck( JET_errOutOfMemory ) );
        }
    }

    {
        CTaskNode *ptn = new CTaskNode();
        if ( !ptn )
        {
            Error( ErrERRCheck( JET_errOutOfMemory ) );
        }
        ptn->m_pfnCompletion = pfnCompletion;
        ptn->m_dwCompletionKey1 = dwCompletionKey1;
        ptn->m_dwCompletionKey2 = dwCompletionKey2;

        ETTaskManagerPost( this, pfnCompletion, dwCompletionKey1, (void*)dwCompletionKey2 );

        m_critTask.Enter();
        m_ilTask.InsertAsNextMost( ptn );
        m_critTask.Leave();
        m_semTaskDispatch.Release();
    }

HandleError:
    if ( err < JET_errSuccess )
    {
        AtomicDecrement( (LONG *)&m_cPostedTasks );
    }

    return err;
}


//  on POSIX there is no IOCP to register against — file I/O completion is
//  handled directly by the file layer (io_uring), so this just succeeds.

BOOL CTaskManager::FTMRegisterFile( VOID *hFile, CTaskManager::PfnCompletion pfnCompletion )
{
    Assert( hFile && (hFile != INVALID_HANDLE_VALUE));
    Assert( pfnCompletion );
    return fTrue;
}

BOOL CTaskManager::FTMIFileIOCompletion( CTaskManager::PfnCompletion pfnCompletion )
{
    return m_pfnFileIOCompletion == pfnCompletion;
}


VOID CTaskManager::TMRequestIdleCallback(   TICK            dtickIdle,
                                            PfnCompletion   pfnIdle )
{
    Assert( Postls()->fIsTaskThread );
    Assert( Postls()->pfnTaskThreadIdle == NULL || Postls()->pfnTaskThreadIdle == pfnIdle );

    Postls()->dtickTaskThreadIdle   = dtickIdle;
    Postls()->pfnTaskThreadIdle     = pfnIdle;
}


DWORD CTaskManager::TMDispatch( DWORD_PTR dwContext )
{
    THREADCONTEXT *ptc;

    Assert( 0 != dwContext );
    ptc = (THREADCONTEXT *)dwContext;

    ptc->ptm->TMIDispatch( ptc->dwThreadContext );

    return 0;
}


VOID CTaskManager::TMIDispatch( const DWORD_PTR dwThreadContext )
{
    COMPLETIONPACKETINFO     cpi = { 0 };

    Assert( !Postls()->fIsTaskThread );
    Postls()->fIsTaskThread = fTrue;

    while ( Postls()->fIsTaskThread )
    {
        cpi.fDequeued               = fFalse;
        cpi.pfnCompletion           = NULL;
        cpi.dwCompletionKey1        = 0;
        cpi.dwCompletionKey2        = 0;
        cpi.fGQCSSuccess            = fFalse;
        cpi.gle                     = (DWORD)errCodeInconsistency;
        cpi.tickStartWait           = TickOSTimeCurrent();
        cpi.tickComplete            = (TICK)-1;
        OnNonRTM( cpi.dtickIdle     = Postls()->dtickTaskThreadIdle );
        OnNonRTM( cpi.pfnIdle       = Postls()->pfnTaskThreadIdle );

        CTaskNode *ptn;

        if ( m_semTaskDispatch.FAcquire( Postls()->dtickTaskThreadIdle ? Postls()->dtickTaskThreadIdle : cmsecInfiniteNoDeadlock ) )
        {
            m_critTask.Enter();
            ptn = m_ilTask.PrevMost();
            Assert( ptn );
            m_ilTask.Remove( ptn );
            m_critTask.Leave();

            cpi.pfnCompletion       = ptn->m_pfnCompletion;
            cpi.dwCompletionKey1    = ptn->m_dwCompletionKey1;
            cpi.dwCompletionKey2    = ptn->m_dwCompletionKey2;
            cpi.fDequeued           = fTrue;
            cpi.gle                 = ERROR_SUCCESS;
            cpi.tickComplete        = TickOSTimeCurrent();

            delete ptn;
        }
        else
        {
            //  fire a timeout event (idle callback)

            cpi.pfnCompletion       = Postls()->pfnTaskThreadIdle;
            cpi.dwCompletionKey1    = 0;
            cpi.dwCompletionKey2    = 0;
            cpi.fDequeued           = fTrue;
            cpi.gle                 = ERROR_SUCCESS;
            cpi.tickComplete        = TickOSTimeCurrent();

            AtomicIncrement( (LONG *)&m_cPostedTasks );
        }

        OnNonRTM( const DWORD irrcpi = AtomicIncrement( (LONG*)&m_irrcpiLast ) );
        OnNonRTM( m_rgcpiLast[irrcpi%_countof(m_rgcpiLast)] = cpi );

        AssertRTL( !cpi.fDequeued || cpi.pfnCompletion != NULL );

        if ( cpi.fDequeued )
        {
            Enforce( cpi.pfnCompletion );

            if ( cpi.pfnCompletion == Postls()->pfnTaskThreadIdle )
            {
                Postls()->dtickTaskThreadIdle   = 0;
                Postls()->pfnTaskThreadIdle     = NULL;
            }

            SetLastError( cpi.gle );

            if ( !FTMIFileIOCompletion( cpi.pfnCompletion ) )
            {
                ETTaskManagerRun( this,
                              cpi.pfnCompletion,
                              cpi.dwCompletionKey1,
                              (void*)cpi.dwCompletionKey2,
                              cpi.gle,
                              (void*)dwThreadContext );
            }

            cpi.pfnCompletion( cpi.gle, dwThreadContext, cpi.dwCompletionKey1, cpi.dwCompletionKey2 );

            const LONG count = AtomicDecrement( (LONG *)&m_cPostedTasks );
            Assert( count >= 0 || m_fIOCPHasRegisteredFile );
        }
    }
}


VOID CTaskManager::TMITermTask( const DWORD     dwError,
                                const DWORD_PTR dwThreadContext,
                                const DWORD     dwCompletionKey1,
                                const DWORD_PTR dwCompletionKey2 )
{
    Expected( dwError == ERROR_SUCCESS );
    Assert( Postls()->fIsTaskThread );
    Postls()->fIsTaskThread = fFalse;
}


BOOL CTaskManager::FNeedNewThread() const
{
    BOOL fNeedNewThread         = fFalse;

    const ULONG tickCurr        = TickOSTimeCurrent();
    const ULONG tickThreshold   = m_cThread * 1000 / CUtilProcessProcessor();
    const ULONG tickNext        = m_cmsLastActivateThreadTime + tickThreshold;

    if (    0 == m_cThread
        || (
                m_cThread < m_cThreadMax
            &&  !m_fIOCPHasRegisteredFile
            &&  m_cPostedTasks > m_cTasksThreshold
            &&  TickCmp( tickCurr, tickNext ) >= 0 ) )
    {
        fNeedNewThread = fTrue;
    }

    return fNeedNewThread;
}


ERR CTaskManager::ErrAddThreadCheck( const BOOL fForceMaxThreads )
{
    ERR err = JET_errSuccess;

    if( FNeedNewThread()
        && m_critActivateThread.FTryEnter() )
    {
        if( FNeedNewThread() )
        {
            do
            {
                err = ErrUtilThreadCreate(
                                PUTIL_THREAD_PROC( TMDispatch ),
                                OSMemoryPageReserveGranularity(),
                                priorityNormal,
                                &m_rgThreadContext[m_cThread].thread,
                                DWORD_PTR( m_rgThreadContext + m_cThread ) );
                if ( err < JET_errSuccess )
                    break;

                AtomicIncrement( (LONG *)&m_cThread );

                if ( m_cThread == m_cThreadMax )
                {
                    m_cTasksThreshold = 0xffffffff;
                }
                else
                {
                    Assert( m_cThread < m_cThreadMax );
                    m_cmsLastActivateThreadTime = TickOSTimeCurrent();
                    m_cTasksThreshold           = m_cThread * 8;
                }
            }
            while ( m_cThread < m_cThreadMax && fForceMaxThreads );
        }

        m_critActivateThread.Leave();
    }
    return err;
}


////////////////////////////////////////////////
//
//  Task Manager wrapper for QUWI-style API
//
//  On Linux there is no QueueUserWorkItem, so we always fall through to
//  CTaskManager. The dispatch function is shared with the Windows variant
//  via TMIDispatch (TMIDispatchGP performs the actual TaskInfo Notify*
//  bookkeeping and runs the user pfn).
//

CGPTaskManager::CGPTaskManager()
    :   m_fInit( fFalse ),
        m_cPostedTasks( 0 ),
        m_asigAllDone( CSyncBasicInfo( szAsigAllDone ) ),
        m_rwlPostTasks( CLockBasicInfo( CSyncBasicInfo( szRwlPostTasks ), rankRwlPostTasks, 0 ) ),
        m_ptaskmanager( NULL )
{
}

CGPTaskManager::~CGPTaskManager()
{
    TMTerm();
}

ERR CGPTaskManager::ErrTMInit( const ULONG cThread )
{
    ERR err = JET_errSuccess;

    m_fInit             = fFalse;
    m_cPostedTasks      = 0;
    m_asigAllDone.Reset();
    m_ptaskmanager      = NULL;

    Alloc( m_ptaskmanager = new CTaskManager );
    Call( m_ptaskmanager->ErrTMInit( cThread ? cThread : CUtilProcessProcessor() ) );

    //  dummy refcount so TMTerm can tell whether to wait for outstanding tasks

    m_cPostedTasks = 1;
    m_fInit = fTrue;

HandleError:
    if ( err < JET_errSuccess )
    {
        TMTerm();
    }
    return err;
}

VOID CGPTaskManager::TMTerm()
{
    if ( m_fInit && !FUtilProcessAbort() )
    {
        m_rwlPostTasks.EnterAsWriter();
        m_fInit = fFalse;
        m_rwlPostTasks.LeaveAsWriter();

        if ( AtomicDecrement( (LONG *)&m_cPostedTasks ) )
        {
            m_asigAllDone.Wait();
        }

        if ( m_ptaskmanager )
        {
            m_ptaskmanager->TMTerm();
            delete m_ptaskmanager;
            m_ptaskmanager = NULL;
        }
    }
}

ERR CGPTaskManager::ErrTMPost( PfnCompletion pfnCompletion, VOID *pvParam, TaskInfo *pTaskInfo )
{
    ERR         err         = JET_errSuccess;
    PTMCallWrap ptmCallWrap = NULL;

    m_rwlPostTasks.EnterAsReader();

    if ( m_fInit )
    {
        const LONG  cTasksOutstanding   = AtomicIncrement( (LONG *)&m_cPostedTasks );
        Assert( cTasksOutstanding > 1 );

        ptmCallWrap = new TMCallWrap;
        if ( NULL != ptmCallWrap )
        {
            ptmCallWrap->pfnCompletion  = pfnCompletion;
            ptmCallWrap->pvParam        = pvParam;
            ptmCallWrap->pThis          = this;
            ptmCallWrap->pTaskInfo      = pTaskInfo;

            if (pTaskInfo)
            {
                pTaskInfo->NotifyPost();
            }

            ETGPTaskManagerPost( this, pfnCompletion, pvParam, pTaskInfo );

            err = m_ptaskmanager->ErrTMPost( TMIDispatch, 0, DWORD_PTR( ptmCallWrap ) );
        }
        else
        {
            err = ErrERRCheck( JET_errOutOfMemory );
        }

        if ( err < JET_errSuccess )
        {
            delete ptmCallWrap;
            ptmCallWrap = NULL;
            const LONG  cTasksRemaining     = AtomicDecrement( (LONG *)&m_cPostedTasks );
            Assert( cTasksRemaining >= 1 );
        }
    }
    else
    {
        err = ErrERRCheck( JET_errTaskDropped );
    }

    m_rwlPostTasks.LeaveAsReader();
    return err;
}

DWORD __stdcall CGPTaskManager::TMIDispatchGP( VOID *pvParam )
{
    PTMCallWrap     ptmCallWrap     = PTMCallWrap( pvParam );
    const BOOL      fIsTaskThread   = Postls()->fIsTaskThread;

    if ( !fIsTaskThread )
    {
        Postls()->fIsTaskThread = fTrue;
    }

    Assert( NULL != ptmCallWrap );

    if (ptmCallWrap->pTaskInfo)
    {
        ptmCallWrap->pTaskInfo->NotifyDispatch();
    }

    CLockDeadlockDetectionInfo::AssertCleanApiExit( 0, 0, 0 );

    ETGPTaskManagerRun( ptmCallWrap->pThis, ptmCallWrap->pfnCompletion, ptmCallWrap->pvParam, ptmCallWrap->pTaskInfo );

    //  on POSIX we always go through CTaskManager, so the QUWI-only
    //  exception-catching wrapper is unnecessary; tasks run directly.

    ptmCallWrap->pfnCompletion( ptmCallWrap->pvParam );

    CLockDeadlockDetectionInfo::AssertCleanApiExit( 0, 0, 0 );

    if ( !fIsTaskThread )
    {
        Postls()->fIsTaskThread = fFalse;
    }

    CGPTaskManager * const  pThis           = ptmCallWrap->pThis;

    if (ptmCallWrap->pTaskInfo)
    {
        ptmCallWrap->pTaskInfo->NotifyEnd();
    }

    delete ptmCallWrap;
    ptmCallWrap = NULL;

    const LONG              cTasks          = AtomicDecrement( (LONG *)&( pThis->m_cPostedTasks ) );

    if ( 0 == cTasks )
    {
        Assert( !pThis->m_fInit );
        pThis->m_asigAllDone.Set();
    }
    else
    {
        Assert( cTasks >= 1 );
    }

    return 0;
}

VOID CGPTaskManager::TMIDispatch(   const DWORD     dwError,
                                    const DWORD_PTR dwThreadContext,
                                    const DWORD     dwCompletionKey1,
                                    const DWORD_PTR dwCompletionKey2 )
{
    Expected( dwError == ERROR_SUCCESS );
    Assert( NULL == dwThreadContext );
    Assert( 0 == dwCompletionKey1 );
    TMIDispatchGP( (VOID *)dwCompletionKey2 );
}


BOOL FOSTaskIsTaskThread( void )
{
    return Postls()->fIsTaskThread;
}

void OSTaskPostterm()      {}
BOOL FOSTaskPreinit()      { return fTrue; }
void OSTaskTerm()          {}
ERR  ErrOSTaskInit()       { return JET_errSuccess; }


////////////////////////////////////////////////
//  TaskInfoGeneric / TaskInfoGenericStats — verbatim from task.cxx
//

void TaskInfoGeneric::NotifyPost ()
{
    m_dwPostTick = TickOSTimeCurrent( );
    m_dwDispatchTick = m_dwEndTick = 0;
}

void TaskInfoGeneric::NotifyDispatch ()
{
    m_dwDispatchTick = TickOSTimeCurrent( );
}

void TaskInfoGeneric::NotifyEnd()
{
    m_dwEndTick = TickOSTimeCurrent( );
    m_pTaskInfoGenericStats->ReportTaskInfo( *this );
    delete this;
}


void TaskInfoGenericStats::ReportTaskInfo(const TaskInfoGeneric & taskInfo)
{
    Assert( taskInfo.m_dwDispatchTick >= taskInfo.m_dwPostTick);
    Assert( taskInfo.m_dwEndTick >= taskInfo.m_dwDispatchTick);
    Assert( taskInfo.m_dwEndTick >= taskInfo.m_dwPostTick);
    const DWORD dwDispatchTick   = taskInfo.m_dwDispatchTick - taskInfo.m_dwPostTick;
    const DWORD dwExecuteTick    = taskInfo.m_dwEndTick - taskInfo.m_dwDispatchTick;
    const DWORD dwTotalTick      = taskInfo.m_dwEndTick - taskInfo.m_dwPostTick;

    Assert( dwTotalTick >= dwDispatchTick);
    Assert( dwTotalTick >= dwExecuteTick);

    if (dwDispatchTick >= m_msMaxDispatch ||
        dwExecuteTick >= m_msMaxExecute)
    {
        m_critStats.Enter();

        if ( ( taskInfo.m_dwEndTick < m_dwLastReportEndTick + dtickTaskAbnormalLatencyEvent ) &&
            ( dwDispatchTick < m_dwLastReportDispatch * 3 / 2) &&
            ( dwExecuteTick < m_dwLastReportExecute * 3 / 2) )
        {
            m_cLastReportNotReported++;
        }
        else
        {
            WCHAR szMaxDispatch[32];
            WCHAR szMaxExecute[32];
            WCHAR szTaskType[32];

            OSStrCbFormatW( szMaxDispatch, sizeof( szMaxDispatch ), L"%d", dwDispatchTick / 1000 );
            OSStrCbFormatW( szMaxExecute, sizeof( szMaxExecute ), L"%d", dwExecuteTick / 1000 );
            OSStrCbFormatW( szTaskType, sizeof( szTaskType ), L"%hs", m_szTaskType );

            if (0 == m_dwLastReportEndTick || 0 == m_cLastReportNotReported)
            {
                const WCHAR *rgszT[3];
                rgszT[0] = szTaskType;
                rgszT[1] = szMaxDispatch;
                rgszT[2] = szMaxExecute;

                UtilReportEvent(
                        eventWarning,
                        PERFORMANCE_CATEGORY,
                        TASK_INFO_STATS_TOO_LONG_ID,
                        3,
                        rgszT,
                        0,
                        NULL );
            }
            else
            {
                const WCHAR *rgszT[5];
                WCHAR szCountLastReport[32];
                WCHAR szTimeLastReport[32];

                OSStrCbFormatW( szCountLastReport, sizeof( szCountLastReport ), L"%d", m_cLastReportNotReported );
                OSStrCbFormatW( szTimeLastReport, sizeof( szTimeLastReport ), L"%d", (taskInfo.m_dwEndTick - m_dwLastReportEndTick)  / 1000 );

                rgszT[0] = szTaskType;
                rgszT[1] = szMaxDispatch;
                rgszT[2] = szMaxExecute;
                rgszT[3] = szCountLastReport;
                rgszT[4] = szTimeLastReport;

                UtilReportEvent(
                        eventWarning,
                        PERFORMANCE_CATEGORY,
                        TASK_INFO_STATS_TOO_LONG_AGAIN_ID,
                        5,
                        rgszT,
                        0,
                        NULL );
            }

            m_dwLastReportDispatch = dwDispatchTick;
            m_dwLastReportExecute = dwExecuteTick;
            m_cLastReportNotReported = 0;
            m_dwLastReportEndTick = taskInfo.m_dwEndTick;
        }

        m_critStats.Leave();
    }
}
