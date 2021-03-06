// **********************************************************************
//
// Copyright (c) 2003-2017 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#ifndef TEST_I_H
#define TEST_I_H

#include <Test.h>
#include <TestCommon.h>

class TestIntfControllerI;
ICE_DEFINE_PTR(TestIntfControllerIPtr, TestIntfControllerI);

class TestIntfI : public virtual Test::TestIntf, public IceUtil::Monitor<IceUtil::Mutex>
{
public:

    TestIntfI();

    virtual void op(const Ice::Current&);
    virtual int opWithResult(const Ice::Current&);
    virtual void opWithUE(const Ice::Current&);
    virtual int opWithResultAndUE(const Ice::Current&);
    virtual void opWithPayload(ICE_IN(Ice::ByteSeq), const Ice::Current&);
    virtual void opBatch(const Ice::Current&);
    virtual Ice::Int opBatchCount(const Ice::Current&);
    virtual void opWithArgs(Ice::Int&, Ice::Int&, Ice::Int&, Ice::Int&, Ice::Int&, Ice::Int&, Ice::Int&,
                            Ice::Int&, Ice::Int&, Ice::Int&, Ice::Int&, const Ice::Current&);
    virtual bool waitForBatch(Ice::Int, const Ice::Current&);
    virtual void close(Test::CloseMode, const Ice::Current&);
    virtual void sleep(Ice::Int, const Ice::Current&);
#ifdef ICE_CPP11_MAPPING
    virtual void startDispatchAsync(std::function<void()>, std::function<void(std::exception_ptr)>,
                                    const Ice::Current&);
#else
    virtual void startDispatch_async(const Test::AMD_TestIntf_startDispatchPtr&, const Ice::Current&);
#endif
    virtual void finishDispatch(const Ice::Current&);
    virtual void shutdown(const Ice::Current&);

    virtual bool supportsAMD(const Ice::Current&);
    virtual bool supportsFunctionalTests(const Ice::Current&);

    virtual void pingBiDir(ICE_IN(Ice::Identity), const Ice::Current&);

private:

    int _batchCount;
#ifdef ICE_CPP11_MAPPING
    std::vector<std::function<void()>> _pending;
#else
    std::vector<Test::AMD_TestIntf_startDispatchPtr> _pending;
#endif
};

class TestIntfControllerI : public Test::TestIntfController, IceUtil::Monitor<IceUtil::Mutex>
{
public:

    virtual void holdAdapter(const Ice::Current&);
    virtual void resumeAdapter(const Ice::Current&);

    TestIntfControllerI(const Ice::ObjectAdapterPtr&);

private:

    Ice::ObjectAdapterPtr _adapter;
};

#endif
