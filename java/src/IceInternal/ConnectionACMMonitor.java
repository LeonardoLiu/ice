// **********************************************************************
//
// Copyright (c) 2003-2014 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

package IceInternal;

class ConnectionACMMonitor implements ACMMonitor
{
    ConnectionACMMonitor(FactoryACMMonitor parent, java.util.concurrent.ScheduledExecutorService timer,
        ACMConfig config)
    {
        _parent = parent;
        _timer = timer;
        _config = config;
    }

    protected synchronized void
    finalize()
        throws Throwable
    {
        try
        {
            IceUtilInternal.Assert.FinalizerAssert(_connection == null);
        }
        catch(java.lang.Exception ex)
        {
        }
        finally
        {
            super.finalize();
        }
    }

    public synchronized void
    add(Ice.ConnectionI connection)
    {
        assert(_connection == null);
        _connection = connection;
        if(_config.timeout > 0)
        {
            _future = _timer.scheduleAtFixedRate(this, _config.timeout / 2, _config.timeout / 2,
                java.util.concurrent.TimeUnit.MILLISECONDS);
        }
    }

    public synchronized void
    remove(Ice.ConnectionI connection)
    {
        assert(_connection == connection);
        _connection = null;
        if(_config.timeout > 0)
        {
            _future.cancel(false);
            _future = null;
        }
    }
    
    public void
    reap(Ice.ConnectionI connection)
    {
        _parent.reap(connection);
    }
    
    public ACMMonitor
    acm(Ice.IntOptional timeout, Ice.Optional<Ice.ACMClose> close, Ice.Optional<Ice.ACMHeartbeat> heartbeat)
    {
        return _parent.acm(timeout, close, heartbeat);
    }
    
    public Ice.ACM
    getACM()
    {
        Ice.ACM acm = new Ice.ACM();
        acm.timeout = _config.timeout / 1000;
        acm.close = _config.close;
        acm.heartbeat = _config.heartbeat;
        return acm;
    }
    
    public void
    run()
    {
        Ice.ConnectionI connection;
        synchronized(this)
        {
            if(_connection == null)
            {
                return;
            }
            connection = _connection;
        }
        
        try
        {          
            connection.monitor(Time.currentMonotonicTimeMillis(), _config);
        }
        catch(Exception ex)
        {   
            _parent.handleException(ex);
        }
    }

    final private FactoryACMMonitor _parent;
    final private java.util.concurrent.ScheduledExecutorService _timer;
    private java.util.concurrent.Future<?> _future;
    final private ACMConfig _config;

    private Ice.ConnectionI _connection;
};