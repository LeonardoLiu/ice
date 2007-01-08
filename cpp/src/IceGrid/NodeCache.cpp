// **********************************************************************
//
// Copyright (c) 2003-2007 ZeroC, Inc. All rights reserved.
//
// This copy of Ice is licensed to you under the terms described in the
// ICE_LICENSE file included in this distribution.
//
// **********************************************************************

#include <IceUtil/Functional.h>
#include <Ice/Communicator.h>
#include <Ice/Properties.h>
#include <Ice/LoggerUtil.h>
#include <IceGrid/NodeCache.h>
#include <IceGrid/SessionI.h>
#include <IceGrid/NodeSessionI.h>
#include <IceGrid/ServerCache.h>
#include <IceGrid/ReplicaCache.h>
#include <IceGrid/DescriptorHelper.h>

#include <IcePatch2/Util.h>

using namespace std;
using namespace IceGrid;

namespace IceGrid
{

struct ToInternalServerDescriptor : std::unary_function<CommunicatorDescriptorPtr&, void>
{
    ToInternalServerDescriptor(const InternalServerDescriptorPtr& descriptor, const InternalNodeInfoPtr& node) :
	_desc(descriptor),
	_node(node)
    {
    }
    
    void
    operator()(const CommunicatorDescriptorPtr& desc)
    {
	//
	// Figure out the configuration file name for the communicator
	// (if it's a service, it's "config_<service name>", if it's
	// the server, it's just "config").
	//
	string filename = "config";
	ServiceDescriptorPtr svc = ServiceDescriptorPtr::dynamicCast(desc);
	if(svc)
	{
	    filename += "_" + svc->name;
	}
	PropertyDescriptorSeq& props = _desc->properties[filename];

	PropertyDescriptorSeq communicatorProps = desc->propertySet.properties;

	//
	// Add the adapters and their configuration.
	//
	for(AdapterDescriptorSeq::const_iterator q = desc->adapters.begin(); q != desc->adapters.end(); ++q)
	{
	    _desc->adapters.push_back(new InternalAdapterDescriptor(q->id, q->serverLifetime));

	    props.push_back(createProperty("# Object adapter " + q->name));
	    PropertyDescriptor prop = removeProperty(communicatorProps, "Ice.OA." + q->name + ".Endpoints");
	    prop.name = "Ice.OA." + q->name + ".Endpoints";
	    props.push_back(prop);
	    props.push_back(createProperty("Ice.OA." + q->name + ".AdapterId", q->id));
	    if(!q->replicaGroupId.empty())
	    {
		props.push_back(createProperty("Ice.OA." + q->name + ".ReplicaGroupId", q->replicaGroupId));
	    }
	    if(q->registerProcess)
	    {
		props.push_back(createProperty("Ice.OA." + q->name + ".RegisterProcess", "1"));
		_desc->processRegistered = true;
	    }
	}

	_desc->logs.insert(_desc->logs.end(), desc->logs.begin(), desc->logs.end());

	const string dbsPath = _node->dataDir + "/servers/" + _desc->id + "/dbs/";
	for(DbEnvDescriptorSeq::const_iterator p = desc->dbEnvs.begin(); p != desc->dbEnvs.end(); ++p)
	{
	    props.push_back(createProperty("# Database environment " + p->name));
	    if(p->dbHome.empty())
	    {
		_desc->dbEnvs.push_back(new InternalDbEnvDescriptor(p->name, p->properties));
		props.push_back(createProperty("Freeze.DbEnv." + p->name + ".DbHome", dbsPath + p->name));
	    }
	    else
	    {
		props.push_back(createProperty("Freeze.DbEnv." + p->name + ".DbHome", p->dbHome));
	    }
	}

	//
	// Copy the communicator descriptor properties.
	//
	if(!communicatorProps.empty())
	{
	    if(svc)
	    {
		props.push_back(createProperty("# Service descriptor properties"));
	    }
	    else
	    {
		props.push_back(createProperty("# Server descriptor properties"));
	    }
	    copy(communicatorProps.begin(), communicatorProps.end(), back_inserter(props));
	}
    }

    PropertyDescriptor
    removeProperty(PropertyDescriptorSeq& properties, const string& name)
    {
	string value;
	PropertyDescriptorSeq::iterator p = properties.begin();
	while(p != properties.end())
	{
	    if(p->name == name)
	    {
		value = p->value; 
		p = properties.erase(p);
	    }
	    else
	    {
		++p;
	    }
	}
	PropertyDescriptor desc;
	desc.name = name;
	desc.value = value;
	return desc;
    }

    InternalServerDescriptorPtr _desc;
    InternalNodeInfoPtr _node;
};

class LoadCB : public AMI_Node_loadServer
{
public:

    LoadCB(const TraceLevelsPtr& traceLevels, 
	   const ServerEntryPtr& server, 
	   const string& node,
	   int timeout) : 
	_traceLevels(traceLevels), _server(server), _id(server->getId()), _node(node), _timeout(timeout)
    {
    }

    void
    ice_response(const ServerPrx& proxy, const AdapterPrxDict& adpts, int at, int dt)
    {
	if(_traceLevels && _traceLevels->server > 1)
	{
	    Ice::Trace out(_traceLevels->logger, _traceLevels->serverCat);
	    out << "loaded `" << _id << "' on node `" << _node << "'";	
	}

	//
	// Add the node session timeout on the proxies.
	//
	ServerPrx server = ServerPrx::uncheckedCast(proxy->ice_timeout(_timeout * 1000));
	AdapterPrxDict adapters;
	for(AdapterPrxDict::const_iterator p = adpts.begin(); p != adpts.end(); ++p)
	{
	    adapters.insert(make_pair(p->first, AdapterPrx::uncheckedCast(p->second->ice_timeout(_timeout * 1000))));
	}

	_server->loadCallback(server, adapters, at + _timeout, dt + _timeout);
    }

    void
    ice_exception(const Ice::Exception& ex)
    {
	try
	{
	    ex.ice_throw();
	}
	catch(const DeploymentException& ex)
	{
	    if(_traceLevels && _traceLevels->server > 1)
	    {
		Ice::Trace out(_traceLevels->logger, _traceLevels->serverCat);
		out << "couldn't load `" << _id << "' on node `" << _node << "':\n" << ex.reason;
	    }

	    ostringstream os;
	    os << "couldn't load `" << _id << "' on node `" << _node << "':\n" << ex.reason;
	    _server->exception(DeploymentException(os.str()));
	}
	catch(const Ice::Exception& ex)
	{
	    if(_traceLevels && _traceLevels->server > 1)
	    {
		Ice::Trace out(_traceLevels->logger, _traceLevels->serverCat);
		out << "couldn't load `" << _id << "' on node `" << _node << "':\n" << ex;
	    }

	    ostringstream os;
	    os << ex;
	    _server->exception(NodeUnreachableException(_node, os.str()));
	}
    }

private:
    
    const TraceLevelsPtr _traceLevels;
    const ServerEntryPtr _server;
    const string _id;
    const string _node;
    const int _timeout;
};

class DestroyCB : public AMI_Node_destroyServer
{
public:

    DestroyCB(const TraceLevelsPtr& traceLevels, const ServerEntryPtr& server, const string& node) : 
	_traceLevels(traceLevels), _server(server), _id(server->getId()), _node(node)
    {
    }

    void
    ice_response()
    {
	if(_traceLevels && _traceLevels->server > 1)
	{
	    Ice::Trace out(_traceLevels->logger, _traceLevels->serverCat);
	    out << "unloaded `" << _id << "' on node `" << _node << "'";	
	}
	_server->destroyCallback();
    }

    void
    ice_exception(const Ice::Exception& ex)
    {
	try
	{
	    ex.ice_throw();
	}
	catch(const DeploymentException& ex)
	{
	    if(_traceLevels && _traceLevels->server > 1)
	    {
		Ice::Trace out(_traceLevels->logger, _traceLevels->serverCat);
		out << "couldn't unload `" << _id << "' on node `" << _node << "':\n" << ex.reason;
	    }

	    ostringstream os;
	    os << "couldn't unload `" << _id << "' on node `" << _node << "':\n" << ex.reason;
	    _server->exception(DeploymentException(os.str()));
	}
	catch(const Ice::Exception& ex)
	{
	    if(_traceLevels && _traceLevels->server > 1)
	    {
		Ice::Trace out(_traceLevels->logger, _traceLevels->serverCat);
		out << "couldn't unload `" << _id << "' on node `" << _node << "':\n" << ex;
	    }
	    ostringstream os;
	    os << ex;
	    _server->exception(NodeUnreachableException(_node, os.str()));
	}
    }

private:
    
    const TraceLevelsPtr _traceLevels;
    const ServerEntryPtr _server;
    const string _id;
    const string _node;
};

class RegisterCB : public AMI_Node_registerWithReplica
{
public:

    RegisterCB(const NodeEntryPtr& node) : _node(node)
    {
    }

    void
    ice_response()
    {
	_node->finishedRegistration();
    }

    void
    ice_exception(const Ice::Exception& ex)
    {
	_node->finishedRegistration(ex);
    }

private:
    const NodeEntryPtr _node;
};

};

NodeCache::NodeCache(const Ice::CommunicatorPtr& communicator, ReplicaCache& replicaCache, const string& replicaName) :
    _communicator(communicator),
    _replicaName(replicaName),
    _replicaCache(replicaCache)
{
}

NodeEntryPtr
NodeCache::get(const string& name, bool create) const
{
    Lock sync(*this);
    NodeEntryPtr entry = getImpl(name);
    if(!entry && create)
    {
	NodeCache& self = const_cast<NodeCache&>(*this);
	entry = new NodeEntry(self, name);
	self.addImpl(name, entry);
    }
    if(!entry)
    {
	NodeNotExistException ex;
	ex.name = name;
	throw ex;
    }
    return entry;
}

NodeEntry::NodeEntry(NodeCache& cache, const std::string& name) : 
    _cache(cache),
    _ref(0),
    _name(name),
    _registering(false)
{
}

NodeEntry::~NodeEntry()
{
}

void
NodeEntry::addDescriptor(const string& application, const NodeDescriptor& descriptor)
{
    Lock sync(*this);
    _descriptors.insert(make_pair(application, descriptor));
}

void
NodeEntry::removeDescriptor(const string& application)
{
    Lock sync(*this);
    _descriptors.erase(application);
}

void
NodeEntry::addServer(const ServerEntryPtr& entry)
{
    Lock sync(*this);
    _servers.insert(make_pair(entry->getId(), entry));
}

void
NodeEntry::removeServer(const ServerEntryPtr& entry)
{
    Lock sync(*this);
    _servers.erase(entry->getId());
}

void
NodeEntry::setSession(const NodeSessionIPtr& session)
{
    Lock sync(*this);

    if(session)
    {
	while(_session)
	{
	    if(_session->isDestroyed())
	    {
		// If the current session has just been destroyed, wait for the setSession(0) call.
		assert(session != _session);
		wait();
	    }
	    else
	    {
		NodeSessionIPtr session = _session;
		sync.release();
		try
		{
		    session->getNode()->ice_ping();
		    throw NodeActiveException();
		}
		catch(const Ice::LocalException&)
		{
		    try
		    {
			session->destroy();
		    }
		    catch(const Ice::ObjectNotExistException&)
		    {
		    }
		}
		sync.acquire();
	    }
	}
	
	//
	// Clear the saved proxy, the node has established a session
	// so we won't need anymore to try to register it with this
	// registry.
	//
	_proxy = 0;
    }
    else
    {
	if(!_session)
	{
	    return;
	}
    }
    
    _session = session;
    notifyAll();

    if(_registering)
    {
	_registering = false;
	notifyAll();
    }

    if(session)
    {
	if(_cache.getTraceLevels() && _cache.getTraceLevels()->node > 0)
	{
	    Ice::Trace out(_cache.getTraceLevels()->logger, _cache.getTraceLevels()->nodeCat);
	    out << "node `" << _name << "' up";
	}
    }
    else
    {
	if(_cache.getTraceLevels() && _cache.getTraceLevels()->node > 0)
	{
	    Ice::Trace out(_cache.getTraceLevels()->logger, _cache.getTraceLevels()->nodeCat);
	    out << "node `" << _name << "' down";
	}
    }
}

NodePrx
NodeEntry::getProxy() const
{
    Lock sync(*this);
    checkSession();
    return _session->getNode();
}

InternalNodeInfoPtr
NodeEntry::getInfo() const
{
    Lock sync(*this);
    checkSession();
    return _session->getInfo();
}

ServerEntrySeq
NodeEntry::getServers() const
{
    Lock sync(*this);
    ServerEntrySeq entries;
    for(map<string, ServerEntryPtr>::const_iterator p = _servers.begin(); p != _servers.end(); ++p)
    {
	entries.push_back(p->second);
    }
    return entries;
}

LoadInfo
NodeEntry::getLoadInfoAndLoadFactor(const string& application, float& loadFactor) const
{
    Lock sync(*this);
    checkSession();

    map<string, NodeDescriptor>::const_iterator p = _descriptors.find(application);
    if(p == _descriptors.end())
    {
	throw NodeNotExistException(); // The node doesn't exist in the given application.
    }
    
    //
    // TODO: Cache the load factors? Parsing the load factor for each
    // call could be costly.
    //
    loadFactor = -1.0f;
    if(!p->second.loadFactor.empty())
    {
	istringstream is(p->second.loadFactor);
	is >> loadFactor;
    }
    if(loadFactor < 0.0f)
    {
	if(_session->getInfo()->os != "Windows")
	{
	    //
	    // On Unix platforms, we divide the load averages by the
	    // number of processors. A load of 2 on a dual processor
	    // machine is the same as a load of 1 on a single process
	    // machine.
	    //
	    loadFactor = 1.0f / _session->getInfo()->nProcessors;
	}
	else
	{
	    //
	    // On Windows, load1, load5 and load15 are the average of
	    // the CPU utilization (all CPUs). We don't need to divide
	    // by the number of CPU.
	    //
	    loadFactor = 1.0f;
	}
    }

    return _session->getLoadInfo();
}

NodeSessionIPtr
NodeEntry::getSession() const
{
    Lock sync(*this);
    checkSession();
    return _session;
}

bool
NodeEntry::canRemove()
{
    Lock sync(*this);
    return _servers.empty() && !_session && _descriptors.empty();
}

void
NodeEntry::loadServer(const ServerEntryPtr& entry, const ServerInfo& server, const SessionIPtr& session, int timeout)
{
    try
    {
	NodePrx node;
	int sessionTimeout;
	InternalServerDescriptorPtr desc;
	{
	    Lock sync(*this);
	    checkSession();
	    node = _session->getNode();
	    sessionTimeout = _session->getTimeout();

	    //
	    // Check if we should use a specific timeout (the load
	    // call can deactivate the server and it can take some
	    // time to deactivate, up to "deactivation-timeout"
	    // seconds).
	    // 
	    if(timeout > 0 && timeout != sessionTimeout)
	    {
		node = NodePrx::uncheckedCast(node->ice_timeout(timeout + sessionTimeout));
	    }

	    ServerInfo info = server;
	    try
	    {
		info.descriptor = getServerDescriptor(server, session);
	    }
	    catch(const DeploymentException&)
	    {
		//
		// We ignore the deployment error for now (which can
		// only be caused in theory by session variables not
		// being defined because the server isn't
		// allocated...)
		//
	    }
	    desc = getInternalServerDescriptor(info);
	}
	assert(desc);
	
	if(_cache.getTraceLevels() && _cache.getTraceLevels()->server > 2)
	{
	    Ice::Trace out(_cache.getTraceLevels()->logger, _cache.getTraceLevels()->serverCat);
	    out << "loading `" << desc->id << "' on node `" << _name << "'";
	    if(session)
	    {
		out << " for session `" << session->getId() << "'";
	    }
	}
	
	AMI_Node_loadServerPtr amiCB = new LoadCB(_cache.getTraceLevels(), entry, _name, sessionTimeout);
	node->loadServer_async(amiCB, desc, _cache.getReplicaName());
    }
    catch(const NodeUnreachableException& ex)
    {
	entry->exception(ex);
    }
}

void
NodeEntry::destroyServer(const ServerEntryPtr& entry, const ServerInfo& info, int timeout)
{
    try
    {
	NodePrx node;
	{
	    Lock sync(*this);
	    checkSession();
	    node = _session->getNode();
	    int sessionTimeout = _session->getTimeout();

	    //
	    // Check if we should use a specific timeout (the load
	    // call can deactivate the server and it can take some
	    // time to deactivate, up to "deactivation-timeout"
	    // seconds).
	    // 
	    if(timeout > 0 && timeout != sessionTimeout)
	    {
		node = NodePrx::uncheckedCast(node->ice_timeout(timeout + sessionTimeout));
	    }
	}
	
	if(_cache.getTraceLevels() && _cache.getTraceLevels()->server > 2)
	{
	    Ice::Trace out(_cache.getTraceLevels()->logger, _cache.getTraceLevels()->serverCat);
	    out << "unloading `" << info.descriptor->id << "' on node `" << _name << "'";	
	}

	AMI_Node_destroyServerPtr amiCB = new DestroyCB(_cache.getTraceLevels(), entry, _name);
	node->destroyServer_async(amiCB, info.descriptor->id, info.uuid, info.revision, _cache.getReplicaName());
    }
    catch(const NodeUnreachableException& ex)
    {
	entry->exception(ex);
    }
}

ServerInfo
NodeEntry::getServerInfo(const ServerInfo& server, const SessionIPtr& session)
{
    Lock sync(*this);
    checkSession();

    ServerInfo info = server;
    info.descriptor = getServerDescriptor(server, session);
    assert(info.descriptor);
    return info;
}

ServerDescriptorPtr
NodeEntry::getServerDescriptor(const ServerInfo& server, const SessionIPtr& session)
{
    assert(_session);

    Resolver resolve(_session->getInfo(), _cache.getCommunicator());
    resolve.setReserved("application", server.application);
    resolve.setReserved("server", server.descriptor->id);
    resolve.setContext("server `${server}'");

    if(session)
    {
	resolve.setReserved("session.id", session->getId());
    }

    IceBoxDescriptorPtr iceBox = IceBoxDescriptorPtr::dynamicCast(server.descriptor);
    if(iceBox)
    {
	return IceBoxHelper(iceBox).instantiate(resolve, PropertyDescriptorSeq(), PropertySetDescriptorDict());
    }
    else
    {
	return ServerHelper(server.descriptor).instantiate(resolve, PropertyDescriptorSeq(), 
							   PropertySetDescriptorDict());
    }
}

void
NodeEntry::__incRef()
{
    Lock sync(*this);
    assert(_ref >= 0);
    ++_ref;
}

void
NodeEntry::__decRef()
{
    //
    // The node entry implements its own reference counting. If the
    // reference count drops to 1, this means that only the cache
    // holds a reference on the node entry. If that's the case, we
    // check if the node entry can be removed or not and if it can be
    // removed we remove it from the cache map.
    //

    bool doRemove = false;
    bool doDelete = false;
    {
	Lock sync(*this); // We use a recursive mutex so it's fine to
			  // create Ptr with the mutex locked.
	assert(_ref > 0);
	--_ref;
	
	if(_ref == 1)
	{
	    doRemove = canRemove();
	}
	else if(_ref == 0)
	{
	    doDelete = true;
	}
    }

    if(doRemove)
    {
	_cache.remove(_name);
    }
    else if(doDelete)
    {
	delete this;
    }
}

void
NodeEntry::checkSession() const
{ 
    if(_session && !_session->isDestroyed())
    {
	return;
    }
    else if(!_proxy && !_registering)
    {
	throw NodeUnreachableException(_name, "the node is not active");
    }
    else if(_proxy)
    {
	//
	// If the node proxy is set, we attempt to get the node to
	// register with this registry.
	//	
	assert(!_registering);

	if(_cache.getTraceLevels() && _cache.getTraceLevels()->node > 0)
	{
	    Ice::Trace out(_cache.getTraceLevels()->logger, _cache.getTraceLevels()->nodeCat);
	    out << "creating node `" << _name << "' session";
	}

	//
	// NOTE: setting _registering to true must be done before the
	// call otherwise if the callback is call immediately we'll
	// hang in the while loop.
	//
	_registering = true; 
	NodeEntry* self = const_cast<NodeEntry*>(this);
	_proxy->registerWithReplica_async(new RegisterCB(self), _cache.getReplicaCache().getInternalRegistry());
	_proxy = 0; // Registration with the proxy is only attempted once.
    }

    while(_registering)
    {
	wait();
    }
    
    if(!_session)
    {
	throw NodeUnreachableException(_name, "the node is not active");
    }
}

void
NodeEntry::setProxy(const NodePrx& node)
{
    Lock sync(*this);
    
    //
    // If the node has already established a session with the
    // registry, no need to remember its proxy, we don't need to get
    // it to register with this registry since it's already
    // registered.
    //
    if(!_session)
    {
	_proxy = node;
    }
}

void
NodeEntry::finishedRegistration()
{
    Lock sync(*this);
    if(_cache.getTraceLevels() && _cache.getTraceLevels()->node > 0)
    {
	Ice::Trace out(_cache.getTraceLevels()->logger, _cache.getTraceLevels()->nodeCat);
	if(_session)
	{
	    out << "node `" << _name << "' session created";
	}
	else
	{
	    out << "node `" << _name << "' session creation failed";
	}
    }    

    if(_registering)
    {
	_registering = false;
	notifyAll();
    }
}

void
NodeEntry::finishedRegistration(const Ice::Exception& ex)
{
    Lock sync(*this);
    if(_cache.getTraceLevels() && _cache.getTraceLevels()->node > 0)
    {
	Ice::Trace out(_cache.getTraceLevels()->logger, _cache.getTraceLevels()->nodeCat);
	out << "node `" << _name << "' session creation failed:\n" << ex;
    }    

    if(_registering)
    {
	_registering = false;
	notifyAll();
    }
}

InternalServerDescriptorPtr
NodeEntry::getInternalServerDescriptor(const ServerInfo& info) const
{
    assert(_session);

    InternalServerDescriptorPtr server = new InternalServerDescriptor();
    server->id = info.descriptor->id;
    server->application = info.application;
    server->uuid = info.uuid;
    server->revision = info.revision;
    server->sessionId = info.sessionId;
    server->exe = info.descriptor->exe;
    server->pwd = info.descriptor->pwd;
    server->user = info.descriptor->user;
    server->activation = info.descriptor->activation;
    server->activationTimeout = info.descriptor->activationTimeout;
    server->deactivationTimeout = info.descriptor->deactivationTimeout;
    if(!info.descriptor->iceVersion.empty())
    {
	server->iceVersion = info.descriptor->iceVersion;
    }
    else
    {
	server->iceVersion = string(ICE_STRING_VERSION);
    }
    server->applicationDistrib = info.descriptor->applicationDistrib;
    if(!info.descriptor->distrib.icepatch.empty())
    {
	server->distrib = new InternalDistributionDescriptor(info.descriptor->distrib.icepatch, 
							     info.descriptor->distrib.directories);
    }
    server->options = info.descriptor->options;
    server->envs = info.descriptor->envs;
    server->processRegistered = false; // Assigned for each communicator (see below)
    // server->logs: assigned for each communicator (see below)
    // server->adapters: assigned for each communicator (see below)
    // server->dbEnvs: assigned for each communicator (see below)
    // server->properties: assigned for each communicator (see below)

    //
    // Add server properties.
    //
    PropertyDescriptorSeq& props = server->properties["config"];
    props.push_back(createProperty("# Server configuration"));
    props.push_back(createProperty("Ice.ServerId", info.descriptor->id));
    props.push_back(createProperty("Ice.ProgramName", info.descriptor->id));

    //
    // Add IceBox properties.
    //
    string servicesStr;
    IceBoxDescriptorPtr iceBox = IceBoxDescriptorPtr::dynamicCast(info.descriptor);
    if(iceBox)
    {
	for(ServiceInstanceDescriptorSeq::const_iterator p = iceBox->services.begin(); p != iceBox->services.end();++p)
	{
	    ServiceDescriptorPtr s = p->descriptor;
	    const string path = _session->getInfo()->dataDir + "/servers/" + server->id + "/config/config_" + s->name;
	    props.push_back(
		createProperty("IceBox.Service." + s->name, s->entry + " --Ice.Config=\"" + path + "\""));
	    servicesStr += s->name + " ";
	}
	props.push_back(createProperty("IceBox.LoadOrder", servicesStr));
    }

    //
    // Now, for each communicator of the descriptor, add the necessary
    // logs, adapters, db envs and properties to the internal server 
    // descriptor.
    //
    forEachCommunicator(ToInternalServerDescriptor(server, _session->getInfo()))(info.descriptor);

    //
    // Transform the OA properties to the old naming (Ice.OA.XXX.* ->
    // XXX.*) for server with a version < 3.2.0
    // 
    if(getMMVersion(server->iceVersion) < 30200)
    {
	const string oaPrefix = "Ice.OA.";
	for(PropertyDescriptorSeqDict::iterator p = server->properties.begin(); p != server->properties.end(); ++p)
	{
	    for(PropertyDescriptorSeq::iterator q = p->second.begin(); q != p->second.end(); ++q)
	    {
		if(q->name.find(oaPrefix) == 0)
		{
		    q->name = q->name.substr(oaPrefix.size());
		}
	    }
	}
    }

    return server;
}
